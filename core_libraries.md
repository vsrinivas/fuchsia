Fuchsia Core Libraries
======================

This document describes the core libraries in the Fuchsia system, starting from
the bottom of the dependency chain.

# Magenta libraries

## libmagenta

This library defines the Magenta system ABI.

TODO(kulakowski) Talk about how this is not quite the kernel
syscall interface, since the VDSO abstracts that.

## libmx

libmagenta defines C types and function calls acting on those
objects. libmx is a light C++ wrapper around those. It adds type
safety beyond `mx_handle_t`, so that every kernel object type has a
corresponding C++ type, and adds ownership semantics to those
handles. It otherwise takes no opinions around naming or policy.

For more information about libmx, see
[its documentation](https://fuchsia.googlesource.com/magenta/+/master/system/ulib/mx/README.md).

## FDL

Much of Magenta is written in C++, both in kernel and in
userspace. Linking against the C++ standard library is not especially
well suited to this environment (it is too easy to allocate, throw
exceptions, etc., and the library itself is large). There are a number
of useful constructs in the standard libary that we would wish to use,
like type traits and unique pointers. However, C++ standard libraries
are not really to be consumed piecemeal like this. So we built a
library which provides similar constructs named fdl. This library
also includes constructs not present in the standard library but which
are useful library code for kernel and device driver environments (for
instance, slab allocation).

For more information about FDL,
[read its overview](https://fuchsia.googlesource.com/magenta/+/master/docs/cxx.md#fdl).

# Fuchsia Template Library (FTL)

FTL is a platform-independent library containing basic C++ building blocks, such
as logging and reference counting. FTL depends on the C++ standard library but
not on any Magenta- or Fuchsia-specific libraries. We build FTL both for target
(Fuchsia) and for host (Linux, Mac) systems.

Generally speaking, we try to use the C++ standard library for basic building
blocks, but in some cases the C++ standard library either doesn't have something
we need (e.g., a featureful logging system) or has a version of what we need
doesn't meet our requirements (e.g., `std::shared_ptr` is twice as large as
`ftl::RefPtr`).

# Magenta Template Libary (MTL)

MTL is a Magenta-specific library containing high-level C++ concepts for working
with the Magenta system calls. For example, MTL provides an `mtl::MessageLoop`
abstraction on top of Magenta's underlying waiting primitives. MTL also contains
helpers for working with Magenta primitives asynchronously that build upon
`mtl::MessageLoop` (e.g., for draining a socket asynchronously).

MTL depends on FTL and implements some interfaces defined in FTL, such as
`ftl::TaskRunner`. MTL also depends on `libfidl` and implements FIDL's
asynchronous waiter mechanism using `mtl::MessageLoop`.
