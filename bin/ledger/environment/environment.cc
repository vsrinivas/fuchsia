// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/environment/environment.h"

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/threading/create_thread.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"

namespace ledger {

Environment::Environment(fxl::RefPtr<fxl::TaskRunner> main_runner,
                         fxl::RefPtr<fxl::TaskRunner> io_runner)
    : main_runner_(std::move(main_runner)),
      coroutine_service_(std::make_unique<coroutine::CoroutineServiceImpl>()),
      io_runner_(std::move(io_runner)) {
  FXL_DCHECK(main_runner_);
}

Environment::~Environment() {
  if (io_thread_.joinable()) {
    io_runner_->PostTask([] { fsl::MessageLoop::GetCurrent()->QuitNow(); });
    io_thread_.join();
  }
}

const fxl::RefPtr<fxl::TaskRunner> Environment::GetIORunner() {
  if (!io_runner_) {
    io_thread_ = fsl::CreateThread(&io_runner_, "io thread");
  }
  return io_runner_;
}

void Environment::SetTriggerCloudErasedForTesting() {
  FXL_LOG(WARNING)
      << "Setting up the environment to trigger cloud erased recovery: "
      << "THIS SHOULD ONLY HAPPEN IN TESTS";
  trigger_cloud_erased_for_testing_ = true;
}

}  // namespace ledger
