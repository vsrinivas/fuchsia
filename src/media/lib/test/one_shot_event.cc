// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/test/one_shot_event.h"

#include <zircon/assert.h>
#include <zircon/errors.h>

OneShotEvent::OneShotEvent() {
  zx_status_t status = zx::event::create(0, &event_);
  ZX_ASSERT(status == ZX_OK);
}

void OneShotEvent::Signal() {
  zx_status_t status = event_.signal(0, ZX_EVENT_SIGNALED);
  ZX_ASSERT(status == ZX_OK);
}

void OneShotEvent::Wait(zx::time just_fail_deadline) {
  zx_signals_t pending;
  zx_status_t status = event_.wait_one(ZX_EVENT_SIGNALED, just_fail_deadline, &pending);
  ZX_ASSERT_MSG(status != ZX_ERR_TIMED_OUT, "Wait timed out.");
  ZX_ASSERT_MSG(status == ZX_OK, "Wait failed.");
}
