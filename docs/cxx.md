# C++ in Magenta

A subset of the C++14 language is used in the Magenta tree. This
includes both the upper layers of the kernel (above the lk layer), as
well as some userspace code. In particular, Magenta does not use the
C++ standard library, and many language features are not used or
allowed.

## Language features

- Not allowed
  - Exceptions
  - RTTI and `dynamic_cast`
  - Operator overloading
  - Default parameters
  - Virtual inheritance
  - Statically constructed objects
  - Trailing return type syntax
    - Exception: when necessary for lambdas with otherwise unutterable return types
  - Initializer lists
  - `thread_local` in kernel code
- Allowed
  - Pure interface inheritance
  - Lambdas
  - `constexpr`
  - `nullptr`
  - `enum class`es
  - `template`s
  - Plain old classes
  - `auto`
  - Multiple implementation inheritance
    - But be judicious. This is used widely for e.g. intrusive
    container mixins.
- Needs more ruling TODO(cpu)
  - Global constructors
    - Currently we have these for global data structures.

## mxtl
We have built our own template library, called *mxtl*, to
address our particular needs. This library is split into two parts:

1. [system/ulib/mxtl](../system/ulib/mxtl) which is usable from both
   kernel and userspace.
2. [kernel/lib/mxtl](../kernel/lib/mxtl) which is usable only from
    the kernel.

*mxtl* provides

- utility code
  - [basic algorithms](../system/ulib/mxtl/include/mxtl/algorithm.h)
  - [integer type limits](../system/ulib/mxtl/include/mxtl/limits.h)
  - [type traits](../system/ulib/mxtl/include/mxtl/type_support.h)
  - [atomics](../system/ulib/mxtl/include/mxtl/atomic.h)
- allocators
  - [slab allocation](../system/ulib/mxtl/include/mxtl/slab_allocator.h)
  - [slab malloc](../system/ulib/mxtl/include/mxtl/slab_malloc.h)
- arrays
  - [fixed sized arrays](../system/ulib/mxtl/include/mxtl/array.h)
  - [fixed sized arrays](../system/ulib/mxtl/include/mxtl/inline_array.h),
    which stack allocates small arrays
- inline containers
  - [doubly linked list](../system/ulib/mxtl/include/mxtl/intrusive_double_list.h)
  - [hash table](../system/ulib/mxtl/include/mxtl/intrusive_hash_table.h)
  - [singly linked list](../system/ulib/mxtl/include/mxtl/intrusive_single_list.h)
  - [wavl trees](../system/ulib/mxtl/include/mxtl/intrusive_wavl_tree.h)
- smart pointers
  - [intrusive refcounting mixin](../system/ulib/mxtl/include/mxtl/ref_counted.h)
  - [intrusive refcounted pointer](../system/ulib/mxtl/include/mxtl/ref_ptr.h)
  - [unique pointer](../system/ulib/mxtl/include/mxtl/unique_ptr.h)
- raii utilities
  - [auto call](../system/ulib/mxtl/include/mxtl/auto_call.h) to run
    code upon leaving scope
  - [AutoLock](../system/ulib/mxtl/include/mxtl/auto_lock.h)

## mx

We have built a minimal C++ library around the various Magenta
[objects](objects) and [syscalls](syscalls.md) called
[`mx`](../system/ulib/mx/README.md). `mx` is a minimal layer on top of
`mx_handle_t` and the system calls, to provide handles with type
safety and ownership semantics.
