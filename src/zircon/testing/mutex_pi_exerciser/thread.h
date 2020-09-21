// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_ZIRCON_TESTING_MUTEX_PI_EXERCISER_THREAD_H_
#define SRC_ZIRCON_TESTING_MUTEX_PI_EXERCISER_THREAD_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/profile.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <threads.h>
#include <zircon/types.h>

#include <array>

#include <fbl/function.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>

#include "event.h"

class Thread {
 public:
  using Thunk = fbl::InlineFunction<void(), sizeof(void*) * 4>;

  explicit Thread(uint32_t prio);
  ~Thread() { Exit(); }

  DISALLOW_COPY_ASSIGN_AND_MOVE(Thread);

  static zx_status_t ConnectSchedulerService();
  uint32_t prio() const { return prio_; }
  const char* name() const { return name_; }

  zx_status_t Start(Thunk thunk);
  zx_status_t WaitForReset() { return WaitForState(State::WAITING_TO_START); }

 private:
  enum class State { INIT, WAITING_TO_START, RUNNING, EXITED };

  static constexpr zx::duration THREAD_TIMEOUT = zx::msec(1000);
  static constexpr zx::duration THREAD_POLL_INTERVAL = zx::msec(1);
  static constexpr uint32_t PRIORITY_LEVELS = 32;

  static zx_status_t EnsureProfile(uint32_t prio_level);

  int EntryPoint();
  void Exit();

  zx_status_t WaitForState(State target_state);

  static zx::channel scheduler_service_;
  static std::array<zx::profile, PRIORITY_LEVELS> profiles_;

  const uint32_t prio_;
  char name_[ZX_MAX_NAME_LEN];

  fbl::Mutex thunk_lock_;
  Thunk thunk_ TA_GUARDED(thunk_lock_);

  thrd_t thread_;
  zx::thread handle_;
  Event barrier_;
  std::atomic<State> state_{State::INIT};
};

#endif  // SRC_ZIRCON_TESTING_MUTEX_PI_EXERCISER_THREAD_H_
