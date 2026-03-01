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

int main(void) {
    banner();

    size_t n = 8;
    char *buf = malloc(n);
    if (!buf) { perror("malloc"); return 1; }

#ifdef __CHERI_PURE_CAPABILITY__
    size_t cap_len = (size_t)cheri_getlen(buf);
    printf("[info] malloc requested=%zu bytes | capability_length=%zu bytes\n", n, cap_len);
#else
    printf("[info] malloc requested=%zu bytes\n", n);
#endif

    printf("[info] buf_ptr=%p\n", (void *)buf);

    memset(buf, 'A', n);
    printf("[step] init: memset first %zu bytes to 'A'\n", n);

    printf("[step] in-bounds write: indices 0..%zu set to 'B'\n", n - 1);
    for (size_t i = 0; i < n; i++) {
        buf[i] = 'B';
    }
    printf("[ok] in-bounds write completed\n");

    // Choose a definitely-out-of-bounds index for CHERI.
    // (Linux may still not crash — undefined behavior.)
    size_t bad = 1024;
    printf("[step] OOB write attempt: buf[%zu] = 'X'\n", bad);

    volatile char *v = buf;  // discourage compiler reordering/optimizations
    v[bad] = 'X';

    // If we get here, we didn't immediately trap. Demonstrate what we can observe.
    printf("[warn] no immediate trap after OOB write\n");

#ifndef __CHERI_PURE_CAPABILITY__
    // On Linux, show that we can *read back* the value we just wrote (still UB, but often “works”).
    volatile unsigned char readback = (unsigned char)v[bad];
    printf("[linux] readback after OOB: buf[%zu] == 0x%02x ('%c')\n",
           bad, (unsigned)readback, (readback >= 32 && readback <= 126) ? readback : '.');
#else
    // On CHERI, you typically won't reach here if bounds are enforced (you'll trap at the write).
    printf("[cheri] reached post-OOB logging (unexpected)\n");
#endif

    free(buf);
    printf("[done] freed buffer, exiting normally\n");
    return 0;
}