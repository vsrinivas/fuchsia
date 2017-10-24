// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_ENVIRONMENT_ENVIRONMENT_H_
#define PERIDOT_BIN_LEDGER_ENVIRONMENT_ENVIRONMENT_H_

#include <thread>

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/tasks/task_runner.h"
#include "peridot/bin/ledger/coroutine/coroutine.h"

namespace ledger {

// Environment for the ledger application.
class Environment {
 public:
  Environment(fxl::RefPtr<fxl::TaskRunner> main_runner,
              fxl::RefPtr<fxl::TaskRunner> io_runner = nullptr);
  ~Environment();

  const fxl::RefPtr<fxl::TaskRunner> main_runner() { return main_runner_; }

  coroutine::CoroutineService* coroutine_service() {
    return coroutine_service_.get();
  }

  // Returns a TaskRunner allowing to access the I/O thread. The I/O thread
  // should be used to access the file system.
  const fxl::RefPtr<fxl::TaskRunner> GetIORunner();

 private:
  fxl::RefPtr<fxl::TaskRunner> main_runner_;
  std::unique_ptr<coroutine::CoroutineService> coroutine_service_;

  std::thread io_thread_;
  fxl::RefPtr<fxl::TaskRunner> io_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Environment);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_ENVIRONMENT_ENVIRONMENT_H_
