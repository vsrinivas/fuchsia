// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thread.h"

#include <fuchsia/scheduler/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/threads.h>

#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "utils.h"

// Static storage for Thread::
zx::channel Thread::scheduler_service_;
std::array<zx::profile, Thread::PRIORITY_LEVELS> Thread::profiles_;

Thread::Thread(uint32_t prio) : prio_(prio) {
  snprintf(name_, sizeof(name_), "mutex_pi_thread %02u", prio_);
}

// static
zx_status_t Thread::ConnectSchedulerService() {
  if (scheduler_service_.is_valid()) {
    return ZX_ERR_BAD_STATE;
  }

  zx::channel server_ep;
  zx::channel::create(0, &scheduler_service_, &server_ep);
  zx_status_t res;

  res = fdio_service_connect("/svc/" fuchsia_scheduler_ProfileProvider_Name, server_ep.release());
  if (res != ZX_OK) {
    fprintf(stderr, "Failed to connect schedule.ProfileProvider! (res %d)\n", res);
  }

  return res;
}

// static
zx_status_t Thread::EnsureProfile(uint32_t prio_level) {
  if (prio_level >= profiles_.size()) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx::profile& profile = profiles_[prio_level];
  if (profile.is_valid()) {
    return ZX_OK;
  }

  char name[32];
  size_t name_len = snprintf(name, sizeof(name), "mutex_pi_exerciser %02u", prio_level);
  zx_status_t get_profile_res;
  zx_status_t res;

  res = fuchsia_scheduler_ProfileProviderGetProfile(scheduler_service_.get(), prio_level, name,
                                                    name_len, &get_profile_res,
                                                    profile.reset_and_get_address());
  if ((res != ZX_OK) || (get_profile_res != ZX_OK)) {
    fprintf(stderr, "Failed to obtain profile for priority %u (res = %d, gp_res = %d)\n",
            prio_level, res, get_profile_res);
    if (res == ZX_OK) {
      res = get_profile_res;
    }
  }

  return res;
}

zx_status_t Thread::Start(Thunk thunk) {
  zx_status_t res;

  if (!static_cast<bool>(thunk)) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto state = state_.load();
  if ((state != State::INIT) && (state != State::WAITING_TO_START)) {
    return ZX_ERR_BAD_STATE;
  }

  // If we have not created our thread yet, do so now.
  if (state == State::INIT) {
    barrier_.Reset();

    auto trampoline = [](void* ctx) -> int { return reinterpret_cast<Thread*>(ctx)->EntryPoint(); };

    int thrd_create_res = thrd_create_with_name(&thread_, trampoline, this, name_);
    if (thrd_create_res != ZX_OK) {
      return ZX_ERR_NO_RESOURCES;
    }

    auto cleanup = fbl::MakeAutoCall([this]() { Exit(); });

    zx_status_t res = EnsureProfile(prio_);
    if (res != ZX_OK) {
      return res;
    }

    zx::unowned_thread thread(thrd_get_zx_handle(thread_));
    res = thread->set_profile(profiles_[prio_], 0);
    if (res != ZX_OK) {
      return res;
    }

    res = thread->duplicate(ZX_RIGHT_SAME_RIGHTS, &(handle_));
    if (res != ZX_OK) {
      return res;
    }

    res = WaitForState(State::WAITING_TO_START);
    if (res != ZX_OK) {
      return res;
    }

    cleanup.cancel();
  }

  // Install our new thunk, let the thread go, and wait until it is running.
  ZX_DEBUG_ASSERT(state_.load() == State::WAITING_TO_START);
  {
    fbl::AutoLock lock(&thunk_lock_);
    thunk_ = std::move(thunk);
  }
  barrier_.Signal();
  res = WaitForState(State::RUNNING);
  if (res != ZX_OK) {
    return res;
  }
  barrier_.Reset();

  return ZX_OK;
}

int Thread::EntryPoint() {
  state_.store(State::WAITING_TO_START);
  while (true) {
    barrier_.Wait();

    {
      fbl::AutoLock lock(&thunk_lock_);
      if (static_cast<bool>(thunk_)) {
        state_.store(State::RUNNING);
        thunk_();
        thunk_ = nullptr;
        state_.store(State::WAITING_TO_START);
      } else {
        break;
      }
    }
  }
  state_.store(State::EXITED);
  return 0;
}

void Thread::Exit() {
  if (handle_.is_valid()) {
    ZX_ASSERT(WaitForState(State::WAITING_TO_START) == ZX_OK);
    barrier_.Signal();
    thrd_join(thread_, nullptr);
  }

  barrier_.Reset();
  handle_.reset();
  state_.store(State::INIT);
}

zx_status_t Thread::WaitForState(State target_state) {
  zx_status_t res;

  res = WaitFor([this, target_state]() { return state_.load() == target_state; }, zx::msec(500));
  if (res != ZX_OK) {
    fprintf(stderr, "timed out waiting for \"%s\" to achieve state (%u)\n", name_,
            static_cast<uint32_t>(target_state));
    return res;
  }

  return ZX_OK;
}
