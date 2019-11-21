// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_QUIT_ON_ERROR_H_
#define SRC_LEDGER_BIN_TESTING_QUIT_ON_ERROR_H_

#include <lib/fit/function.h>

#include <functional>
#include <string>

#include "fuchsia/ledger/cpp/fidl.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/public/status.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

namespace internal {
// Internal class to be able to take both kind of status.
class StatusTranslater {
 public:
  // Implicit to be able to take both status in |QuitOnError| and
  // |QuitOnErrorCallback|.
  StatusTranslater(Status status);
  StatusTranslater(const fuchsia::ledger::Page_CreateReferenceFromBuffer_Result& result);
  StatusTranslater(zx_status_t status);
  StatusTranslater(const fuchsia::ledger::PageSnapshot_Get_Result& result);
  StatusTranslater(const fuchsia::ledger::PageSnapshot_GetInline_Result& result);
  StatusTranslater(const fuchsia::ledger::PageSnapshot_Fetch_Result& result);
  StatusTranslater(const fuchsia::ledger::PageSnapshot_FetchPartial_Result& result);

  bool ok() { return ok_; };
  const std::string& description() { return description_; }

 private:
  bool ok_;
  std::string description_;
};
}  // namespace internal

// Logs an error and calls |quit_callback| which quits a related message loop if
// the given ledger status is not Status::OK. Returns true if the loop
// was quit .
bool QuitOnError(fit::closure quit_callback, internal::StatusTranslater status,
                 absl::string_view description);

inline auto QuitOnErrorCallback(fit::closure quit_callback, std::string description) {
  return [quit_callback = std::move(quit_callback),
          description = std::move(description)](internal::StatusTranslater status) mutable {
    QuitOnError(quit_callback.share(), status, description);
  };
}

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_QUIT_ON_ERROR_H_
