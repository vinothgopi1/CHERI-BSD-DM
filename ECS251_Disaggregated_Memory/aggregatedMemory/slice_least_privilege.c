// slice_least_privilege.c
// Demonstrates CHERI "pointer narrowing" (least privilege) by bounding a slice capability.
// Linux: slice pointer is not bounded, so buggy_worker can read/write outside intended region.
// CHERI: slice is bounded with cheri_setbounds(), so out-of-slice access should trap.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef __CHERI_PURE_CAPABILITY__
#include <cheri/cheric.h>
#endif

static void banner(void) {
#ifdef __CHERI_PURE_CAPABILITY__
    printf("=== CHERI execution ===\n");
#else
    printf("=== LINUX execution ===\n");
#endif
}

static void buggy_worker(char *slice, size_t slice_len) {
    printf("[worker] entered buggy_worker(slice=%p, slice_len=%zu)\n", (void *)slice, slice_len);

#ifdef __CHERI_PURE_CAPABILITY__
    printf("[worker] slice_cap_len=%zu\n", (size_t)cheri_getlen(slice));
#endif

    printf("[worker] intended write inside slice: slice[0] = 'W'\n");
    slice[0] = 'W';
    printf("[worker] ok: wrote inside slice\n");

    // Bug: tries to write BEFORE the slice (should be disallowed in CHERI if bounded)
    printf("[worker] BUG: attempt write before slice: slice[-16] = '!' (should fault on CHERI)\n");
    volatile char *v = slice;
    v[-16] = '!';

    // If we get here, the OOB access did not immediately trap
    printf("[worker] BUG: attempt read before slice: slice[-16] -> '%c'\n", v[-16]);
    printf("[worker] finished (if you see this on CHERI, slice wasn’t bounded)\n");
}

int main(void) {
    banner();

    size_t total = 64;
    char *buf = malloc(total);
    if (!buf) { perror("malloc"); return 1; }
    memset(buf, '.', total);

    // Put a "secret" in first 16 bytes
    memcpy(buf, "SECRET_SECRET____", 16);

    printf("[info] buf=%p total=%zu\n", (void *)buf, total);
#ifdef __CHERI_PURE_CAPABILITY__
    printf("[info] buf_cap_len=%zu\n", (size_t)cheri_getlen(buf));
#endif

    size_t offset = 32;
    size_t slice_len = 16;
    char *slice = buf + offset;

#ifdef __CHERI_PURE_CAPABILITY__
    // Narrow authority to ONLY [buf+offset, buf+offset+slice_len)
    slice = (char *)cheri_setbounds(slice, slice_len);
    printf("[info] bounded slice created: offset=%zu len=%zu\n", offset, slice_len);
    printf("[info] slice_ptr=%p slice_cap_len=%zu\n", (void *)slice, (size_t)cheri_getlen(slice));
#else
    printf("[info] slice pointer (NOT bounded on Linux): offset=%zu len=%zu\n", offset, slice_len);
    printf("[info] slice_ptr=%p\n", (void *)slice);
#endif

    printf("[step] calling buggy_worker(slice)\n");
    buggy_worker(slice, slice_len);

    // If we returned, the buggy access didn't trap (Linux: expected; CHERI: unexpected)
    printf("[warn] returned from buggy_worker\n");

#ifndef __CHERI_PURE_CAPABILITY__
    // Show whether the "secret" likely got corrupted by the buggy write
    printf("[linux] post-worker: first 16 bytes now: '%.16s'\n", buf);
#endif

    free(buf);
    printf("[done] exiting normally\n");
    return 0;
}