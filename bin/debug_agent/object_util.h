// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

namespace zx {
class process;
class thread;
}

zx::thread ThreadForKoid(const zx::process& process, zx_koid_t thread_koid);
zx::thread ThreadForKoid(zx_handle_t process, zx_koid_t thread_koid);

zx_koid_t KoidForProcess(const zx::process& process);
