#ifndef SYSROOT_BITS_NULL_H_
#define SYSROOT_BITS_NULL_H_

// The compiler's <stddef.h> defines NULL without defining anything
// else if __need_NULL is defined first.
#define __need_NULL
#include <stddef.h>

#endif  // SYSROOT_BITS_NULL_H_
