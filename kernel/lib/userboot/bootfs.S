// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>

// put this in a special section, mark it read only
.section bootfs,"a"
.balign 4096

// include the bootfs archive containing a user file system
DATA(user_bootfs)
.incbin USER_BOOTFS_FILENAME
END(user_bootfs)

.Luser_bootfs_end:

// save the length of the bootfs
DATA(user_bootfs_len)
    .int .Luser_bootfs_end - user_bootfs
END(user_bootfs_len)
