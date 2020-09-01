// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <openthread/platform/misc.h>

static otPlatResetReason sPlatResetReason = OT_PLAT_RESET_REASON_POWER_ON;

otPlatResetReason otPlatGetResetReason(otInstance *aInstance) {
  OT_UNUSED_VARIABLE(aInstance);

  return sPlatResetReason;
}
