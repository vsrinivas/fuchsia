// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/environment/environment.h"

#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/create_thread.h"

namespace ledger {

namespace {}  // namespace

Environment::Environment(configuration::Configuration configuration,
                         ftl::RefPtr<ftl::TaskRunner> main_runner,
                         NetworkService* network_service,
                         ftl::RefPtr<ftl::TaskRunner> io_runner)
    : configuration_(std::move(configuration)),
      main_runner_(std::move(main_runner)),
      network_service_(network_service),
      io_runner_(std::move(io_runner)) {}

Environment::~Environment() {
  if (io_thread_.joinable()) {
    io_runner_->PostTask([] { mtl::MessageLoop::GetCurrent()->QuitNow(); });
    io_thread_.join();
  }
}

const ftl::RefPtr<ftl::TaskRunner> Environment::GetIORunner() {
  if (!io_runner_) {
    io_thread_ = mtl::CreateThread(&io_runner_, "io thread");
  }
  return io_runner_;
}

}  // namespace ledger
