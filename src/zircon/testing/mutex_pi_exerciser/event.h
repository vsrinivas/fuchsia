// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_ZIRCON_TESTING_MUTEX_PI_EXERCISER_EVENT_H_
#define SRC_ZIRCON_TESTING_MUTEX_PI_EXERCISER_EVENT_H_

#include <lib/zx/time.h>
#include <zircon/types.h>

#include <fbl/futex.h>
#include <fbl/macros.h>

class Event {
 public:
  Event() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(Event);

  zx_status_t Wait(zx::duration timeout = zx::duration::infinite());
  void Signal();
  void Reset();

 private:
  fbl::futex_t signaled_{0};
};

#endif  // SRC_ZIRCON_TESTING_MUTEX_PI_EXERCISER_EVENT_H_
