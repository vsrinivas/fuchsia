# LLCPP Memory Ownership

This document provides an overview of the tools available to manage memory when
using the LLCPP bindings.

## Pointers and memory ownership {#memory-ownership}

LLCPP objects use special smart pointers called `tracking_ptr` to keep track of memory ownership.
With `tracking_ptr`, LLCPP makes it possible for your code to easily set a value and forget
about ownership since `tracking_ptr` will take care of freeing memory when it goes out of scope.

These pointers have two states:

*   Unowned (constructed from an `unowned_ptr_t`).
*   Heap allocated and owned (constructed from a `std::unique_ptr`).

When the contents is owned, a `tracking_ptr` behaves like a `unique_ptr` and the pointer is
deleted on destruction. In the unowned state, `tracking_ptr` behaves like a raw pointer and
destruction is a no-op.

`tracking_ptr` is move-only and has an API closely matching `unique_ptr`.

### Types of object allocation

`tracking_ptr` makes it possible to create LLCPP objects with several allocation strategies.
The allocation strategies can be mixed and matched within the same code.

#### Heap allocation

To heap allocate objects, use the standard `std::make_unique`.

An example with an optional `uint32` field represented as a `tracking_ptr`.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="heap-field" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

This applies to all union and table fields and data arrays within vectors and strings.
Vector and string data arrays must use the array specialization of `std::unique_ptr`,
which takes the element count as an argument.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="heap-vec" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

To copy a collection to a `VectorView`, use `heap_copy_vec`.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="heap-copy-vec" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```


To copy a string to a `StringView`, use `heap_copy_str`.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="heap-copy-str" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

#### Allocators

FIDL provides an `Allocator` API that enables creating `tracking_ptr`s to LLCPP objects through a
number of allocation algorithms. Currently, `BufferThenHeapAllocator`, `UnsafeBufferAllocator`, and
`HeapAllocator` are available in fidl namespace.

The `BufferThenHeapAllocator` allocates from an in-band fixed-size buffer (can be used for stack
allocation), but falls back to heap allocation if the in-band buffer has been exhausted (to avoid
unnecessary unfortunate surprises). Be aware that excessive stack usage can cause its own problems,
so consider using a buffer size that comfortably fits on the stack, or consider putting the whole
BufferThenHeapAllocator on the heap if the buffer needs to be larger than fits on the stack, or
consider using HeapAllocator. Allocations must be assumed to be gone upon destruction of the
`BufferThenHeapAllocator` used to make them.

The `HeapAllocator` always allocates from the heap, and is unique among allocators (so far) in that
all of the `HeapAllocator` allocations can out-live the `HeapAllocator` instance used to make
them.

The `UnsafeBufferAllocator` is unsafe in the sense that it lacks heap failover, so risks creating
unfortunate data-dependent surprises unless the buffer size is absolutely guaranteed to be large
enough including the internal destructor-tracking overhead.  If the internal buffer is exhausted,
make<>() will panic the entire process. Consider using `BufferThenHeapAllocator` instead.  Do not
use `UnsafeBufferAllocator` without rigorously testing that the worst-case set of cumulative
allocations made through the allocator all fit without a panic, and consider how the rigor will be
maintained as code and FIDL tables are changed.

Example:

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="allocator-field" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

The arguments to `allocator.make` are identical to the arguments to `std::make_unique`.
This also applies to VectorViews.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="allocator-vec" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

To copy a collection to a `VectorView` using an allocator, use `copy_vec`.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="copy-vec" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

To create a copy of a string using an allocator, use `copy_str`.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="copy-str" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

#### Unowned pointers

In addition to the managed allocation strategies, it is also possible to directly
create pointers to memory unowned by FIDL. This is discouraged, as it is easy to
accidentally create use-after-free bugs. `unowned_ptr` exists to explicitly mark
pointers to FIDL-unowned memory.

The `unowned_ptr` helper is the recommended way to create `unowned_ptr_t`s,
which is more ergonomic than using the `unowned_ptr_t` constructor directly.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="unowned-ptr" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

To create a `VectorView` from a collection using an unowned pointer to the
collection's data array, use `unowned_vec`.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="unowned-vec" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

To create a `StringView` from unowned memory, use `unowned_str`.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="unowned-str" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

A `StringView` can also be created directly from string literals without using
`unowned_ptr`.

```c++
{%includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/fidl/llcpp/unittests/main.cc" region_tag="stringview-assign" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

#### fidl::StringView

Defined in [lib/fidl/llcpp/string_view.h](/zircon/system/ulib/fidl/include/lib/fidl/llcpp/string_view.h)

Holds a reference to a variable-length string stored within the buffer. C++
wrapper of **fidl_string**. Does not own the memory of the contents.

`fidl::StringView` may be constructed by supplying the pointer and number of
UTF-8 bytes (excluding trailing `\0`) separately. Alternatively, one could pass
a C++ string literal, or any value which implements `[const] char* data()`
and `size()`. The string view would borrow the contents of the container.

It is memory layout compatible with **fidl_string**.

#### fidl::VectorView\<T\>

Defined in [lib/fidl/llcpp/vector_view.h](/zircon/system/ulib/fidl/include/lib/fidl/llcpp/vector_view.h)

Holds a reference to a variable-length vector of elements stored within the
buffer. C++ wrapper of **fidl_vector**. Does not own the memory of elements.

`fidl::VectorView` may be constructed by supplying the pointer and number of
elements separately. Alternatively, one could pass any value which supports
[`std::data`](https://en.cppreference.com/w/cpp/iterator/data), such as a
standard container, or an array. The vector view would borrow the contents of
the container.

It is memory layout compatible with **fidl_vector**.

#### fidl::Array\<T, N\>

Defined in [lib/fidl/llcpp/array.h](/zircon/system/ulib/fidl/include/lib/fidl/llcpp/array.h)

Owns a fixed-length array of elements.
Similar to `std::array<T, N>` but intended purely for in-place use.

It is memory layout compatible with FIDL arrays, and is standard-layout.
The destructor closes handles if applicable e.g. it is an array of handles.
