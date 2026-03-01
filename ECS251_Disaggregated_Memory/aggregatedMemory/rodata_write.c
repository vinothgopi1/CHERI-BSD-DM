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

    const char *s = "HELLO";   // typically in read-only .rodata
    printf("[info] s_ptr=%p\n", (void*)s);

#ifdef __CHERI_PURE_CAPABILITY__
    printf("[info] s_cap_len=%zu\n", (size_t)cheri_getlen(s));
    printf("[info] s_perms=0x%jx\n", (uintmax_t)cheri_getperm(s));
#endif

    printf("[step] attempt write to read-only memory: ((char*)s)[0] = 'X'\n");
    volatile char *v = (volatile char *)s;
    v[0] = 'X';

    printf("[warn] no immediate trap after rodata write (unexpected)\n");
#ifndef __CHERI_PURE_CAPABILITY__
    printf("[linux] readback: s[0] == '%c' (unexpected; usually segfault)\n", (char)v[0]);
#else
    printf("[cheri] reached post-write logging (unexpected)\n");
#endif

    printf("[done] exiting normally\n");
    return 0;
}