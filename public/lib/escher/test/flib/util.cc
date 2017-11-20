// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/test/flib/util.h"

#include "lib/fxl/logging.h"

namespace escher {
namespace test {

bool IsEventSignalled(const zx::event& fence, zx_signals_t signal) {
  zx_signals_t pending = 0u;
  fence.wait_one(signal, 0, &pending);
  return (pending & signal) != 0u;
}

zx::event CopyEvent(const zx::event& event) {
  zx::event event_copy;
  if (event.duplicate(ZX_RIGHT_SAME_RIGHTS, &event_copy) != ZX_OK)
    FXL_LOG(ERROR) << "Copying zx::event failed.";
  return event_copy;
}

}  // namespace test
}  // namespace escher
