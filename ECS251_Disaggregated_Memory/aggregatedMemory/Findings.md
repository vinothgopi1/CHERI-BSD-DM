# Aggregated Memory Test Files (Linux VM + CheriBSD Purecap)

This document summarizes the **aggregated-memory** safety experiments we ran to compare:

- **Conventional Linux execution (baseline pointers)** vs.
- **CheriBSD purecap execution (capabilities: bounds + permissions enforced by hardware)**

The goal was to demonstrate which memory safety properties CHERI enforces by default, which behaviors differ from Linux, and where additional mechanisms are needed (e.g., temporal safety / revocation).

---

## What we wanted to investigate

We focused on four core aspects of CHERI memory safety:

1. **Spatial memory safety (bounds enforcement)**
   - Out-of-bounds reads/writes should fault when a capability is dereferenced outside its bounds.

2. **Capability permissions (authority restrictions)**
   - Writes should fail when a capability does not include store permission (e.g., read-only data).

3. **Least privilege via pointer narrowing**
   - A callee should only be able to access the exact region authorized by the caller (bounded slice).

4. **Temporal safety (use-after-free)**
   - Whether CHERI alone prevents use-after-free, and what additional mechanisms (revocation/quarantine) are needed.

---

## How we set up execution of the files

### Step 1 — Create test files in the Linux VM
We authored C files in:
- `~/test_files/<test>.c`

### Step 2 — Linux baseline compile/run (inside the Linux VM)
```bash
gcc <test>.c -O2 -Wall -o <test>_linux
./<test>_linux
```


### Step 3 — CHERI purecap compile (inside the Linux VM using CheriBSD sysroot)

We compiled RISC-V CheriBSD **purecap** binaries using the CHERI SDK and the purecap sysroot:

```bash
SDK=~/cheri/output/sdk
SYSROOT=~/cheri/output/rootfs-riscv64-purecap

$SDK/bin/clang \
  --target=riscv64-unknown-freebsd \
  --sysroot="$SYSROOT" \
  -march=rv64imafdcxcheri -mabi=l64pc128d \
  -mno-relax \
  -O2 -Wall \
  ~/test_files/<test>.c -o ~/test_files/<test>_cheri_purecap
```

### Step 4 — Move the binary from the Linux VM into CheriBSD

We used `scp` to copy the compiled purecap binary into the running CheriBSD VM:

```bash
scp -P 9999 ~/test_files/<test>_cheri_purecap root@localhost:/root/
```

### Step 5 — Execute Inside CheriBSD

```bash
chmod +x /root/<test>_cheri_purecap
/root/<test>_cheri_purecap
```




## Results and Interpretation

### Heap Out-of-Bounds Write

#### What this test targets
- **Spatial safety on the heap**
- Whether a heap pointer’s **capability bounds** prevent writes beyond the allocated region.

#### What happened initially (why the first heap test didn’t trap)
Our first heap test wrote beyond the requested **8 bytes** (writing indices **8..15**). We initially saw **no trap** in both Linux and CheriBSD.

This is expected because `malloc(n)` often rounds allocations up to an allocator size class. On CHERI purecap, the capability bounds typically match the allocator chunk size, not the requested `n`.

We later confirmed this in CheriBSD:
- **Requested n = 8**
- **Capability length = 16**

So indices **8..15** were still within bounds.

#### Updated heap test result (large OOB index)
We updated the heap OOB write to target a much larger index (`buf[1024]`), which is far beyond any allocator rounding.

Observed behavior:
- **Linux:** program often continues (undefined behavior; may silently corrupt memory).
- **CheriBSD purecap:** traps with:
  - `In-address space security exception (core dumped)`

#### What it shows
- CHERI bounds are enforced **at dereference time** by hardware.
- Heap out-of-bounds access becomes an immediate, enforceable fault once it exceeds the true capability bounds.

---

### Stack Out-of-Bounds Write

#### What this test targets
- **Spatial safety on stack allocations**
- Stack objects are often bounded tightly, making this test very deterministic.

#### Expected behavior
- **Linux:** may or may not crash; often continues (undefined behavior).
- **CheriBSD purecap:** should trap when writing beyond bounds (e.g., `buf[20]` on `char buf[8]`).

#### What it shows
- CHERI bounds apply uniformly to stack memory.
- Many classic stack memory bugs become immediate hardware faults on CHERI.

---

### Use-after-free

#### What this test targets
- **Temporal safety:** using a pointer after the allocation has been freed.

#### Observed results
**Linux baseline**
- UAF write succeeded and readback showed the write:
  - `p[0] == 'X' (still UB)`

**CheriBSD purecap**
- UAF write also succeeded (no immediate trap):
  - logs reached: `[cheri] reached post-UAF logging (revocation may be off)`

#### Why this “no difference” is expected
This is the key nuance:
- CHERI provides strong **spatial safety** (bounds/permissions).
- Use-after-free is a **temporal** safety issue (“this object no longer exists”).
- CHERI does not automatically invalidate freed pointers unless an additional mechanism is enabled (e.g., **capability revocation**, quarantines, or allocator support).

So this result is still useful: it shows that CHERI by itself does not guarantee temporal safety by default.

#### How to interpret this in a report
CHERI reliably stops spatial bugs (out-of-bounds), but temporal bugs like use-after-free require revocation mechanisms that may not be enabled in a default configuration.

---

### Write to read-only memory

#### What this test targets
- **Capability permissions** and immutability enforcement (attempting to write to a string literal in `.rodata`).

#### Observed results
**Linux baseline**
- Crashed with:
  - `Segmentation fault (core dumped)`

**CheriBSD purecap**
- Crashed with:
  - `In-address space security exception (core dumped)`
- Also printed capability metadata:
  - `s_cap_len=6` (tight bound for `"HELLO\0"`)
  - `s_perms=...` (permissions do not allow store/write)

#### What it shows
- Linux prevents the write due to **page protections** (read-only mapping).
- CHERI prevents the write via **capability permission enforcement**, and CHERI exposes:
  - object-level bounds (`cap_len`)
  - capability permissions (`perms`)

Even though both crash, CHERI’s model is more fine-grained: the authority is carried by the pointer capability itself.

---

### Pointer narrowing / least-privilege slice

#### What this test targets
- **Least privilege for pointers**
- Shows a uniquely CHERI feature: bounding a pointer to a subrange so the callee cannot access outside that region even if it tries.

#### Setup
- Allocate a 64-byte buffer.
- Create a slice pointer: `slice = buf + 32`.
- On CHERI, bound it to 16 bytes using:
  - `slice = cheri_setbounds(slice, 16);`
- Call a buggy function that tries to write outside the slice:
  - `slice[-16] = '!'`

#### Observed results
**CheriBSD purecap**
- Printed:
  - `slice_cap_len=16` confirming the bounded slice.
- Trapped exactly when the buggy write occurred:
  - `In-address space security exception (core dumped)`

**Linux baseline**
- The buggy write and read succeeded:
  - `slice[-16] -> '!'`
- Program returned normally.

#### What it shows
- **Linux:** passing a “slice pointer” does not restrict authority; it’s still just an address.
- **CHERI:** the callee only receives the authority you explicitly grant (bounded capability).
- This is the strongest CHERI demo because it demonstrates hardware-enforced least privilege for pointer-based sharing.

---

## Summary of outcomes

### Strong, deterministic CHERI differences observed
- **Heap OOB** (when truly out-of-bounds): CHERI traps, Linux may not.
- **Stack OOB:** CHERI traps, Linux may not.
- **Pointer narrowing:** CHERI prevents “walking outside the slice” via bounded capabilities.
- **Read-only write:** both crash, but CHERI provides capability-level bounds + permission metadata and enforces via capabilities.

### No difference observed (expected under many default configs)
- **Use-after-free:** CHERI does not automatically provide temporal safety unless revocation/quarantine is enabled.