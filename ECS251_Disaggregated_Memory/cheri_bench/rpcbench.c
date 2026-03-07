// rpcbench.c: simple TCP RPC read/write benchmark
// Server: ./rpcbench server <port>
// Client: ./rpcbench client <host> <port> <len_bytes> <iters> <batch>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

static inline uint64_t nsec_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int read_full(int fd, void* buf, size_t n) {
  uint8_t* p = (uint8_t*)buf;
  size_t got = 0;
  while (got < n) {
    ssize_t r = read(fd, p + got, n - got);
    if (r == 0) return -1;
    if (r < 0) { if (errno == EINTR) continue; return -1; }
    got += (size_t)r;
  }
  return 0;
}

static int write_full(int fd, const void* buf, size_t n) {
  const uint8_t* p = (const uint8_t*)buf;
  size_t sent = 0;
  while (sent < n) {
    ssize_t w = write(fd, p + sent, n - sent);
    if (w < 0) { if (errno == EINTR) continue; return -1; }
    sent += (size_t)w;
  }
  return 0;
}

static int run_server(uint16_t port) {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) { perror("socket"); return 1; }

  int yes = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
  if (listen(s, 1) < 0) { perror("listen"); return 1; }

  fprintf(stderr, "rpcbench server listening on %u\n", (unsigned)port);

  for (;;) {
    int c = accept(s, NULL, NULL);
    if (c < 0) { perror("accept"); continue; }

    // Protocol: client sends uint32 len, uint32 batch; then for each request: uint8 op, payload[len] (for write)
    // Server replies with 1 byte ack per request, and echoes payload[len] back for read.
    for (;;) {
      uint32_t len = 0, batch = 0;
      if (read_full(c, &len, 4) < 0) break;
      if (read_full(c, &batch, 4) < 0) break;
      len = ntohl(len);
      batch = ntohl(batch);
      if (len == 0 || batch == 0 || len > (1024u * 1024u)) break;

      uint8_t* buf = (uint8_t*)malloc(len);
      if (!buf) break;

      for (uint32_t i = 0; i < batch; i++) {
        uint8_t op;
        if (read_full(c, &op, 1) < 0) { free(buf); goto done; }
        if (op == (uint8_t)'W') {
          if (read_full(c, buf, len) < 0) { free(buf); goto done; }
          uint8_t ack = 'A';
          if (write_full(c, &ack, 1) < 0) { free(buf); goto done; }
        } else if (op == (uint8_t)'R') {
          // reply with deterministic bytes
          for (uint32_t k = 0; k < len; k++) buf[k] = (uint8_t)(k & 0xff);
          uint8_t ack = 'A';
          if (write_full(c, &ack, 1) < 0) { free(buf); goto done; }
          if (write_full(c, buf, len) < 0) { free(buf); goto done; }
        } else {
          free(buf);
          goto done;
        }
      }

      free(buf);
    }
done:
    close(c);
  }
}

static int connect_tcp(const char* host, uint16_t port) {
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  char portstr[16];
  snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);

  if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return -1;
  int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (s < 0) return -1;
  if (connect(s, res->ai_addr, res->ai_addrlen) < 0) { close(s); return -1; }
  freeaddrinfo(res);
  return s;
}

static int run_client(const char* host, uint16_t port, uint32_t len, uint32_t iters, uint32_t batch) {
  int s = connect_tcp(host, port);
  if (s < 0) { perror("connect"); return 1; }

  uint8_t* payload = (uint8_t*)malloc(len);
  if (!payload) { perror("malloc"); close(s); return 1; }
  for (uint32_t i = 0; i < len; i++) payload[i] = (uint8_t)(i * 131u);

  // warmup (one batch)
  {
    uint32_t nlen = htonl(len), nb = htonl(batch);
    (void)write_full(s, &nlen, 4);
    (void)write_full(s, &nb, 4);
    for (uint32_t i = 0; i < batch; i++) {
      uint8_t op = 'W';
      (void)write_full(s, &op, 1);
      (void)write_full(s, payload, len);
      uint8_t ack;
      (void)read_full(s, &ack, 1);
    }
  }

  uint64_t t0 = nsec_now();
  uint64_t ops = 0;
  int ok = 1;

  for (uint32_t it = 0; it < iters && ok; it++) {
    uint32_t nlen = htonl(len), nb = htonl(batch);
    if (write_full(s, &nlen, 4) < 0) { ok = 0; break; }
    if (write_full(s, &nb, 4) < 0)   { ok = 0; break; }

    for (uint32_t i = 0; i < batch; i++) {
      uint8_t op = 'W'; // change to 'R' if you want read benchmark
      if (write_full(s, &op, 1) < 0) { ok = 0; break; }
      if (write_full(s, payload, len) < 0) { ok = 0; break; }
      uint8_t ack;
      if (read_full(s, &ack, 1) < 0) { ok = 0; break; }
      ops++;
    }
  }

  uint64_t t1 = nsec_now();
  double ns_total = (double)(t1 - t0);
  double ns_per_op = (ops > 0) ? (ns_total / (double)ops) : 0.0;
  double mib = ((double)len * (double)ops) / (1024.0 * 1024.0);
  double sec = ns_total / 1e9;
  double mibps = (sec > 0.0) ? (mib / sec) : 0.0;

  printf("bench,name,host,port,len,iters,batch,ops,ns_total,ns_per_op,MiB,MiB_per_s\n");
  printf("rpcbench,write,%s,%u,%u,%u,%u,%llu,%.0f,%.3f,%.3f,%.3f\n",
         host, (unsigned)port, (unsigned)len, (unsigned)iters, (unsigned)batch,
         (unsigned long long)ops, ns_total, ns_per_op, mib, mibps);

  free(payload);
  close(s);
  return ok ? 0 : 1;
}
int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage:\n");
    fprintf(stderr, "  %s server <port>\n", argv[0]);
    fprintf(stderr, "  %s client <host> <port> <len_bytes> <iters> <batch>\n", argv[0]);
    return 2;
  }
  if (strcmp(argv[1], "server") == 0) {
    if (argc < 3) return 2;
    uint16_t port = (uint16_t)strtoul(argv[2], NULL, 10);
    return run_server(port);
  } else if (strcmp(argv[1], "client") == 0) {
    if (argc < 7) return 2;
    const char* host = argv[2];
    uint16_t port = (uint16_t)strtoul(argv[3], NULL, 10);
    uint32_t len = (uint32_t)strtoul(argv[4], NULL, 10);
    uint32_t iters = (uint32_t)strtoul(argv[5], NULL, 10);
    uint32_t batch = (uint32_t)strtoul(argv[6], NULL, 10);
    return run_client(host, port, len, iters, batch);
  } else {
    return 2;
  }
}
