// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_C_CRT1_H_
#define ZIRCON_SYSTEM_ULIB_C_CRT1_H_

#include <zircon/types.h>  // zx_handle_t

extern "C" {

// Defined in Scrt1.o, linked into main executable.
[[noreturn]] void _start(zx_handle_t bootstrap);

// Referenced by _start, defined wherever in the main executable's link (it
// could be in a DSO or statically linked into the main executable).
int main(int, char**, char**);

using main_t = decltype(main)*;

// Referenced by _start, defined in libc.
// TODO(mcgrathr): Convert the impl. to C++, have the impl. use this header.
// Probably do as part of bootstrap protocol cleanup.
[[noreturn]] void __libc_start_main(zx_handle_t, main_t);
}

#endif  // ZIRCON_SYSTEM_ULIB_C_CRT1_H_
