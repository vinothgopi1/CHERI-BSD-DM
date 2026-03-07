// mallocbench.c: malloc/free throughput by size (outputs CSV)
// Usage: ./mallocbench <size_bytes> <iters>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

static inline uint64_t nsec_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <size_bytes> <iters>\n", argv[0]);
    return 2;
  }
  size_t sz = (size_t) strtoull(argv[1], NULL, 10);
  size_t iters = (size_t) strtoull(argv[2], NULL, 10);
  if (sz < 1 || iters < 1) return 2;

  void** ptrs = (void**) calloc(iters, sizeof(void*));
  if (!ptrs) { perror("calloc"); return 1; }

  // malloc phase
  uint64_t t0 = nsec_now();
  for (size_t i = 0; i < iters; i++) {
    ptrs[i] = malloc(sz);
    if (!ptrs[i]) { perror("malloc"); return 1; }
    // touch first byte so it’s committed
    ((volatile unsigned char*)ptrs[i])[0] = (unsigned char)i;
  }
  uint64_t t1 = nsec_now();

  // free phase
  uint64_t t2 = nsec_now();
  for (size_t i = 0; i < iters; i++) {
    free(ptrs[i]);
  }
  uint64_t t3 = nsec_now();

  double malloc_ns = (double)(t1 - t0);
  double free_ns   = (double)(t3 - t2);

  printf("bench,name,size,iters,ns_total,ns_per_op\n");
  printf("mallocbench,malloc,%llu,%llu,%.0f,%.3f\n",
         (unsigned long long)sz,
         (unsigned long long)iters,
         malloc_ns, malloc_ns/(double)iters);
  printf("mallocbench,free,%llu,%llu,%.0f,%.3f\n",
         (unsigned long long)sz,
         (unsigned long long)iters,
         free_ns, free_ns/(double)iters);

  free(ptrs);
  return 0;
}
