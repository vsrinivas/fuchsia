# `memalloc`

A simple, low-dependency memory allocator, intended to be used in environments
such as early boot. It supports tracking multiple ranges of memory and
performing allocations of a user-specified size and alignment.

Internally, the allocator uses a simple linked list weaved through the free
memory regions to track space, and ordered by memory address. The allocator
uses a first-fit algorithm to allocate memory to callers.

## Alternatives

Before using this, you may want to ensure that other alternatives are not a
better fit for your needs.

*   Various *malloc* libraries are available, including [Scudo malloc][scudo]
    implementation or the kernel's [cmpctmalloc][] implementation. These are
    more robust and performant allocators, albeit with more dependencies in the
    Scudo case.

    The two allocators have book-keeping proportional to the number of
    allocations; in contrast, this simple allocator has book-keeping
    proportional to the number of free regions, which are stored in the regions
    themselves. 100% of memory added to the allocator can be returned via the
    `Allocate` method.

*   [region-alloc][] supports allocating arbitrarily sized and aligned regions
    of a 64-bit space. User-specified ranges can be reserved. The library is
    thread safe, and memory required for book-keeping can be allocated in
    advance.

[cmpctmalloc]: https://cs.opensource.google/fuchsia/fuchsia/+/master:zircon/kernel/lib/heap/cmpctmalloc/cmpctmalloc.cc
[scudo]: https://cs.opensource.google/fuchsia/fuchsia/+/master:zircon/third_party/scudo/README.fuchsia.md
[region-alloc]: https://cs.opensource.google/fuchsia/fuchsia/+/master:zircon/system/ulib/region-alloc/
