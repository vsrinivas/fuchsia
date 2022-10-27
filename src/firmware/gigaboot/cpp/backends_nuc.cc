// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <zircon/assert.h>

#include "backends.h"

namespace gigaboot {

bool SetRebootMode(RebootMode mode) {
  // TODO(b/238334864): Implement reboot mode configuration using the same
  // EFI variable approach in legacy gigaboot.
  return true;
}

RebootMode GetRebootMode() {
  // TODO(b/238334864): Implement reboot mode configuration using the same
  // EFI variable approach in legacy gigaboot.
  return RebootMode::kNormal;
}

}  // namespace gigaboot
