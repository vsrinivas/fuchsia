## FBL

FBL is the Fuchsia Base Library, which is shared between kernel and userspace.
As a result, FBL has very strict dependencies.  For example, FBL cannot depend
on the syscall interface because the syscall interface is not available within
the kernel.  Similarly, FBL cannot depend on C library features that are not
available in the kernel.

1. [system/ulib/fbl](.) which is usable from both
   kernel and userspace.
2. [kernel/lib/fbl](../../../kernel/lib/fbl) which is usable only from
    the kernel.

**NOTE:** Some FBL interfaces below that overlap with standard C++ library
interfaces will probably be either removed entirely or made kernel-only (and
perhaps renamed inside the kernel) once userspace code has migrated to using
standard C++ library facilities where appropriate.

FBL provides:

- utility code
  - [basic algorithms](include/fbl/algorithm.h)
  - [atomics](include/fbl/atomic.h)
  - [alloc checking new](include/fbl/alloc_checker.h)
- allocators
  - [slab allocation](include/fbl/slab_allocator.h)
  - [slab malloc](include/fbl/slab_malloc.h)
- arrays
  - [fixed sized arrays](include/fbl/array.h)
  - [fixed sized arrays](../kernel/lib/fbl/include/fbl/inline_array.h),
    which stack allocates small arrays
- inline containers
  - [doubly linked list](include/fbl/intrusive_double_list.h)
  - [hash table](include/fbl/intrusive_hash_table.h)
  - [singly linked list](include/fbl/intrusive_single_list.h)
  - [wavl trees](include/fbl/intrusive_wavl_tree.h)
- smart pointers
  - [intrusive refcounted mixin](include/fbl/ref_counted.h)
  - [intrusive refcounting pointer](include/fbl/ref_ptr.h)
  - [unique pointer](include/fbl/unique_ptr.h)
- raii utilities
  - [auto call](include/fbl/auto_call.h) to run
    code upon leaving scope
  - [AutoLock](include/fbl/auto_lock.h)

FBL has strict controls on memory allocation.  Memory allocation should be
explicit, using an AllocChecker to let clients recover from allocation
failures.  In some cases, implicit memory allocation is permitted, but
functions that implicitly allocate memory must be #ifdef'ed to be unavailable
in the kernel.

FBL not available outside the Platform Source Tree.
