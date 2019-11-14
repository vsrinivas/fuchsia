// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include <ddk/driver.h>

zx_handle_t get_root_resource() { return ZX_HANDLE_INVALID; }
