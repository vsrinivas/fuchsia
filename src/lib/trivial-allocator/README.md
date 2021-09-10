# Trivial C++ Allocator Library

The trivial-allocator library provides a small, header-only C++ template
implementation of a trivial memory allocator for C++ code.  It can provide
variants of the C++ `new` expression syntax using an explicit allocator object.

These allocators are intended for two classes of use:

 1. startup allocations that stay live for the lifetime of the whole program
 2. grouping short-term allocations that will only ever be deallocated en masse

They are never appropriate for general allocation uses where objects are
created and deleted cyclically, since there is no reuse of unused memory.

These trivial "leaky" allocators do no real bookkeeping, so individual C++
object allocations cannot really be deallocated or reused.  They simply layer
on top of a simpler allocation functions (or callable object) that has some
means of allocating conveniently-sized (large) chunks, and then they parcel out
memory from those chunks.  Underlying memory allocated is either wholly leaked
or is all kept live during the lifetime of the allocation function object.

Trivial underlying "chunk" allocators to wrap inside the "leaky" allocator are
provided for consuming pre-allocated, fixed-sized buffers.  It's expected that
users of the library will provide simple chunk allocation functions appropriate
for their own context.
