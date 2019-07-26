// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/futex.h>
#include <fbl/macros.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

class Event {
 public:
  Event() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(Event);

  zx_status_t Wait(zx::duration timeout = zx::duration::infinite());
  void Signal();
  void Reset();

 private:
  fbl::futex_t signaled_ = {0};
};
