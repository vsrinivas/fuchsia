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

## fbl
We have built our own template library, called *fbl*, to
address our particular needs. This library is split into two parts:

1. [system/ulib/fbl](../system/ulib/fbl) which is usable from both
   kernel and userspace.
2. [kernel/lib/fbl](../kernel/lib/fbl) which is usable only from
    the kernel.

*fbl* provides

- utility code
  - [basic algorithms](../system/ulib/fbl/include/fbl/algorithm.h)
  - [integer type limits](../system/ulib/fbl/include/fbl/limits.h)
  - [type traits](../system/ulib/fbl/include/fbl/type_support.h)
  - [atomics](../system/ulib/fbl/include/fbl/atomic.h)
  - [alloc checking new](../system/ulib/fbl/include/fbl/alloc_checker.h)
- allocators
  - [slab allocation](../system/ulib/fbl/include/fbl/slab_allocator.h)
  - [slab malloc](../system/ulib/fbl/include/fbl/slab_malloc.h)
- arrays
  - [fixed sized arrays](../system/ulib/fbl/include/fbl/array.h)
  - [fixed sized arrays](../system/ulib/fbl/include/fbl/inline_array.h),
    which stack allocates small arrays
- inline containers
  - [doubly linked list](../system/ulib/fbl/include/fbl/intrusive_double_list.h)
  - [hash table](../system/ulib/fbl/include/fbl/intrusive_hash_table.h)
  - [singly linked list](../system/ulib/fbl/include/fbl/intrusive_single_list.h)
  - [wavl trees](../system/ulib/fbl/include/fbl/intrusive_wavl_tree.h)
- smart pointers
  - [intrusive refcounting mixin](../system/ulib/fbl/include/fbl/ref_counted.h)
  - [intrusive refcounted pointer](../system/ulib/fbl/include/fbl/ref_ptr.h)
  - [unique pointer](../system/ulib/fbl/include/fbl/unique_ptr.h)
- raii utilities
  - [auto call](../system/ulib/fbl/include/fbl/auto_call.h) to run
    code upon leaving scope
  - [AutoLock](../system/ulib/fbl/include/fbl/auto_lock.h)

The standard operator new is assumed to either return valid memory or
to throw std::bad_alloc. This policy is not suitable for the
kernel. We also want to dynamically enforce that returns are
explicitly checked. As such, fbl introduces our own operator new
overload which takes a reference to an `AllocChecker`. If the status
of the `AllocChecker` is not queried after the new expression, an
assertion is raised. This lets us enforce that the return value is
checked without having to reason about optimizations of the standard
operator new in the presence of -fno-exceptions and so on.

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
