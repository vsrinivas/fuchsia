// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <zircon/boot/bootdata.h>

.section .text.boot0, "a", @progbits

LOCAL_DATA(_bootdata_header)

    /* bootdata file header */
    .long BOOTDATA_CONTAINER
    .long __data_end - _bootdata_header - 32
    .long BOOTDATA_MAGIC
    .long BOOTDATA_FLAG_V2
    .long 0
    .long 0
    .long BOOTITEM_MAGIC
    .long BOOTITEM_NO_CRC32

    /* bootdata kernel header */
    .long BOOTDATA_KERNEL
    .long __data_end - _bootdata_header - 64
    .long 0
    .long BOOTDATA_FLAG_V2
    .long 0
    .long 0
    .long BOOTITEM_MAGIC
    .long BOOTITEM_NO_CRC32

    /* bootdata kernel payload */
    .quad _start - _bootdata_header
    .quad 0
