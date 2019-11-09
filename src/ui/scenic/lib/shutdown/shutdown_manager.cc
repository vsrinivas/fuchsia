// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/shutdown/shutdown_manager.h"

#include <chrono>
#include <thread>

#include "src/lib/fxl/logging.h"

namespace scenic_impl {

ShutdownManager::ShutdownManager(async_dispatcher_t* dispatcher, fit::closure quit_callback,
                                 fit::closure timeout_callback)
    : executor_(dispatcher),
      quit_callback_(std::move(quit_callback)),
      timeout_callback_(std::move(timeout_callback)),
      shared_bool_(std::make_shared<std::atomic_bool>()) {
  shared_bool_->store(false);
}

ShutdownManager::~ShutdownManager() {
  // Prevent |timeout_callback_| from running, if it hasn't already.  No need to prevent
  // |quit_callback_| from running, since destroying |executor_| will guarantee that.
  bool was_set = shared_bool_->exchange(true);
  if (was_set) {
    // One of the two callbacks already ran.  No need to log any warnings.
    return;
  }

  if (state_ == State::kInit) {
    FXL_LOG(WARNING) << "ShutdownManager destroyed without Shutdown() being called; quit callback "
                        "was not invoked.";
  } else {
    FXL_DCHECK(state_ == State::kShuttingDown);
    FXL_LOG(WARNING) << "ShutdownManager destroyed before shutdown was completed; quit callback "
                        "was not invoked.";
  }
}

void ShutdownManager::RegisterClient(ClientCallback client) {
  FXL_DCHECK(state_ == State::kInit);
  if (state_ != State::kInit) {
    FXL_LOG(WARNING) << "ShutdownManager::RegisterClient(): already shutting down; ignoring.";
    return;
  }
  clients_.push_back(std::move(client));
}

void ShutdownManager::Shutdown(zx::duration timeout) {
  FXL_DCHECK(state_ == State::kInit);
  if (state_ != State::kInit) {
    return;
  }
  state_ = State::kShuttingDown;

  if (clients_.empty()) {
    state_ = State::kFinishedShuttingDown;
    bool was_set = shared_bool_->exchange(true);
    FXL_DCHECK(!was_set);
    quit_callback_();
    return;
  }

  std::vector<fit::promise<>> promises;
  for (auto& callback : clients_) {
    promises.push_back(callback());
  }
  clients_.clear();

  // NOTE: It's OK to capture |this| because the closure won't run if |exectutor_| is destroyed.
  auto joined_promise = fit::join_promise_vector(std::move(promises))
                            .and_then([this](std::vector<fit::result<>>& results) {
                              bool was_set = shared_bool_->exchange(true);
                              if (!was_set) {
                                FXL_DCHECK(state_ == State::kShuttingDown);
                                state_ = State::kFinishedShuttingDown;
                                quit_callback_();
                              }
                            });
  executor_.schedule_task(std::move(joined_promise));

  std::thread timeout_thread(
      [timeout, shared_bool = shared_bool_, cb = std::move(timeout_callback_)] {
        std::this_thread::sleep_for(std::chrono::nanoseconds(timeout.get()));
        bool was_set = shared_bool->exchange(true);
        if (!was_set) {
          cb();
        }
      });
  timeout_thread.detach();
};

}  // namespace scenic_impl
