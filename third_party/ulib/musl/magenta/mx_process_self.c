// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/process.h>

#include "libc.h"

mx_handle_t mx_process_self(void) {
    return libc.proc;
}
