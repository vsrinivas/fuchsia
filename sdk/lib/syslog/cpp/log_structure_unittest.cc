// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/logging_backend.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

namespace syslog {

TEST(StructuredLogging, Log) {
  FX_SLOG(WARNING)("test_log", {"foo"_k = "bar"});

  // TODO(fxbug.dev/57482): Figure out how to verify this appropriately.
}

TEST(StructuredLogging, BackendDirect) {
  syslog_backend::WriteLog(syslog::LOG_WARNING, "foo.cc", 42, "fake tag", "condition",
                           "Log message");
  syslog_backend::WriteLogValue(syslog::LOG_WARNING, "foo.cc", 42, "fake tag", "condition",
                                {"foo"_k = 42});
  // TODO(fxbug.dev/57482): Figure out how to verify this appropriately.
}

}  // namespace syslog
