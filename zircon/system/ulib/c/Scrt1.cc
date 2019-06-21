// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crt1.h"

[[noreturn]] void _start(zx_handle_t bootstrap) {
    __libc_start_main(bootstrap, &main);
}
