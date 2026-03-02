# Makefile
CC ?= cc
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Wpedantic

all: memsrv_page pager_test

memsrv_page: memsrv_page.c
	$(CC) $(CFLAGS) -o $@ $<

pager_test: pager_test.c disagg_pager.c netmem_page.c
	$(CC) $(CFLAGS) -o $@ pager_test.c disagg_pager.c netmem_page.c

clean:
	rm -f memsrv_page pager_test
