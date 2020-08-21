# Kernel Address Sanitizer

[Address Sanitizer (ASAN)][address-sanitizer] is a dynamic (run-time) sanitizer
that checks for certain invalid memory accesses in C++ code - for example,
buffer overruns, use-after-free, (stack) use-after-return/scope, and use of
uninitialized globals. The sanitizer adds compiler-generated instrumentation
before every data memory access and the runtime checks the validity of each
access before it proceeds.

ASAN works by constructing a 'shadow map', a map with one byte per 8 bytes of
kernel address space. Each shadow map byte tracks the validity of the kernel
address it corresponds to - zero represents 'valid for access', non-zero bytes
represent various sub-byte tracking or invalid states.

Allocators (for example the PMM) can invoke `asan_poison_shadow()` to mark
regions of memory in the physmap as "poisoned", disallowing any data accesses to
the region. They can also invoke `asan_unpoison_shadow()` to mark regions of
memory in the physmap as "unpoisoned", allowing any data accesses to the region.

## KASAN Concepts:
 * Poisoned Memory: kasan allows memory to be marked as either poisoned or
   unpoisoned. Memory accesses to poisoned memory result in kernel panics.
   Poisoning could be used in memory allocators to mark memory boundaries and to
   detect use-after-frees.

 * Redzone: memory allocators could add a small buffer before/after their
   allocations and poison it to detect buffer overflows. These buffers are
   called redzones.

 * Quarantine: Given that only memory accesses are checked, and memory can be
   reused (and thus, unpoisoned), increasing the time a memory region is
   poisoned allows more bugs to be detected. KASAN provides a way for allocators
   to hold off memory reuse, called quarantine. Instead of freeing memory right
   away, allocators can push pointers to a queue and free them in FIFO order.

Kernel ASAN is similar to userspace ASAN but has unique bootstrap and memory
allocation requirements.

Note that any function that performs memory accesses outside of the kernel
virtual address space has to be annotated with NO_ASAN, otherwise those
accesses will result in a system crash.

# Implementation

## Early Boot Setup

When kASAN is enabled, all compiled kernel code is instrumented; so we need a
valid shadow map very early in boot, before C code is called.  Currently the
x86-64 kernel has 512 GB of virtual addres space; KASAN requires 64 GB of shadow
memory to track this entire region, corresponding to 1 byte per 8 bytes.

We create a shadow map at [-128GB ; -64GB) to cover all kernel virtual address
space, and point every page of the shadow map to a single read-only zero page.
One page table and one page directory are reused for all entries in the MMU, to
save memory.

The shadow map is placed at [-128GB, -64GB) to avoid any potential mappings at
the highest parts of the kernel address space. Currently the reallocated kernel
is present there and it is foreseeable that other structures may be placed there
for convenient access.

The map in x86_64 looks like this:

* 64 entries in pdp_hi (1GB each) point all to the same page directory
  (kasan_shadow_tables[512..1023]), with RW, NX and global permissions.

* The kasan page directory has 512 entries pointing all to the same page table
  (kasan_shadow_tables), with RW, NX and global permissions.

* The kasan page table has 512 entries pointing all to the same zero page
  (kasan_zero_page), with RO, NX and global permissions.

With this structure, all poison checks inside the kernel address space will
succeed, as all shadow map memory is marked as unpoisoned.

## Late Boot Setup

In order to allow memory poisoning / tracking validity of kernel memory, asan
needs to have writable pages backing portions of the shadow that cover the
kernel physical map. These writable pages replace zero page mappings in parts
of the shadow map that asan instruments.

During late boot, after PMM is initialized, we allocate a shadow page for every
8 pages of address space to instrument that contain at least one page of real memory. 
We do not consider MMIO regions, device memory, the ISA hole, etc. as real memory.
We then replace the early boot zero page mappings with mappings to the newly
allocated shadow pages. All the remaining early boot mappings remain the same.

We register the entire physmap and the kernel data/rodata/bss (sections with
global variables) for instrumentation with asan during boot.

## Runtime

### Poisoning

This version of asan exposes an interface for callers to poison and check the
validity of memory via the following functions:

* `asan_poison_shadow`
* `asan_unpoison_shadow`
* `asan_region_is_poisoned`
* `asan_address_is_poisoned`

Memory allocators should use `asan_poison_shadow` to mark regions of memory as
invalid, specifying different poison values for different types of memory.
Allocators can use `asan_unpoison_shadow` to mark regions of memory as valid
for accesses.

### Kernel heap

The kernel heap is instrumented to poison metadata and free memory and unpoison
allocations.

The kernel heap adds a 'right-side redzone' after every allocation and poisons
it, to detect accesses past the end of a buffer. Heap metadata (before each
allocation) is also poisoned and serves as a 'left-side redzone'.

After an allocation is freed, it is kept in a 'quarantine' to delay its reuse.
This improves detection of use-after-free errors.

The quarantine is implemented as a free-running circular queue, which stores
up to kQuarantineElements pointers and frees them in FIFO order.

In `free`, the memory to be deallocated is added to the quarantine, and
once the queue is full, the oldest element is actually freed.

TODO(fxb/30033): kQuarantineElements is 65,536; this means that in the worst
case it increases kernel heap memory usage by 256 MB (4K * 65536), which is the
same as compiler-rt's default quarantine size. We could consider dynamically
tuning this or having the quarantine not release memory until there is memory
pressure.

### Globals

Global variables are instrumented for out-of-bounds accesses.

When the kernel is compiled with kasan and global checking is enabled, a
redzone is added to the right of every global object. Out-of-bounds accesses
that hit the redzone are errors and are reported via the same mechanism as
other out-of-bounds accesses.

ASAN can also instrument globals for initialization order bugs; we do not
support that feature yet.

[address-sanitizer]: https://clang.llvm.org/docs/AddressSanitizer.html
