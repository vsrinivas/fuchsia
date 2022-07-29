// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/tests/fidl/client_suite/harness/harness.h"

#include <sstream>

namespace client_suite::internal {

void Finisher::Finish(FinishRequest& request, FinishCompleter::Sync& completer) {
  ZX_ASSERT(!finished_);
  completer.Reply(errors_);
  finished_ = true;
}

static std::unordered_map<uint32_t, TestHandlerFunc> test_handlers;

bool RegisterTestHandler(uint32_t key, TestHandlerFunc value) {
  test_handlers[key] = std::move(value);
  return true;
}

TestHandlerFunc LookupTestHandler(fidl_clientsuite::Test test) {
  auto it = test_handlers.find(test);
  ZX_ASSERT_MSG(it != test_handlers.end(), "test handler not registered");
  return std::move(it->second);
}

void ReportVerificationFailure(Finisher& finisher, std::string_view file, int line,
                               std::string_view cond, std::string_view message) {
  std::ostringstream stream;
  stream << file << ":" << line << " " << cond;
  if (!message.empty()) {
    stream << " " << message;
  }
  FX_LOGS(ERROR) << "error in harness: " << stream.str() << std::endl;
  finisher.AddError({stream.str()});
}

}  // namespace client_suite::internal
