// disagg_pager.h
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct disagg_pager disagg_pager_t;

// Map a "remote region" and return a pointer into an mmap'd range.
// Touching pages triggers SIGSEGV and fetches pages from the server.
disagg_pager_t* disagg_open(const char* ip, int port, uint64_t region_bytes, void** out_base);

// Optional: flush all dirty pages back to server
int  disagg_flush(disagg_pager_t* p);

// Unmap and close connection (flushes dirty pages)
int  disagg_close(disagg_pager_t* p);

#ifdef __cplusplus
}
#endif
