// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/benchmark/lib/logging.h"

#include "lib/fsl/tasks/message_loop.h"

namespace test {
namespace benchmark {

bool QuitOnError(ledger::Status status, fxl::StringView description) {
  if (status != ledger::Status::OK) {
    FXL_LOG(ERROR) << description << " failed with status " << status << ".";
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
    return true;
  }
  return false;
}

std::function<void(ledger::Status)> QuitOnErrorCallback(
    std::string description) {
  return [description](ledger::Status status) {
    QuitOnError(status, description);
  };
}

}  // namespace benchmark
}  // namespace test
