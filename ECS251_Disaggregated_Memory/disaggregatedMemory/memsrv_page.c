// memsrv_page.c
// Remote page store server: ALLOC_REGION, READ_PAGE, WRITE_PAGE
// Protocol: fixed-size header (network byte order)
//   struct MsgHdr { uint32_t op, region, page, len; }
// ops:
//   1 = ALLOC_REGION: client sends hdr(op=1, len=region_bytes). server replies hdr(op=1, region=new_id)
//   2 = READ_PAGE:    client sends hdr(op=2, region, page, len=4096). server replies hdr(op=2, ... len=4096) + 4096 bytes
//   3 = WRITE_PAGE:   client sends hdr(op=3, region, page, len=4096) + 4096 bytes. server replies hdr(op=3, ... len=0)

#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum { OP_ALLOC_REGION = 1, OP_READ_PAGE = 2, OP_WRITE_PAGE = 3 };
enum { PAGE_SZ = 4096 };

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
    if (r == 0) return 0; // peer closed
    if (r < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    got += (size_t)r;
  }
  return (ssize_t)got;
}

static ssize_t write_full(int fd, const void* buf, size_t n) {
  const uint8_t* p = (const uint8_t*)buf;
  size_t sent = 0;
  while (sent < n) {
    ssize_t w = send(fd, p + sent, n - sent, 0);
    if (w <= 0) {
      if (w < 0 && errno == EINTR) continue;
      return -1;
    }
    sent += (size_t)w;
  }
  return (ssize_t)sent;
}

typedef struct PageNode {
  uint32_t page_idx;
  uint8_t  data[PAGE_SZ];
  struct PageNode* next;
} PageNode;

typedef struct Region {
  uint32_t id;
  uint64_t bytes;      // requested size
  uint32_t num_pages;  // ceil(bytes/PAGE_SZ)
  PageNode* pages;     // sparse linked list (fine for class project)
  struct Region* next;
} Region;

static Region* g_regions = NULL;
static uint32_t g_next_region = 1;

static Region* find_region(uint32_t id) {
  for (Region* r = g_regions; r; r = r->next) {
    if (r->id == id) return r;
  }
  return NULL;
}

static PageNode* find_page(Region* r, uint32_t page_idx) {
  for (PageNode* p = r->pages; p; p = p->next) {
    if (p->page_idx == page_idx) return p;
  }
  return NULL;
}

static PageNode* get_or_create_page(Region* r, uint32_t page_idx) {
  PageNode* p = find_page(r, page_idx);
  if (p) return p;
  p = (PageNode*)calloc(1, sizeof(PageNode));
  if (!p) return NULL;
  p->page_idx = page_idx;
  p->next = r->pages;
  r->pages = p;
  return p;
}

static void send_hdr(int cfd, uint32_t op, uint32_t region, uint32_t page, uint32_t len) {
  MsgHdr h = { htonl(op), htonl(region), htonl(page), htonl(len) };
  (void)write_full(cfd, &h, sizeof(h));
}

static void handle_client(int cfd) {
  for (;;) {
    MsgHdr net;
    ssize_t r = read_full(cfd, &net, sizeof(net));
    if (r == 0) return;
    if (r < 0) { perror("read hdr"); return; }

    uint32_t op     = ntohl(net.op);
    uint32_t region = ntohl(net.region);
    uint32_t page   = ntohl(net.page);
    uint32_t len    = ntohl(net.len);

    if (op == OP_ALLOC_REGION) {
      uint64_t bytes = (uint64_t)len;
      if (bytes == 0) bytes = (uint64_t)PAGE_SZ;

      Region* reg = (Region*)calloc(1, sizeof(Region));
      if (!reg) { send_hdr(cfd, op, 0, 0, 0); continue; }

      reg->id = g_next_region++;
      reg->bytes = bytes;
      reg->num_pages = (uint32_t)((bytes + PAGE_SZ - 1) / PAGE_SZ);
      reg->pages = NULL;
      reg->next = g_regions;
      g_regions = reg;

      send_hdr(cfd, OP_ALLOC_REGION, reg->id, 0, 0);
      continue;
    }

    Region* reg = find_region(region);
    if (!reg) {
      // unknown region: reply with len=0 as error
      send_hdr(cfd, op, 0, 0, 0);
      // If write had payload, drain it to resync
      if (op == OP_WRITE_PAGE && len > 0) {
        uint8_t tmp[512];
        uint32_t remain = len;
        while (remain) {
          uint32_t chunk = remain > sizeof(tmp) ? (uint32_t)sizeof(tmp) : remain;
          if (read_full(cfd, tmp, chunk) <= 0) break;
          remain -= chunk;
        }
      }
      continue;
    }

    if (page >= reg->num_pages) {
      send_hdr(cfd, op, 0, 0, 0);
      if (op == OP_WRITE_PAGE && len > 0) {
        uint8_t tmp[512];
        uint32_t remain = len;
        while (remain) {
          uint32_t chunk = remain > sizeof(tmp) ? (uint32_t)sizeof(tmp) : remain;
          if (read_full(cfd, tmp, chunk) <= 0) break;
          remain -= chunk;
        }
      }
      continue;
    }

    if (op == OP_READ_PAGE) {
      if (len != PAGE_SZ) { send_hdr(cfd, op, 0, 0, 0); continue; }
      PageNode* p = find_page(reg, page);
      send_hdr(cfd, OP_READ_PAGE, reg->id, page, PAGE_SZ);
      if (p) (void)write_full(cfd, p->data, PAGE_SZ);
      else {
        uint8_t zeros[PAGE_SZ]; memset(zeros, 0, sizeof(zeros));
        (void)write_full(cfd, zeros, PAGE_SZ);
      }
      continue;
    }

    if (op == OP_WRITE_PAGE) {
      if (len != PAGE_SZ) {
        send_hdr(cfd, op, 0, 0, 0);
        // drain payload
        uint8_t tmp[512];
        uint32_t remain = len;
        while (remain) {
          uint32_t chunk = remain > sizeof(tmp) ? (uint32_t)sizeof(tmp) : remain;
          if (read_full(cfd, tmp, chunk) <= 0) break;
          remain -= chunk;
        }
        continue;
      }
      PageNode* p = get_or_create_page(reg, page);
      if (!p) { send_hdr(cfd, op, 0, 0, 0); continue; }
      if (read_full(cfd, p->data, PAGE_SZ) <= 0) return;
      send_hdr(cfd, OP_WRITE_PAGE, reg->id, page, 0);
      continue;
    }

    // Unknown op: ignore / error
    send_hdr(cfd, op, 0, 0, 0);
  }
}

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    return 1;
  }
  int port = atoi(argv[1]);
  int sfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sfd < 0) { perror("socket"); return 1; }

  int opt = 1;
  setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons((uint16_t)port);

  if (bind(sfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
  if (listen(sfd, 16) < 0) { perror("listen"); return 1; }

  printf("memsrv_page listening on %d\n", port);

  for (;;) {
    int cfd = accept(sfd, NULL, NULL);
    if (cfd < 0) { if (errno == EINTR) continue; perror("accept"); break; }
    handle_client(cfd);
    close(cfd);
  }

  close(sfd);
  return 0;
}
