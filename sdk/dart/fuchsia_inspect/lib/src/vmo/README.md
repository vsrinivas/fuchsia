# VMO package

This package contains dart code that manipulates the
[VMO](https://fuchsia.googlesource.com/fuchsia/+/HEAD/docs/development/inspect/vmo-format/)
for storing inspect
information.

## [Slab32](https://en.wikipedia.org/wiki/Slab_allocation) allocator

The Slab32 allocator is an allocator that uses VMO as a free memory heap in which
it allocates fixed size slabs of 32 bytes each.

## Little Big [Slab allocator](https://en.wikipedia.org/wiki/Slab_allocation)

The Little Big Slab allocator is an allocator that uses VMO as a free memory
heap in which it allocates slabs of two different sizes: the "little" and the
"big" slab sizes.

This allocator maintains separate freelists of big and little slabs. Big slabs
are used to allocate relatively large objects for byte or string properties.
Small slabs are used to allocate small objects such as space for holding
integers or doubles. A slab "order" defines its size: a slab of order `N>=0` is
`2^(N+4)` bytes.  The "small" slabs are order `N=1` by default, but the sizes
of both small and big slabs are configurable.

Small and big slabs are interleaved on the VMO based heap.  Each slab size is
aligned on the boundary that is equal to its size.  That is, small slabs of 32
bytes are always allocated at byte offsets evenly divisible by 32. Similarly,
big slabs of 256 bytes are always allocated at byte offsets evenly divisible by
256.

The part of the VMO that is used for allocation is grown as needed, but is
always an integer multiple of the page size.  If the used fraction of
VMO needs to be increased, it is always increased in the integer multiples
of big slab size.

When a small slab is to be allocated, we either use an available one from the
freelist, or convert an existing big slab into a collection of small ones and
sub-allocate in the space that was created this way.

Conversely, if by freeing a small slab an aligned space is created that can fit
an aligned big free slab, the spaces are coalesced into a big slab.

The decision of whether to allocate a big or a small slab is made in the
`allocateBlock` method.  The criterion is the amount of overhead created by an
allocation.
