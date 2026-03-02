// netmem_page.h
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { NETMEM_PAGE_SZ = 4096 };

typedef struct {
  int fd;
  uint32_t region_id;
  uint64_t region_bytes;
  uint32_t num_pages;
} netmem_t;

int  netmem_connect(netmem_t* nm, const char* ip, int port);
int  netmem_alloc_region(netmem_t* nm, uint64_t region_bytes);
int  netmem_read_page(netmem_t* nm, uint32_t page_idx, void* out_4096);
int  netmem_write_page(netmem_t* nm, uint32_t page_idx, const void* in_4096);
void netmem_close(netmem_t* nm);

#ifdef __cplusplus
}
#endif
