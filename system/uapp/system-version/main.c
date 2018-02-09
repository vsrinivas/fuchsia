// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <zircon/syscalls.h>

int main(int argc, char** argv) {
    char buf[64];
    zx_status_t status = zx_system_get_version(buf, sizeof(buf));
    assert(status == ZX_OK);
    printf("%s\n", buf);
    return 0;
}
