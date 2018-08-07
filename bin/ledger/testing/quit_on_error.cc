// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/quit_on_error.h"

#include <lib/fit/function.h>

namespace ledger {

bool QuitOnError(fit::closure quit_callback, Status status,
                 fxl::StringView description) {
  if (status != Status::OK) {
    FXL_LOG(ERROR) << description << " failed with status "
                   << fidl::ToUnderlying(status) << ".";
    quit_callback();
    return true;
  }
  return false;
}

fit::function<void(Status)> QuitOnErrorCallback(fit::closure quit_callback,
                                                std::string description) {
  return [quit_callback = std::move(quit_callback),
          description = std::move(description)](Status status) mutable {
    QuitOnError(quit_callback.share(), status, description);
  };
}

}  // namespace ledger
