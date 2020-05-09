# Kernel Address Sanitizer

[Address Sanitizer (ASAN)][address-sanitizer] is a dynamic (run-time)
sanitizer that checks for certain invalid memory accesses in C++ code -
for example, buffer overruns, use-after-free, (stack)
use-after-return/scope, and use of uninitialized globals. The sanitizer
adds compiler-generated instrumentation before every data memory access
and the runtime checks the validity of each access before it proceeds.

ASAN works by constructing a 'shadow map', a map with one byte per 8
bytes of kernel address space. Each shadow map byte tracks the validity
of the kernel address it corresponds to - zero represents 'valid for
access', non-zero bytes represent various sub-byte tracking or invalid
states.

Kernel ASAN is similar to userspace ASAN but has unique bootstrap and
memory allocation requirements.

# Implementation
## Early Boot Setup

When kASAN is enabled, all compiled kernel code is instrumented; so we
need a valid shadow map very early in boot, before C code is called.
Currently the x86-64 kernel has 512 GB of virtual addres space; KASAN
requires 64 GB of shadow memory to track this entire region,
corresponding to 1 byte per 8 bytes.

We create a shadow map at [-128GB ; -64GB) to cover all kernel virtual
address space, and point every page of the shadow map to a single
read-only zero page. One page table and one page directory are reused
for all entries in the MMU, to save memory.

The shadow map is placed at [-128GB, -64GB) to avoid any potential
mappings at the highest parts of the kernel address space. Currently
the reallocated kernel is present there and it is foreseeable that
other structures may be placed there for convenient access.

The map in x86_64 looks like this:

* 64 entries in pdp_hi (1GB each) point all to the same page directory
  (kasan_shadow_tables[512..1023]), with RW, NX and global permissions.

* The kasan page directory has 512 entries pointing all to the same page
  table (kasan_shadow_tables), with RW, NX and global permissions.

* The kasan page table has 512 entries pointing all to the same zero
  page (kasan_zero_page), with RO, NX and global permissions.

With this structure, all poison checks inside the kernel address space
will succeed, as all shadow map memory is marked as unpoisoned.

## Late Boot Setup

In order to allow memory poisoning / tracking validity of kernel memory,
asan needs to have writable pages backing portions of the shadow that
cover the kernel physical map.

Our implementation of asan only tracks memory within the physmap; this
covers all page allocations/frees and the kernel heap, but does not
cover additional virtual mappings within the kernel address space.

During late boot, after PMM is initialized, we allocate a shadow page for
every 8 pages of physmap that contain at least one page of real memory.
We do not consider MMIO regions, device memory, the ISA hole, etc. as
real memory. We then replace the early boot zero page mappings with
mappings to the newly allocated shadow pages. All the remaining early
boot mappings remain the same.

## Runtime

### Poisoning

This version of asan exposes an interface for callers to poison and check the
validity of memory via the following functions:

* `asan_poison_shadow`
* `asan_region_is_poisoned`
* `asan_address_is_poisoned`

Memory allocators should use `asan_poison_shadow` to mark regions of memory as
valid or invalid, specifying different poison values for different types of
memory.

[address-sanitizer]: https://clang.llvm.org/docs/AddressSanitizer.html
