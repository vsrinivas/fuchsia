# `memalloc`

A simple, low-dependency range allocator, intended to be used in environments
such as early boot. Fixed ranges can be added or removed, and allocations
of given sizes and alignment may be made.

No allocations are made by the library --- rather, a pool of memory used for
book-keeping is provided externally. Internally, the allocator uses a linked
list of ranges. We require one element of book-keeping for each range of
integers.

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
    of a 64-bit space. User-specified ranges can be reserved. The library,
    however, has more dependencies than this library, such as locking
    libraries and memory allocation.

[cmpctmalloc]: https://cs.opensource.google/fuchsia/fuchsia/+/master:zircon/kernel/lib/heap/cmpctmalloc/cmpctmalloc.cc
[scudo]: https://cs.opensource.google/fuchsia/fuchsia/+/master:zircon/third_party/scudo/README.fuchsia.md
[region-alloc]: https://cs.opensource.google/fuchsia/fuchsia/+/master:zircon/system/ulib/region-alloc/
