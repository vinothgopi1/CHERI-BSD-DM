#include <stdio.h>
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

    char buf[8];

#ifdef __CHERI_PURE_CAPABILITY__
    size_t cap_len = (size_t)cheri_getlen(buf);
    printf("[info] stack buffer: requested=8 bytes | capability_length=%zu bytes\n", cap_len);
#else
    printf("[info] stack buffer: requested=8 bytes\n");
#endif

    printf("[info] buf_ptr=%p\n", (void *)buf);

    // Initialize in-bounds
    printf("[step] init: set indices 0..7 to 'A'\n");
    for (int i = 0; i < 8; i++) buf[i] = 'A';
    printf("[ok] init completed\n");

    // In-bounds write
    printf("[step] in-bounds write: buf[0] = 'B'\n");
    buf[0] = 'B';
    printf("[ok] in-bounds write completed\n");

    // Out-of-bounds write attempt
    size_t bad = 20;
    printf("[step] OOB write attempt: buf[%zu] = 'X'\n", bad);

    volatile char *v = buf;   // discourage compiler cleverness/reordering
    v[bad] = 'X';

    // If we get here, no immediate trap happened
    printf("[warn] no immediate trap after OOB write\n");

#ifndef __CHERI_PURE_CAPABILITY__
    // On Linux, show readback (still undefined behavior, but often “works”)
    volatile unsigned char readback = (unsigned char)v[bad];
    printf("[linux] readback after OOB: buf[%zu] == 0x%02x ('%c')\n",
           bad, (unsigned)readback,
           (readback >= 32 && readback <= 126) ? readback : '.');
#else
    // On CHERI, you normally won't reach here if bounds are enforced
    printf("[cheri] reached post-OOB logging (unexpected)\n");
#endif

    printf("[done] exiting normally\n");
    return 0;
}