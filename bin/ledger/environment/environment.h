// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_ENVIRONMENT_ENVIRONMENT_H_
#define APPS_LEDGER_SRC_ENVIRONMENT_ENVIRONMENT_H_

#include <thread>

#include "apps/ledger/src/coroutine/coroutine.h"
#include "apps/ledger/src/network/network_service.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/tasks/task_runner.h"

namespace ledger {

// Environment for the ledger application.
class Environment {
 public:
  Environment(ftl::RefPtr<ftl::TaskRunner> main_runner,
              NetworkService* network_service,
              ftl::RefPtr<ftl::TaskRunner> io_runner = nullptr);
  ~Environment();

  const ftl::RefPtr<ftl::TaskRunner> main_runner() { return main_runner_; }
  NetworkService* network_service() { return network_service_; }
  coroutine::CoroutineService* coroutine_service() {
    return coroutine_service_.get();
  }

  // Returns a TaskRunner allowing to access the I/O thread. The I/O thread
  // should be used to access the file system.
  const ftl::RefPtr<ftl::TaskRunner> GetIORunner();

  // Flags only for testing.
  void SetTriggerCloudErasedForTesting();

  bool TriggerCloudErasedForTesting() {
    return trigger_cloud_erased_for_testing_;
  }

 private:
  ftl::RefPtr<ftl::TaskRunner> main_runner_;
  NetworkService* const network_service_;
  std::unique_ptr<coroutine::CoroutineService> coroutine_service_;

  std::thread io_thread_;
  ftl::RefPtr<ftl::TaskRunner> io_runner_;

  // Flags only for testing.
  bool trigger_cloud_erased_for_testing_ = false;

  FTL_DISALLOW_COPY_AND_ASSIGN(Environment);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_ENVIRONMENT_ENVIRONMENT_H_
