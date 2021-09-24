// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "misc.h"

static otPlatResetReason sPlatResetReason = OT_PLAT_RESET_REASON_POWER_ON;
static OtStackCallBack *ot_stack_callback_ptr = nullptr;

otPlatResetReason otPlatGetResetReason(otInstance *aInstance) {
  OT_UNUSED_VARIABLE(aInstance);

  return sPlatResetReason;
}

void platformMiscSetCallbackPtr(OtStackCallBack *callback_ptr) {
  ot_stack_callback_ptr = callback_ptr;
}

void otPlatReset(otInstance *a_instance) {
  OT_UNUSED_VARIABLE(a_instance);
  if (ot_stack_callback_ptr != nullptr) {
    ot_stack_callback_ptr->Reset();
  }
}
