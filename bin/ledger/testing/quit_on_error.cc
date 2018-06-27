// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/quit_on_error.h"

#include <lib/fit/function.h>

#include "lib/fxl/functional/make_copyable.h"

namespace test {
namespace benchmark {

bool QuitOnError(fit::closure quit_callback, ledger::Status status,
                 fxl::StringView description) {
  if (status != ledger::Status::OK) {
    FXL_LOG(ERROR) << description << " failed with status " << status << ".";
    quit_callback();
    return true;
  }
  return false;
}

fit::function<void(ledger::Status)> QuitOnErrorCallback(
    fit::closure quit_callback, std::string description) {
  return fxl::MakeCopyable(
      [quit_callback = std::move(quit_callback),
       description = std::move(description)](ledger::Status status) mutable {
        QuitOnError(quit_callback.share(), status, description);
      });
}

}  // namespace benchmark
}  // namespace test
