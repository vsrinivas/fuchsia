// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>

.section .bootfs
.align 4

// include the bootfs archive containing user 
DATA(user_bootfs)
.incbin USER_BOOTFS_FILENAME

user_bootfs_end:
.size user_bootfs, user_bootfs_end - user_bootfs

DATA(user_bootfs_len)
    .int user_bootfs_end - user_bootfs
.size user_bootfs_len, . - user_bootfs_len
