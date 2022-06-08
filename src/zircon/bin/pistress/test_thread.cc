// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_thread.h"

#include <lib/fdio/directory.h>
#include <lib/zx/clock.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <zircon/threads.h>

#include <algorithm>
#include <memory>
#include <random>

#include <fbl/auto_lock.h>

#include "global_stats.h"
#include "random.h"
#include "sync_obj.h"

TestThread::TestThread(const TestThreadBehavior& behavior, zx::profile profile)
    : behavior_(behavior), profile_(std::move(profile)) {
  ZX_ASSERT(behavior_.path_len_dist.min() <= behavior_.path_len_dist.max());
  ZX_ASSERT(behavior_.path_len_dist.max() <= std::size(sync_objs_));
  ZX_ASSERT(std::size(sync_obj_deck_) == std::size(sync_objs_));

  // Build an array of indices which we will shuffle and use as our sync
  // object "deck" when determining which locks we will obtain during an
  // iteration.
  for (size_t i = 0; i < std::size(sync_obj_deck_); ++i) {
    sync_obj_deck_[i] = i;
  }
}

TestThread::~TestThread() { ZX_ASSERT(!thread_.has_value()); }

zx_status_t TestThread::InitStatics() {
  {
    zx::channel channel0, channel1;
    zx_status_t status;

    if ((status = zx::channel::create(0u, &channel0, &channel1)) != ZX_OK) {
      return status;
    }

    if ((status = fdio_service_connect(
             (std::string("/svc/") + fuchsia::scheduler::ProfileProvider::Name_).c_str(),
             channel0.release())) != ZX_OK) {
      return status;
    }

    profile_provider_ =
        std::make_unique<fuchsia::scheduler::ProfileProvider_SyncProxy>(std::move(channel1));
  }

  // Create the proper number of mutexes and cond_vars, then shuffle the array
  // of object pointers so that the acquisition ordering requirements are
  // randomized.
  for (size_t i = 0; i < std::size(sync_objs_); ++i) {
    if (i < kNumMutexes) {
      sync_objs_[i] = std::make_unique<MutexSyncObj>();
    } else {
      sync_objs_[i] = std::make_unique<CondVarSyncObj>();
    }
  }

  Random::Shuffle(sync_objs_);
  return ZX_OK;
}

zx_status_t TestThread::AddThread(const TestThreadBehavior& behavior) {
  zx::profile profile;
  zx_status_t fidl_status;
  zx_status_t status;

  if (behavior.profile_type == ProfileType::Fair) {
    status =
        profile_provider_->GetProfile(behavior.priority, "pi_stress/fair", &fidl_status, &profile);
  } else {
    status =
        profile_provider_->GetDeadlineProfile(behavior.capacity, behavior.deadline, behavior.period,
                                              "pi_stress/deadline", &fidl_status, &profile);
  }

  if (status != ZX_OK) {
    return status;
  }

  if (fidl_status != ZX_OK) {
    return fidl_status;
  }

  threads_.emplace_back(std::unique_ptr<TestThread>(new TestThread(behavior, std::move(profile))));
  thread_dist_ = std::uniform_int_distribution<size_t>{0, threads_.size() - 1};

  return ZX_OK;
}

void TestThread::Shutdown() {
  // Set the global shutdown flag, in addition to all of the CondVarSyncObj
  // shutdown flags.
  shutdown_now_.store(true);
  for (auto& obj : sync_objs_) {
    obj->Shutdown();
  }

  // Join all of our test threads, then destroy them.
  for (auto& t : threads_) {
    t->Join();
  }
  threads_.clear();

  profile_provider_.reset();
}

TestThread& TestThread::random_thread() {
  ZX_ASSERT(threads_.size() > 0);
  return *threads_[Random::Get(thread_dist_)];
}

void TestThread::Start() {
  ZX_ASSERT(!thread_.has_value());
  thread_ = std::thread([](TestThread* thiz) { thiz->Run(); }, this);
}

void TestThread::ChangeProfile() {
  fbl::AutoLock lock{&profile_lock_};
  if (!self_->is_valid()) {
    return;
  }

  // If were borrowing a profile, revert to our base profile.  Otherwise, apply
  // the profile of another thread selected at random.
  zx_status_t status = ZX_ERR_INTERNAL;
  if (profile_borrowed_) {
    status = self_->set_profile(profile_, 0);
    global_stats.profiles_reverted.fetch_add(1u);
  } else {
    status = self_->set_profile(random_thread().profile_, 0);
    global_stats.profiles_changed.fetch_add(1u);
  }

  ZX_ASSERT(status == ZX_OK);
  profile_borrowed_ = !profile_borrowed_;
}

void TestThread::Join() {
  ZX_ASSERT(shutdown_now_.load());
  if (!thread_.has_value()) {
    return;
  }

  thread_->join();
  thread_.reset();
}

void TestThread::HoldLocks(size_t deck_ndx) {
  // Obtain the next sync object in the sequence
  ZX_ASSERT(deck_ndx < path_len_);
  SyncObj& sync_obj = *sync_objs_[sync_obj_deck_[deck_ndx]];

  sync_obj.Acquire(behavior_);

  // Randomly change our profile if needed
  if (Random::RollDice(behavior_.self_profile_change_prob)) {
    ChangeProfile();
  }

  // Choose our linger behavior (intermediate or final) and then linger if we
  // need to.
  size_t next_ndx = deck_ndx + 1;
  const bool intermediate = (next_ndx != path_len_);
  LingerBehavior& lb = intermediate ? behavior_.intermediate_linger : behavior_.final_linger;

  if (Random::RollDice(lb.linger_probability)) {
    zx::duration linger_time{Random::Get(lb.time_dist)};

    if (Random::RollDice(lb.spin_probability)) {
      zx::time deadline = zx::deadline_after(linger_time);
      while (zx::clock::get_monotonic() < deadline) {
      }
      (intermediate ? global_stats.intermediate_spins : global_stats.final_spins).fetch_add(1u);
    } else {
      zx::nanosleep(zx::deadline_after(linger_time));
      (intermediate ? global_stats.intermediate_sleeps : global_stats.final_sleeps).fetch_add(1u);
    }
  }

  // If we have not hit the end, recurse and obtain the next sync object
  if (intermediate) {
    HoldLocks(next_ndx);
  }

  sync_obj.Release();
}

void TestThread::Run() {
  // Set our profile.
  {
    fbl::AutoLock profile_lock{&profile_lock_};
    ZX_ASSERT(!self_->is_valid());

    self_ = zx::unowned_thread{thrd_get_zx_handle(thrd_current())};
    zx_status_t status = self_->set_profile(profile_, 0);
    ZX_ASSERT(status == ZX_OK);

    profile_borrowed_ = false;
  }

  while (!shutdown_now_.load()) {
    // Shuffle the deck
    Random::Shuffle(sync_obj_deck_);

    // Determine how long our path for this pass will be, then sort the top
    // lock_path elements in the deck so that we don't ever have an A/B vs. B/A
    // lock ordering problem.
    path_len_ = Random::Get(behavior_.path_len_dist);
    std::sort(sync_obj_deck_.begin(), sync_obj_deck_.begin() + path_len_);

    // Recursively obtain the sync objects, lingering when we hit the final one, then
    // release.
    HoldLocks();
  }
}
