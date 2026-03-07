CHERI_SDK   ?= $(HOME)/cheri/output/sdk
CHERI_CLANG ?= $(CHERI_SDK)/bin/clang
CHERI_CFG   ?= $(CHERI_SDK)/bin/cheribsd-riscv64-purecap.cfg

CFLAGS := --config $(CHERI_CFG) -O2 -pipe -Wall -Wextra

all: ptrchase mallocbench rpcbench

ptrchase: ptrchase.c
	$(CHERI_CLANG) $(CFLAGS) -o $@ $<

mallocbench: mallocbench.c
	$(CHERI_CLANG) $(CFLAGS) -o $@ $<

rpcbench: rpcbench.c
	$(CHERI_CLANG) $(CFLAGS) -o $@ $<

clean:
	rm -f ptrchase mallocbench rpcbench *.o *.csv