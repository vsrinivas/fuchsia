Fuchsia Core Libraries
======================

This document describes the core libraries in the Fuchsia system, starting from
the bottom of the dependency chain.

# Magenta libraries

TODO(abarth): Link to docs about mxtl and mx

# Fuchsia Template Library (FTL)

FTL is a platform-independent library containing basic C++ building blocks, such
as logging and reference counting. FTL depends on the C++ standard library but
not on any Magenta- or Fuchsia-specific libraries. We build FTL both for target
(Fuchsia) and for host (Linux, Mac) systems.

Generally speaking, we try to use Tthe C++ standard library for basic building
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
