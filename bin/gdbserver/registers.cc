// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

#include "lib/ftl/logging.h"

#include "thread.h"

namespace debugserver {
namespace arch {

Registers::Registers(Thread* thread) : thread_(thread) {
  FTL_DCHECK(thread);
  FTL_DCHECK(thread->debug_handle() != MX_HANDLE_INVALID);
}

}  // namespace arch
}  // namespace debugserver
