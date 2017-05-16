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

## mxcpp

Some of our code runs in an environment which cannot include the
standard C++ runtime environment. This environment includes symbols
like __cxa_pure_virtual that are defined by the ABI and that the
compiler expects to be ambient. [The mxcpp
library](../system/ulib/mxcpp) provides that dependency. It also
includes the placement operator new overloads and, in userspace, the
standard new and delete operators. Note that it does not include the
similarly named __cxa_atexit, which in userspace must be provided by
the libc. See extensive comments in musl's atexit implementation if
you are curious.

*This library is mutually exclusive of the standard C++ library.*

## mxalloc

The standard operator new is assumed to either return valid memory or
to throw std::bad_alloc. This policy is not suitable for the
kernel. We also want to dynamically enforce that returns are
explicitly checked. As such, [the mxalloc
library](../system/ulib/mxalloc) introduces our own operator new
overload which takes a reference to an `AllocChecker`. If the status
of the `AllocChecker` is not queried after the new expression, an
assertion is raised. This lets us enforce that the return value is
checked without having to reason about optimizations of the standard
operator new in the presence of -fno-exceptions and so on.

This library can be linked into programs that use the standard
library, and also into programs that use `mxcpp`.
