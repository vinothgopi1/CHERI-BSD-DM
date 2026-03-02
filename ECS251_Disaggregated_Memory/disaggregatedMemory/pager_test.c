// pager_test.c
#define _POSIX_C_SOURCE 200809L
#include "disagg_pager.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t ns_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int main(int argc, char** argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: %s <server_ip> <port> <region_mb>\n", argv[0]);
    return 1;
  }
  const char* ip = argv[1];
  int port = atoi(argv[2]);
  uint64_t region_mb = (uint64_t)strtoull(argv[3], NULL, 10);
  uint64_t region_bytes = region_mb * 1024ull * 1024ull;

  void* base = NULL;
  disagg_pager_t* p = disagg_open(ip, port, region_bytes, &base);
  if (!p) {
    fprintf(stderr, "disagg_open failed\n");
    return 1;
  }

  // Touch one byte per page sequentially (forces faults + READ_PAGE)
  uint8_t* mem = (uint8_t*)base;
  uint64_t page_sz = 4096;
  uint64_t pages = region_bytes / page_sz;

  uint64_t t0 = ns_now();
  for (uint64_t i = 0; i < pages; i++) {
    mem[i * page_sz] = (uint8_t)(i & 0xFF); // first read fault then write fault per page
  }
  uint64_t t1 = ns_now();

  // Flush dirty pages (WRITE_PAGE for each page)
  uint64_t t2 = ns_now();
  if (disagg_flush(p) != 0) {
    fprintf(stderr, "flush failed\n");
  }
  uint64_t t3 = ns_now();

  printf("Touched %" PRIu64 " pages (%.2f MiB)\n", pages, (double)region_bytes / (1024.0 * 1024.0));
  printf("Fault+fill+first-write time: %.3f ms\n", (double)(t1 - t0) / 1e6);
  printf("Flush(writeback) time:       %.3f ms\n", (double)(t3 - t2) / 1e6);

  disagg_close(p);
  return 0;
}
