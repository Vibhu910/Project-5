ARCH     := $(shell uname -m | sed 's/x86_64/x86/' | sed 's/aarch64/arm64/')
CLANG    := clang
BPFTOOL  := bpftool
CC       := gcc

BPF_CFLAGS   := -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) \
                -I/usr/include/$(shell uname -m)-linux-gnu -I.
USER_CFLAGS  := -g -O2 -Wall
USER_LDFLAGS := -lbpf -lelf -lz

VMLINUX_H := vmlinux.h

.PHONY: all clean help

all: scheduler_deep_tracer

$(VMLINUX_H):
	@echo "[*] Generating $(VMLINUX_H) from running kernel BTF..."
	@ls /sys/kernel/btf/vmlinux > /dev/null 2>&1 || \
	    (echo "ERROR: /sys/kernel/btf/vmlinux not found — kernel BTF not enabled" && exit 1)
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@
	@echo "[+] $(VMLINUX_H) ready ($(shell wc -l < $(VMLINUX_H)) lines)"

scheduler_deep_tracer.bpf.o: scheduler_deep_tracer.bpf.c $(VMLINUX_H)
	@echo "[*] Compiling scheduler BPF program..."
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@
	@echo "[+] scheduler BPF object ready"

scheduler_deep_tracer.skel.h: scheduler_deep_tracer.bpf.o
	@echo "[*] Generating scheduler skeleton header..."
	$(BPFTOOL) gen skeleton $< > $@
	@echo "[+] scheduler skeleton ready"

scheduler_deep_tracer: scheduler_deep_tracer.c scheduler_deep_tracer.skel.h
	@echo "[*] Compiling scheduler user-space loader..."
	$(CC) $(USER_CFLAGS) $< $(USER_LDFLAGS) -o $@
	@echo "[+] scheduler_deep_tracer binary ready"
	@echo "[✓] Scheduler tracer built — run with: sudo ./scheduler_deep_tracer [duration_seconds]"

clean:
	@echo "[*] Cleaning scheduler tracer artifacts..."
	rm -f scheduler_deep_tracer.bpf.o scheduler_deep_tracer.skel.h \
	      scheduler_deep_tracer
	@echo "[*] Removing vmlinux.h..."
	rm -f $(VMLINUX_H)
	@echo "[+] All clean"

