// This file circumvents the include order when building musl itself so that
// musl's <limits.h> prevails over the compiler's <limits.h>.
//
// The include order puts the libc (musl) headers last (via `-idirafter`).
// This is important so that the compiler and/or libc++ header can come
// in first and use #include_next to reach the corresponding libc header.
//
// Clang's <limits.h> does #include_next only #ifdef __STDC_HOSTED__.
// But musl itself is compiled with -ffreestanding, which removes that
// predefine.
//
// For building musl, just musl's <limits.h> alone without the Clang
// header's interjections works fine.  Since musl looks in src/internal
// first, it will find this header first.  So this simply redirects to
// the real libc header, which is what #include_next would find if
// Clang's header did it.

#include "../../include/limits.h"

// The libc++ <climits> header, which is used by other libc++ headers, includes
// <limits.h> and tries to check that it found the libc++ wrapper header, which
// doesn't do anything useful.  Since this header preempts all others while
// buildling, just define the macro that <climits> checks.
#define _LIBCPP_LIMITS_H
