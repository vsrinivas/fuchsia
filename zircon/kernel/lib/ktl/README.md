# Kernel Template Library

The Kernel Template Library is a curated subset of the C++ standard library API
using the `ktl` namespace rather than the `std` namespace.  When a specific
interface is deemed useful, safe, and appropriate for use in the kernel, it can
be added here.  No `std` APIs are ever used directly in the kernel, only `ktl`.

The API is a strict subset of the standard C++ library API aside from the
namespace.  The header file names are mostly directly mapped as well:

    <foo> -> <ktl/foo.h>

There are some exceptions.  For example, <memory> and <utility> are very broad
collections of APIs and the ktl subset is more focussed.  So instead of
<ktl/memory.h> and <ktl/utility.h> there are specific headers for the standard
APIs from <memory> and <utility> that are included, such as <ktl/move.h> for
`ktl::move` (`std::move` in <utility>) and <ktl/unique_ptr.h> for
`ktl::unique_ptr` (`std::unique_ptr` in <memory>).

The implementation here simply leverages the libc++ implementation and defines
`using` aliases in the `ktl` namespace.  It's expected that ktl will only ever
provide "header only" APIs.

## ktl enforcement

The [`<ktl/enforce.h>`](include/ktl/enforce.h) header file should be used in
most `.cc` files (but not `.h`) files meant only for the kernel environment.
That is, in `.cc` files that use the `<ktl/*.h>` headers or use kernel-only
headers that use them.

This file must appear last in the `#include` list and `clang-format` will
arrange that automatically.

After this `#include` any use of the `std` identifier in C++ code will cause a
compile-time error.  When such errors are encountered, the kernel code should
be checked for use of C++ standard library headers (with no `.h` in the name,
like `<memory>, etc.) that should be replaced with `<ktl/*.h>` headers and for
`std::` names that should be replaced with `ktl::` names.
