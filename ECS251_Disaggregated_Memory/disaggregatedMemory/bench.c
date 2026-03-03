// bench.c
// Unified benchmark driver for:
//   - local:  local anonymous memory
//   - rpc:    explicit RPC per 4KB page (netmem_read_page/netmem_write_page)
//   - pager:  mmap(PROT_NONE)+SIGSEGV pager (disagg_open/disagg_flush)
//
// Outputs CSV to stdout.
//
// Build expects the earlier files exist in the same dir:
//   disagg_pager.c/.h, netmem_page.c/.h
//
// Example:
//   ./memsrv_page 4444
//   ./bench --mode=pager --ip=127.0.0.1 --port=4444 --region-mb=256 --pattern=seq --dirty=1 --passes=1 --header
//
// Notes:
// - pager mode: first pass includes faults; "resident" pass is after pages are mapped
// - rpc mode: every page op pays network+server overhead
// - dirty is fraction of pages written (0..1). Writes are 1 byte per page for pager/local,
//   but full 4KB for rpc flush/write operations.

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "disagg_pager.h"
#include "netmem_page.h"

#ifndef PAGE_SZ
#define PAGE_SZ 4096u
#endif

typedef enum { MODE_LOCAL, MODE_RPC, MODE_PAGER } mode_t;
typedef enum { PAT_SEQ, PAT_RAND } pattern_t;

typedef struct {
  mode_t mode;
  pattern_t pattern;
  const char* ip;
  int port;
  uint64_t region_mb;
  double dirty;      // 0..1 fraction of pages to write
  uint32_t passes;   // number of access passes (first pass always included)
  uint64_t seed;
  bool header;
} cfg_t;

static uint64_t ns_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t xorshift64(uint64_t* s) {
  uint64_t x = *s;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  *s = x;
  return x;
}

static void usage(const char* argv0) {
  fprintf(stderr,
          "usage: %s [options]\n"
          "  --mode=local|rpc|pager\n"
          "  --pattern=seq|rand\n"
          "  --region-mb=N            (default 256)\n"
          "  --dirty=F                (0..1, default 1)\n"
          "  --passes=N               (default 2; for pager: pass1=faulting, pass2=resident)\n"
          "  --ip=A.B.C.D             (default 127.0.0.1; for rpc/pager)\n"
          "  --port=N                 (default 4444; for rpc/pager)\n"
          "  --seed=N                 (default 1)\n"
          "  --header                 (print CSV header)\n",
          argv0);
}

static bool parse_kv(const char* arg, const char* key, const char** out_val) {
  size_t klen = strlen(key);
  if (strncmp(arg, key, klen) == 0 && arg[klen] == '=') {
    *out_val = arg + klen + 1;
    return true;
  }
  return false;
}

static int parse_cfg(cfg_t* c, int argc, char** argv) {
  c->mode = MODE_PAGER;
  c->pattern = PAT_SEQ;
  c->ip = "127.0.0.1";
  c->port = 4444;
  c->region_mb = 256;
  c->dirty = 1.0;
  c->passes = 2;
  c->seed = 1;
  c->header = false;

  for (int i = 1; i < argc; i++) {
    const char* v = NULL;
    if (strcmp(argv[i], "--header") == 0) {
      c->header = true;
    } else if (parse_kv(argv[i], "--mode", &v)) {
      if (strcmp(v, "local") == 0) c->mode = MODE_LOCAL;
      else if (strcmp(v, "rpc") == 0) c->mode = MODE_RPC;
      else if (strcmp(v, "pager") == 0) c->mode = MODE_PAGER;
      else return -1;
    } else if (parse_kv(argv[i], "--pattern", &v)) {
      if (strcmp(v, "seq") == 0) c->pattern = PAT_SEQ;
      else if (strcmp(v, "rand") == 0) c->pattern = PAT_RAND;
      else return -1;
    } else if (parse_kv(argv[i], "--region-mb", &v)) {
      c->region_mb = strtoull(v, NULL, 10);
    } else if (parse_kv(argv[i], "--dirty", &v)) {
      c->dirty = strtod(v, NULL);
      if (!(c->dirty >= 0.0 && c->dirty <= 1.0)) return -1;
    } else if (parse_kv(argv[i], "--passes", &v)) {
      c->passes = (uint32_t)strtoul(v, NULL, 10);
      if (c->passes == 0) c->passes = 1;
    } else if (parse_kv(argv[i], "--ip", &v)) {
      c->ip = v;
    } else if (parse_kv(argv[i], "--port", &v)) {
      c->port = atoi(v);
    } else if (parse_kv(argv[i], "--seed", &v)) {
      c->seed = strtoull(v, NULL, 10);
      if (c->seed == 0) c->seed = 1;
    } else {
      return -1;
    }
  }
  return 0;
}

static void build_order(uint32_t* order, uint32_t n, pattern_t pat, uint64_t seed) {
  for (uint32_t i = 0; i < n; i++) order[i] = i;
  if (pat == PAT_SEQ) return;

  // Fisher-Yates shuffle
  uint64_t s = seed;
  for (uint32_t i = n; i > 1; i--) {
    uint32_t j = (uint32_t)(xorshift64(&s) % i);
    uint32_t tmp = order[i - 1];
    order[i - 1] = order[j];
    order[j] = tmp;
  }
}

static uint32_t count_dirty_pages(uint32_t pages, double dirty_frac) {
  uint64_t x = (uint64_t)((double)pages * dirty_frac + 0.5);
  if (x > pages) x = pages;
  return (uint32_t)x;
}

static void mark_dirty_set(uint8_t* is_dirty, uint32_t pages, uint32_t dirty_pages, uint64_t seed) {
  memset(is_dirty, 0, pages);
  if (dirty_pages == 0) return;
  if (dirty_pages == pages) { memset(is_dirty, 1, pages); return; }

  // sample dirty_pages unique indices by shuffling an order array
  uint32_t* tmp = (uint32_t*)malloc((size_t)pages * sizeof(uint32_t));
  if (!tmp) { memset(is_dirty, 1, pages); return; }
  build_order(tmp, pages, PAT_RAND, seed ^ 0x9e3779b97f4a7c15ULL);
  for (uint32_t i = 0; i < dirty_pages; i++) is_dirty[tmp[i]] = 1;
  free(tmp);
}

static void print_csv_header(void) {
  printf("mode,pattern,region_mb,pages,dirty_frac,dirty_pages,passes,"
         "pass1_ms,pass1_mib_s,pass2_ms,pass2_mib_s,flush_ms,flush_mib_s\n");
}

static const char* mode_name(mode_t m) {
  switch (m) {
    case MODE_LOCAL: return "local";
    case MODE_RPC: return "rpc";
    case MODE_PAGER: return "pager";
  }
  return "unknown";
}

static const char* pat_name(pattern_t p) {
  return (p == PAT_SEQ) ? "seq" : "rand";
}

static double mib_per_s(uint64_t bytes, uint64_t ns) {
  if (ns == 0) return 0.0;
  double sec = (double)ns / 1e9;
  double mib = (double)bytes / (1024.0 * 1024.0);
  return mib / sec;
}

/* ---------------- LOCAL ---------------- */

static int run_local(const cfg_t* c,
                     const uint32_t* order,
                     const uint8_t* is_dirty,
                     uint32_t pages,
                     uint64_t region_bytes,
                     double* out_pass1_ms,
                     double* out_pass2_ms) {
  uint8_t* mem = (uint8_t*)aligned_alloc(PAGE_SZ, (size_t)region_bytes);
  if (!mem) return -1;
  memset(mem, 0, (size_t)region_bytes);

  volatile uint64_t sink = 0;

  // pass 1
  uint64_t t0 = ns_now();
  for (uint32_t k = 0; k < pages; k++) {
    uint32_t i = order[k];
    uint8_t* page = mem + (uint64_t)i * PAGE_SZ;
    // read
    sink += *(volatile uint64_t*)page;
    // write (1 byte) if dirty
    if (is_dirty[i]) page[0] ^= 0xA5;
  }
  uint64_t t1 = ns_now();
  *out_pass1_ms = (double)(t1 - t0) / 1e6;

  // extra passes (resident)
  double pass2_total_ms = 0.0;
  for (uint32_t pass = 2; pass <= c->passes; pass++) {
    uint64_t s0 = ns_now();
    for (uint32_t k = 0; k < pages; k++) {
      uint32_t i = order[k];
      uint8_t* page = mem + (uint64_t)i * PAGE_SZ;
      sink += *(volatile uint64_t*)page;
      if (is_dirty[i]) page[0] ^= 0x5A;
    }
    uint64_t s1 = ns_now();
    pass2_total_ms += (double)(s1 - s0) / 1e6;
  }
  *out_pass2_ms = pass2_total_ms;

  // keep sink used
  if (sink == 0xdeadbeefULL) fprintf(stderr, "sink\n");

  free(mem);
  return 0;
}

/* ---------------- RPC ---------------- */

static int run_rpc(const cfg_t* c,
                   const uint32_t* order,
                   const uint8_t* is_dirty,
                   uint32_t pages,
                   uint64_t region_bytes,
                   double* out_pass1_ms,
                   double* out_pass2_ms,
                   double* out_flush_ms,
                   uint32_t* out_dirty_pages) {
  netmem_t nm;
  if (netmem_connect(&nm, c->ip, c->port) != 0) return -1;
  if (netmem_alloc_region(&nm, region_bytes) != 0) { netmem_close(&nm); return -1; }

  // For rpc mode, "dirty pages" means we WRITE_PAGE (full 4KB) for those pages.
  uint32_t dirty_pages = 0;
  for (uint32_t i = 0; i < pages; i++) dirty_pages += is_dirty[i] ? 1u : 0u;
  *out_dirty_pages = dirty_pages;

  uint8_t buf[PAGE_SZ];
  memset(buf, 0, sizeof(buf));
  volatile uint64_t sink = 0;

  // pass 1: for each page, READ_PAGE (always), and if dirty -> WRITE_PAGE
  uint64_t t0 = ns_now();
  for (uint32_t k = 0; k < pages; k++) {
    uint32_t i = order[k];
    if (netmem_read_page(&nm, i, buf) != 0) { netmem_close(&nm); return -1; }
    sink += *(uint64_t*)buf;
    if (is_dirty[i]) {
      buf[0] ^= 0xA5;
      if (netmem_write_page(&nm, i, buf) != 0) { netmem_close(&nm); return -1; }
    }
  }
  uint64_t t1 = ns_now();
  *out_pass1_ms = (double)(t1 - t0) / 1e6;

  // additional passes (still RPC each time)
  double pass2_total_ms = 0.0;
  for (uint32_t pass = 2; pass <= c->passes; pass++) {
    uint64_t s0 = ns_now();
    for (uint32_t k = 0; k < pages; k++) {
      uint32_t i = order[k];
      if (netmem_read_page(&nm, i, buf) != 0) { netmem_close(&nm); return -1; }
      sink += *(uint64_t*)buf;
      if (is_dirty[i]) {
        buf[0] ^= 0x5A;
        if (netmem_write_page(&nm, i, buf) != 0) { netmem_close(&nm); return -1; }
      }
    }
    uint64_t s1 = ns_now();
    pass2_total_ms += (double)(s1 - s0) / 1e6;
  }
  *out_pass2_ms = pass2_total_ms;

  // flush is "N/A" for rpc mode since writes already happened. Still output 0.
  *out_flush_ms = 0.0;

  if (sink == 0xdeadbeefULL) fprintf(stderr, "sink\n");
  netmem_close(&nm);
  return 0;
}

/* ---------------- PAGER ---------------- */

static int run_pager(const cfg_t* c,
                     const uint32_t* order,
                     const uint8_t* is_dirty,
                     uint32_t pages,
                     uint64_t region_bytes,
                     double* out_pass1_ms,
                     double* out_pass2_ms,
                     double* out_flush_ms) {
  void* base = NULL;
  disagg_pager_t* p = disagg_open(c->ip, c->port, region_bytes, &base);
  if (!p || !base) return -1;

  uint8_t* mem = (uint8_t*)base;
  volatile uint64_t sink = 0;

  // pass 1: causes faults as needed
  uint64_t t0 = ns_now();
  for (uint32_t k = 0; k < pages; k++) {
    uint32_t i = order[k];
    uint8_t* page = mem + (uint64_t)i * PAGE_SZ;
    // read (fault if not resident)
    sink += *(volatile uint64_t*)page;
    // write 1 byte if dirty (may trigger write-protect fault)
    if (is_dirty[i]) page[0] ^= 0xA5;
  }
  uint64_t t1 = ns_now();
  *out_pass1_ms = (double)(t1 - t0) / 1e6;

  // additional passes: should be resident, so much faster
  double pass2_total_ms = 0.0;
  for (uint32_t pass = 2; pass <= c->passes; pass++) {
    uint64_t s0 = ns_now();
    for (uint32_t k = 0; k < pages; k++) {
      uint32_t i = order[k];
      uint8_t* page = mem + (uint64_t)i * PAGE_SZ;
      sink += *(volatile uint64_t*)page;
      if (is_dirty[i]) page[0] ^= 0x5A;
    }
    uint64_t s1 = ns_now();
    pass2_total_ms += (double)(s1 - s0) / 1e6;
  }
  *out_pass2_ms = pass2_total_ms;

  // flush dirty pages back to server (full 4KB each dirty page)
  uint64_t f0 = ns_now();
  if (disagg_flush(p) != 0) { disagg_close(p); return -1; }
  uint64_t f1 = ns_now();
  *out_flush_ms = (double)(f1 - f0) / 1e6;

  if (sink == 0xdeadbeefULL) fprintf(stderr, "sink\n");
  disagg_close(p);
  return 0;
}

/* ---------------- MAIN ---------------- */

int main(int argc, char** argv) {
  cfg_t c;
  if (parse_cfg(&c, argc, argv) != 0) {
    usage(argv[0]);
    return 1;
  }

  uint64_t region_bytes = c.region_mb * 1024ull * 1024ull;
  if (region_bytes < PAGE_SZ) region_bytes = PAGE_SZ;

  uint32_t pages = (uint32_t)(region_bytes / PAGE_SZ);
  region_bytes = (uint64_t)pages * PAGE_SZ; // clamp to whole pages

  uint32_t* order = (uint32_t*)malloc((size_t)pages * sizeof(uint32_t));
  uint8_t* is_dirty = (uint8_t*)malloc((size_t)pages);
  if (!order || !is_dirty) {
    fprintf(stderr, "alloc failed\n");
    return 1;
  }

  build_order(order, pages, c.pattern, c.seed);
  uint32_t dirty_pages_target = count_dirty_pages(pages, c.dirty);
  mark_dirty_set(is_dirty, pages, dirty_pages_target, c.seed);

  double pass1_ms = 0.0, pass2_ms = 0.0, flush_ms = 0.0;
  uint32_t dirty_pages_actual = 0;
  for (uint32_t i = 0; i < pages; i++) dirty_pages_actual += is_dirty[i] ? 1u : 0u;

  int rc = 0;
  if (c.mode == MODE_LOCAL) {
    rc = run_local(&c, order, is_dirty, pages, region_bytes, &pass1_ms, &pass2_ms);
    flush_ms = 0.0;
  } else if (c.mode == MODE_RPC) {
    rc = run_rpc(&c, order, is_dirty, pages, region_bytes, &pass1_ms, &pass2_ms, &flush_ms, &dirty_pages_actual);
  } else {
    rc = run_pager(&c, order, is_dirty, pages, region_bytes, &pass1_ms, &pass2_ms, &flush_ms);
  }

  if (rc != 0) {
    fprintf(stderr, "benchmark failed (mode=%s). errno=%d\n", mode_name(c.mode), errno);
    free(order);
    free(is_dirty);
    return 2;
  }

  // Throughput interpretation:
  // - pass1 bytes accessed: pages*4KB (read) + maybe writes. We report throughput as read-bytes / time.
  // - pass2 bytes accessed: (passes-1)*pages*4KB similarly.
  // - flush bytes: dirty_pages*4KB.
  uint64_t pass_bytes = (uint64_t)pages * PAGE_SZ;
  uint64_t pass2_bytes = (uint64_t)(c.passes > 1 ? (c.passes - 1) : 0) * pass_bytes;
  uint64_t flush_bytes = (uint64_t)dirty_pages_actual * PAGE_SZ;

  uint64_t pass1_ns = (uint64_t)(pass1_ms * 1e6) * 1000ull;
  uint64_t pass2_ns = (uint64_t)(pass2_ms * 1e6) * 1000ull;
  uint64_t flush_ns = (uint64_t)(flush_ms * 1e6) * 1000ull;

  if (c.header) print_csv_header();

  printf("%s,%s,%" PRIu64 ",%" PRIu32 ",%.3f,%" PRIu32 ",%" PRIu32 ","
         "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
         mode_name(c.mode),
         pat_name(c.pattern),
         c.region_mb,
         pages,
         c.dirty,
         dirty_pages_actual,
         c.passes,
         pass1_ms,
         mib_per_s(pass_bytes, pass1_ns),
         pass2_ms,
         (pass2_ns ? mib_per_s(pass2_bytes, pass2_ns) : 0.0),
         flush_ms,
         (flush_ns ? mib_per_s(flush_bytes, flush_ns) : 0.0));

  free(order);
  free(is_dirty);
  return 0;
}
