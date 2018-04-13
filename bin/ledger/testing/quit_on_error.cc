// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/quit_on_error.h"

namespace test {
namespace benchmark {

bool QuitOnError(fxl::Closure quit_callback, ledger::Status status,
                 fxl::StringView description) {
  if (status != ledger::Status::OK) {
    FXL_LOG(ERROR) << description << " failed with status " << status << ".";
    quit_callback();
    return true;
  }
  return false;
}

std::function<void(ledger::Status)> QuitOnErrorCallback(
    fxl::Closure quit_callback,
    std::string description) {
  return [quit_callback = std::move(quit_callback), description](ledger::Status status) {
    QuitOnError(quit_callback, status, description);
  };
}

}  // namespace benchmark
}  // namespace test
