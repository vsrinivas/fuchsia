# C++ in Zircon

A subset of the C++14 language is used in the Zircon tree. This
includes both the upper layers of the kernel (above the lk layer), as
well as some userspace code. In particular, Zircon does not use the
C++ standard library, and many language features are not used or
allowed.

## Language features

- Not allowed
  - Exceptions
  - RTTI and `dynamic_cast`
  - Operator overloading
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
  - Default parameters
    - But use judgment. One optional out parameter at the end is
      probably fine. Four optional bool arguments, probably not.
  - Plain old classes
  - `auto`
  - Multiple implementation inheritance
    - But be judicious. This is used widely for e.g. intrusive
    container mixins.
- Needs more ruling TODO(cpu)
  - Global constructors
    - Currently we have these for global data structures.

## FBL

FBL is the Fuchsia Base Library, which is shared between kernel and userspace.
As a result, FBL has very strict dependencies.  For example, FBL cannot depend
on the syscall interface because the syscall interface is not available within
the kernel.  Similarly, FBL cannot depend on C library features that are not
available in the kernel.

1. [system/ulib/fbl](../system/ulib/fbl) which is usable from both
   kernel and userspace.
2. [kernel/lib/fbl](../kernel/lib/fbl) which is usable only from
    the kernel.

FBL provides:

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
  - [fixed sized arrays](../kernel/lib/fbl/include/fbl/inline_array.h),
    which stack allocates small arrays
- inline containers
  - [doubly linked list](../system/ulib/fbl/include/fbl/intrusive_double_list.h)
  - [hash table](../system/ulib/fbl/include/fbl/intrusive_hash_table.h)
  - [singly linked list](../system/ulib/fbl/include/fbl/intrusive_single_list.h)
  - [wavl trees](../system/ulib/fbl/include/fbl/intrusive_wavl_tree.h)
- smart pointers
  - [intrusive refcounted mixin](../system/ulib/fbl/include/fbl/ref_counted.h)
  - [intrusive refcounting pointer](../system/ulib/fbl/include/fbl/ref_ptr.h)
  - [unique pointer](../system/ulib/fbl/include/fbl/unique_ptr.h)
- raii utilities
  - [auto call](../system/ulib/fbl/include/fbl/auto_call.h) to run
    code upon leaving scope
  - [AutoLock](../system/ulib/fbl/include/fbl/auto_lock.h)

FBL has strict controls on memory allocation.  Memory allocation should be
explicit, using an AllocChecker to let clients recover from allocation
failures.  In some cases, implicit memory allocation is permitted, but
functions that implicitly allocate memory must be #ifdef'ed to be unavailable
in the kernel.

FBL not available outside the Fuchsia Source Tree.

## ZX

ZX contains C++ wrappers for the Zircon [objects](objects) and
[syscalls](syscalls.md).  These wrappers provide type safety and move semantics
for handles but offer no opinion beyond what's in syscalls.abigen.  At some
point in the future, we might autogenerate ZX from syscalls.abigen, similar to
how we autogenerate the syscall wrappers in other languages.

ZX is part of the Fuchsia SDK.

## FZL

FZL is the Fuchsia Zircon Library.  This library provides value-add for common
operations involving kernel objects and is free to have opinions about how to
interact with the Zircon syscalls.  If a piece of code has no dependency on
Zircon syscalls, the code should go in FBL instead.

FZL not available outside the Fuchsia Source Tree.

## ZXCPP

Some of our code runs in an environment which cannot include the
standard C++ runtime environment. This environment includes symbols
like __cxa_pure_virtual that are defined by the ABI and that the
compiler expects to be ambient. [The zxcpp
library](../system/ulib/zxcpp) provides that dependency. It also
includes the placement operator new overloads and, in userspace, the
standard new and delete operators. Note that it does not include the
similarly named __cxa_atexit, which in userspace must be provided by
the libc. See extensive comments in musl's atexit implementation if
you are curious.

*This library is mutually exclusive of the standard C++ library.*
