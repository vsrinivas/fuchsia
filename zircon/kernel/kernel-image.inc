// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>

// This header is generated from the symbol table of the linked kernel.
// It defines VERSION_STRING_OFFSET and VERSION_STRING_SIZE, which are the
// bounds of the embedded version string variable inside the kernel image.
#include "kernel-image.h"

// This header defines VERSION_STRING_FILE for use below.  It also defines
// VERSION_STRING_SIZE, not to the size of the file but to the maximum size to
// be allocated at compile time.  This better match what kernel-image.h has
// extracted from the kernel symbol table, or something went wrong.  That
// mismatch will cause a non-identical re-#define error at compile time.
#include <lib/version/version-string.h>

// Pad out to the header size that was allocated in the kernel image layout.
// This ensures that the kernel image is aligned correctly in memory.
.org BOOT_HEADER_SIZE

// Crack the kernel image in half around the version string and replace the
// wafer-thin layer in the middle with the contents of the separate version
// string file, which can change independently of the kernel image itself.

.incbin KERNEL_IMAGE_FILE, BOOT_HEADER_SIZE, VERSION_STRING_OFFSET - BOOT_HEADER_SIZE

// Exactly VERSION_STRING_SIZE bytes are to be replaced in the image.
0:
.incbin VERSION_STRING_FILE

// The replaced bytes must end with a NUL terminator.
.byte 0

// If it's smaller, pad the difference with more NULs.
1:
.space VERSION_STRING_SIZE - (1b - 0b)

// The second half of the kernel image skips the compile-time embedded string.
.incbin KERNEL_IMAGE_FILE, VERSION_STRING_OFFSET + VERSION_STRING_SIZE
