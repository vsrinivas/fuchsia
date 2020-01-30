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
