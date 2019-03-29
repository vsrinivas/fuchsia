// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_QUIT_ON_ERROR_H_
#define SRC_LEDGER_BIN_TESTING_QUIT_ON_ERROR_H_

#include <functional>
#include <string>

#include <lib/fit/function.h>
#include <src/lib/fxl/strings/string_view.h>

#include "src/ledger/bin/fidl/include/types.h"

namespace ledger {

// Logs an error and calls |quit_callback| which quits a related message loop if
// the given ledger status is not Status::OK. Returns true if the loop
// was quit .
bool QuitOnError(fit::closure quit_callback, Status status,
                 fxl::StringView description);

fit::function<void(Status)> QuitOnErrorCallback(fit::closure quit_callback,
                                                std::string description);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_QUIT_ON_ERROR_H_
