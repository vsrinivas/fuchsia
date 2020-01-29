/* Copyright 2019 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef FIRMWARE_LIBABR_ABR_UTIL_H_
#define FIRMWARE_LIBABR_ABR_UTIL_H_

#ifdef __cplusplus
extern "C" {
#endif

#define ABR_STRINGIFY(x) #x
#define ABR_TO_STRING(x) ABR_STRINGIFY(x)

#ifdef ABR_ENABLE_DEBUG
/* Aborts the program if |expr| is false.
 *
 * This has no effect unless ABR_ENABLE_DEBUG is defined.
 */
#define ABR_ASSERT(expr)                     \
  do {                                       \
    if (!(expr)) {                           \
      ABR_FATAL("assert fail: " #expr "\n"); \
    }                                        \
  } while (0)
#else
#define ABR_ASSERT(expr)
#endif /* ABR_ENABLE_DEBUG */

#ifdef ABR_ENABLE_DEBUG
/* Print a message, used for diagnostics.
 *
 * This has no effect unless ABR_ENABLE_DEBUG is defined.
 */
#define ABR_DEBUG(message)                                              \
  do {                                                                  \
    AbrPrint(__FILE__ ":" ABR_TO_STRING(__LINE__) ": DEBUG: " message); \
  } while (0)
#else
#define ABR_DEBUG(message)
#endif /* ABR_ENABLE_DEBUG */

#define ABR_ERROR(message)                                              \
  do {                                                                  \
    AbrPrint(__FILE__ ":" ABR_TO_STRING(__LINE__) ": ERROR: " message); \
  } while (0)

#define ABR_FATAL(message)                                              \
  do {                                                                  \
    AbrPrint(__FILE__ ":" ABR_TO_STRING(__LINE__) ": FATAL: " message); \
    AbrAbort();                                                         \
  } while (0)

/* Converts a 32-bit unsigned integer from big-endian to host byte order. */
uint32_t AbrBigEndianToHost(uint32_t in);

/* Converts a 32-bit unsigned integer from host to big-endian byte order. */
uint32_t AbrHostToBigEndian(uint32_t in);

/* Compare |n| bytes starting at |s1| with |s2| and return 0 if they match, 1 if they don't. Returns
 * 0 if |n|==0, since no bytes mismatched.
 *
 * Time taken to perform the comparison is only dependent on |n| and not on the relationship of the
 * match between |s1| and |s2|.
 *
 * Note that unlike memcmp(), this only indicates inequality, not whether |s1| is less than or
 * greater than |s2|.
 */
int AbrSafeMemcmp(const void* s1, const void* s2, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_LIBABR_ABR_UTIL_H_ */
