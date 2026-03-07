// ptrchase.c: pointer-chasing latency (outputs CSV)
// Usage: ./ptrchase <N> <iters>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

typedef struct Node {
  struct Node* next;
  uint64_t pad; // keep node non-trivial
} Node;

static inline uint64_t nsec_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void shuffle(size_t* a, size_t n) {
  for (size_t i = n - 1; i > 0; i--) {
    size_t j = (size_t) (rand() % (int)(i + 1));
    size_t tmp = a[i]; a[i] = a[j]; a[j] = tmp;
  }
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <N_nodes> <iters>\n", argv[0]);
    return 2;
  }
  size_t N = (size_t) strtoull(argv[1], NULL, 10);
  size_t iters = (size_t) strtoull(argv[2], NULL, 10);
  if (N < 2 || iters < 1) return 2;

  srand(1);

  Node* nodes = (Node*) malloc(N * sizeof(Node));
  if (!nodes) { perror("malloc"); return 1; }

  size_t* idx = (size_t*) malloc(N * sizeof(size_t));
  if (!idx) { perror("malloc idx"); return 1; }
  for (size_t i = 0; i < N; i++) idx[i] = i;
  shuffle(idx, N);

  // link in shuffled order (cycle)
  for (size_t i = 0; i < N; i++) {
    size_t cur = idx[i];
    size_t nxt = idx[(i + 1) % N];
    nodes[cur].next = &nodes[nxt];
    nodes[cur].pad = (uint64_t)cur;
  }

  Node* p = &nodes[idx[0]];
  volatile uint64_t checksum = 0;

  // warm up
  for (size_t i = 0; i < N; i++) { checksum += p->pad; p = p->next; }

  uint64_t t0 = nsec_now();
  size_t steps = 0;
  for (size_t r = 0; r < iters; r++) {
    for (size_t i = 0; i < N; i++) {
      checksum += p->pad;
      p = p->next;
      steps++;
    }
  }
  uint64_t t1 = nsec_now();

  double ns_total = (double)(t1 - t0);
  double ns_per_step = ns_total / (double)steps;

  // CSV: bench,name,N,iters,steps,ns_total,ns_per_op,checksum
  printf("bench,name,N,iters,steps,ns_total,ns_per_op,checksum\n");
  printf("ptrchase,ptrchase,%llu,%llu,%llu,%.0f,%.3f,%llu\n",
         (unsigned long long)N,
         (unsigned long long)iters,
         (unsigned long long)steps,
         ns_total, ns_per_step,
         (unsigned long long)checksum);

  free(idx);
  free(nodes);
  return 0;
}
