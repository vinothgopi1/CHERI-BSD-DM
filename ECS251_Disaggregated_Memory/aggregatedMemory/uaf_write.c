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

    size_t n = 32;
    char *p = malloc(n);
    if (!p) { perror("malloc"); return 1; }

#ifdef __CHERI_PURE_CAPABILITY__
    printf("[info] malloc requested=%zu | cap_len=%zu\n", n, (size_t)cheri_getlen(p));
#else
    printf("[info] malloc requested=%zu\n", n);
#endif
    printf("[info] p=%p\n", (void*)p);

    printf("[step] init: fill with 'A'\n");
    memset(p, 'A', n);
    p[n - 1] = '\0';
    printf("[ok] init done\n");

    printf("[step] free(p)\n");
    free(p);
    printf("[ok] free completed\n");

    printf("[step] UAF write attempt: p[0] = 'X' (use-after-free)\n");
    volatile char *v = p;
    v[0] = 'X';

    printf("[warn] no immediate trap after UAF write\n");
#ifndef __CHERI_PURE_CAPABILITY__
    printf("[linux] readback after UAF: p[0] == '%c' (still UB)\n", v[0]);
#else
    printf("[cheri] reached post-UAF logging (revocation may be off)\n");
#endif

    printf("[done] exiting normally\n");
    return 0;
}