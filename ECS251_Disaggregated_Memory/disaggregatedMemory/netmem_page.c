// netmem_page.c
#define _POSIX_C_SOURCE 200809L
#include "netmem_page.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum { OP_ALLOC_REGION = 1, OP_READ_PAGE = 2, OP_WRITE_PAGE = 3 };

typedef struct {
  uint32_t op;
  uint32_t region;
  uint32_t page;
  uint32_t len;
} __attribute__((packed)) MsgHdr;

static ssize_t read_full(int fd, void* buf, size_t n) {
  uint8_t* p = (uint8_t*)buf;
  size_t got = 0;
  while (got < n) {
    ssize_t r = recv(fd, p + got, n - got, 0);
    if (r == 0) return 0;
    if (r < 0) { if (errno == EINTR) continue; return -1; }
    got += (size_t)r;
  }
  return (ssize_t)got;
}

static ssize_t write_full(int fd, const void* buf, size_t n) {
  const uint8_t* p = (const uint8_t*)buf;
  size_t sent = 0;
  while (sent < n) {
    ssize_t w = send(fd, p + sent, n - sent, 0);
    if (w <= 0) { if (w < 0 && errno == EINTR) continue; return -1; }
    sent += (size_t)w;
  }
  return (ssize_t)sent;
}

int netmem_connect(netmem_t* nm, const char* ip, int port) {
  memset(nm, 0, sizeof(*nm));
  nm->fd = socket(AF_INET, SOCK_STREAM, 0);
  if (nm->fd < 0) return -1;

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) return -1;

  if (connect(nm->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) return -1;
  return 0;
}

int netmem_alloc_region(netmem_t* nm, uint64_t region_bytes) {
  if (region_bytes == 0) region_bytes = NETMEM_PAGE_SZ;

  // For simplicity, put region_bytes in len (uint32_t). Keep region <= 4GB for class project.
  if (region_bytes > 0xFFFFFFFFu) return -1;

  MsgHdr h = { htonl(OP_ALLOC_REGION), htonl(0), htonl(0), htonl((uint32_t)region_bytes) };
  if (write_full(nm->fd, &h, sizeof(h)) < 0) return -1;

  MsgHdr rep;
  if (read_full(nm->fd, &rep, sizeof(rep)) <= 0) return -1;

  uint32_t op = ntohl(rep.op);
  uint32_t rid = ntohl(rep.region);
  if (op != OP_ALLOC_REGION || rid == 0) return -1;

  nm->region_id = rid;
  nm->region_bytes = region_bytes;
  nm->num_pages = (uint32_t)((region_bytes + NETMEM_PAGE_SZ - 1) / NETMEM_PAGE_SZ);
  return 0;
}

int netmem_read_page(netmem_t* nm, uint32_t page_idx, void* out_4096) {
  if (!nm || nm->fd <= 0 || nm->region_id == 0) return -1;
  if (page_idx >= nm->num_pages) return -1;

  MsgHdr h = { htonl(OP_READ_PAGE), htonl(nm->region_id), htonl(page_idx), htonl(NETMEM_PAGE_SZ) };
  if (write_full(nm->fd, &h, sizeof(h)) < 0) return -1;

  MsgHdr rep;
  if (read_full(nm->fd, &rep, sizeof(rep)) <= 0) return -1;

  uint32_t op = ntohl(rep.op);
  uint32_t rid = ntohl(rep.region);
  uint32_t pg  = ntohl(rep.page);
  uint32_t len = ntohl(rep.len);

  if (op != OP_READ_PAGE || rid != nm->region_id || pg != page_idx || len != NETMEM_PAGE_SZ) return -1;
  if (read_full(nm->fd, out_4096, NETMEM_PAGE_SZ) <= 0) return -1;
  return 0;
}

int netmem_write_page(netmem_t* nm, uint32_t page_idx, const void* in_4096) {
  if (!nm || nm->fd <= 0 || nm->region_id == 0) return -1;
  if (page_idx >= nm->num_pages) return -1;

  MsgHdr h = { htonl(OP_WRITE_PAGE), htonl(nm->region_id), htonl(page_idx), htonl(NETMEM_PAGE_SZ) };
  if (write_full(nm->fd, &h, sizeof(h)) < 0) return -1;
  if (write_full(nm->fd, in_4096, NETMEM_PAGE_SZ) < 0) return -1;

  MsgHdr rep;
  if (read_full(nm->fd, &rep, sizeof(rep)) <= 0) return -1;

  uint32_t op = ntohl(rep.op);
  uint32_t rid = ntohl(rep.region);
  uint32_t pg  = ntohl(rep.page);
  if (op != OP_WRITE_PAGE || rid != nm->region_id || pg != page_idx) return -1;
  return 0;
}

void netmem_close(netmem_t* nm) {
  if (!nm) return;
  if (nm->fd > 0) close(nm->fd);
  nm->fd = -1;
  nm->region_id = 0;
}
