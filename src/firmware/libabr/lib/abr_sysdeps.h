/* Copyright 2019 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef FIRMWARE_LIBABR_ABR_SYSDEPS_H_
#define FIRMWARE_LIBABR_ABR_SYSDEPS_H_

/* Define this to zero to remove all standard library dependencies. Note you'll
 * need to define some standard types and implement the abr_* functions declared
 * below.
 */
#define LIBABR_USE_STDLIB_DEPS 1

#if LIBABR_USE_STDLIB_DEPS
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif /* LIBABR_USE_STDLIB_DEPS */

#ifdef __cplusplus
extern "C" {
#endif

/* This should work for clang and gcc. */
#define ABR_ATTR_PACKED __attribute__((packed))

#if LIBABR_USE_STDLIB_DEPS
#define AbrMemcpy memcpy
#define AbrMemset memset
#define AbrPrint printf
#define AbrAbort abort
#else  /* LIBABR_USE_STDLIB_DEPS */

/* Like standard memcpy. Copy |n| bytes from |src| to |dest|. Returns |dest|. */
void* AbrMemcpy(void* dest, const void* src, size_t n);

/* Like standard memset. Set |n| bytes at |dest| to |c|. Returns |dest|. */
void* AbrMemset(void* dest, const int c, size_t n);

/* Prints out a NUL-terminated string. Note this is much simpler than the standard printf, but
 * printf can safely replace this at call-sites.
 */
void AbrPrint(const char* message);

/* Like standard abort. Aborts the program or reboots the device if |abort| is not implemented in a
 * platform. */
void AbrAbort(void);
#endif /* LIBABR_USE_STDLIB_DEPS */

/* Calculates the CRC-32 for data in |buf| of size |buf_size|. An implementation is not provided
 * because most environments already have an implementation available.
 */
uint32_t AbrCrc32(const void* buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_LIBABR_ABR_SYSDEPS_H_ */
