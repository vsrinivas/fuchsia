// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/thread.h>

#include <zircon/syscalls.h>

#include <lib/zx/process.h>

namespace zx {

zx_status_t thread::create(const process& process, const char* name,
                           uint32_t name_len, uint32_t flags, thread* result) {
    // Assume |result| and |process| must refer to different containers, due
    // to strict aliasing.
    return zx_thread_create(
        process.get(), name, name_len, flags, result->reset_and_get_address());
}

} // namespace zx
