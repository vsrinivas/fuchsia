// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

#include "lib/ftl/logging.h"

namespace debugserver {
namespace arch {

Registers::Registers(const mx_handle_t thread_handle)
    : thread_handle_(thread_handle) {
  FTL_DCHECK(thread_handle_ != MX_HANDLE_INVALID);
}

}  // namespace arch
}  // namespace debugserver
