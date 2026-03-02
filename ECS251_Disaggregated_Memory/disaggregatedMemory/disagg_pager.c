// disagg_pager.c
// NOTE: This SIGSEGV-based pager is a class-project approach.
// It uses network I/O inside a signal handler (not async-signal-safe).
// In practice you'd use userfaultfd (Linux) or a kernel module.
// But this is enough to demonstrate "remote paging" behavior.

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "disagg_pager.h"
#include "netmem_page.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct {
  uint8_t resident : 1;
  uint8_t dirty    : 1;
  uint8_t writable : 1;
  uint8_t _pad     : 5;
} page_meta_t;

struct disagg_pager {
  netmem_t nm;
  uint8_t* base;
  uint64_t bytes;
  uint32_t num_pages;
  page_meta_t* meta;
  struct sigaction old_segv;
};

static disagg_pager_t* g_pager = NULL;

static inline uint64_t align_down(uint64_t x, uint64_t a) { return x & ~(a - 1); }

static void segv_handler(int sig, siginfo_t* info, void* uctx) {
  (void)sig; (void)uctx;
  if (!g_pager || !info || !info->si_addr) goto chain;

  uint8_t* addr = (uint8_t*)info->si_addr;
  uint8_t* base = g_pager->base;
  uint8_t* end  = g_pager->base + g_pager->bytes;

  if (!(addr >= base && addr < end)) goto chain;

  uint64_t off = (uint64_t)(addr - base);
  uint64_t page_off = align_down(off, NETMEM_PAGE_SZ);
  uint32_t page_idx = (uint32_t)(page_off / NETMEM_PAGE_SZ);
  uint8_t* page_addr = base + page_off;

  page_meta_t* m = &g_pager->meta[page_idx];

  // If not resident, fetch page then map as read-only (first touch).
  if (!m->resident) {
    uint8_t buf[NETMEM_PAGE_SZ];
    if (netmem_read_page(&g_pager->nm, page_idx, buf) != 0) {
      // fail hard
      _exit(127);
    }
    if (mprotect(page_addr, NETMEM_PAGE_SZ, PROT_READ) != 0) _exit(127);
    memcpy(page_addr, buf, NETMEM_PAGE_SZ);
    m->resident = 1;
    m->writable = 0;
    // return; re-executes faulting instruction (should now succeed if it was a read)
    return;
  }

  // Resident but not writable => treat as first write fault
  if (!m->writable) {
    if (mprotect(page_addr, NETMEM_PAGE_SZ, PROT_READ | PROT_WRITE) != 0) _exit(127);
    m->writable = 1;
    m->dirty = 1;
    return;
  }

  // If we got here, something weird (e.g., execute fault). Chain.
chain:
  if (g_pager) {
    if (g_pager->old_segv.sa_flags & SA_SIGINFO) {
      if (g_pager->old_segv.sa_sigaction) g_pager->old_segv.sa_sigaction(sig, info, uctx);
    } else {
      if (g_pager->old_segv.sa_handler == SIG_DFL) {
        signal(SIGSEGV, SIG_DFL);
        raise(SIGSEGV);
      } else if (g_pager->old_segv.sa_handler == SIG_IGN) {
        return;
      } else if (g_pager->old_segv.sa_handler) {
        g_pager->old_segv.sa_handler(sig);
      }
    }
  } else {
    signal(SIGSEGV, SIG_DFL);
    raise(SIGSEGV);
  }
}

disagg_pager_t* disagg_open(const char* ip, int port, uint64_t region_bytes, void** out_base) {
  if (!out_base) return NULL;
  *out_base = NULL;

  disagg_pager_t* p = (disagg_pager_t*)calloc(1, sizeof(*p));
  if (!p) return NULL;

  if (netmem_connect(&p->nm, ip, port) != 0) { free(p); return NULL; }
  if (netmem_alloc_region(&p->nm, region_bytes) != 0) { netmem_close(&p->nm); free(p); return NULL; }

  p->bytes = p->nm.region_bytes;
  p->num_pages = p->nm.num_pages;

  p->meta = (page_meta_t*)calloc(p->num_pages, sizeof(page_meta_t));
  if (!p->meta) { netmem_close(&p->nm); free(p); return NULL; }

  void* base = mmap(NULL, (size_t)p->bytes, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (base == MAP_FAILED) {
    free(p->meta);
    netmem_close(&p->nm);
    free(p);
    return NULL;
  }
  p->base = (uint8_t*)base;

  // Install handler (single pager instance for simplicity)
  g_pager = p;

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = segv_handler;
  sa.sa_flags = SA_SIGINFO | SA_NODEFER;
  sigemptyset(&sa.sa_mask);

  if (sigaction(SIGSEGV, &sa, &p->old_segv) != 0) {
    munmap(p->base, (size_t)p->bytes);
    free(p->meta);
    netmem_close(&p->nm);
    free(p);
    g_pager = NULL;
    return NULL;
  }

  *out_base = p->base;
  return p;
}

int disagg_flush(disagg_pager_t* p) {
  if (!p) return -1;
  for (uint32_t i = 0; i < p->num_pages; i++) {
    if (p->meta[i].resident && p->meta[i].dirty) {
      uint8_t* page_addr = p->base + ((uint64_t)i * NETMEM_PAGE_SZ);
      // Ensure readable
      // (if it was PROT_READ only, memcpy would still work; writeback doesn't require writable)
      if (netmem_write_page(&p->nm, i, page_addr) != 0) return -1;
      p->meta[i].dirty = 0;
    }
  }
  return 0;
}

int disagg_close(disagg_pager_t* p) {
  if (!p) return -1;

  (void)disagg_flush(p);

  // Restore old handler only if this is the global pager
  if (g_pager == p) {
    sigaction(SIGSEGV, &p->old_segv, NULL);
    g_pager = NULL;
  }

  munmap(p->base, (size_t)p->bytes);
  free(p->meta);
  netmem_close(&p->nm);
  free(p);
  return 0;
}
