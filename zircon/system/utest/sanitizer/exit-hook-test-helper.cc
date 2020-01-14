// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "exit-hook-test-helper.h"

#include <zircon/compiler.h>
#include <zircon/sanitizer.h>

constexpr int kExitStatus = 17;
static_assert(kExitStatus != kHookStatus);

__EXPORT int __sanitizer_process_exit_hook(int status) { return kHookStatus; }

int main() { return kExitStatus; }
