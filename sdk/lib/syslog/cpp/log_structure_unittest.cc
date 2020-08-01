// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

namespace syslog {

TEST(StructuredLogging, Log) {
  FX_SLOG(WARNING)("test_log", {"foo"_k = "bar"});

  // TODO(57482): Figure out how to verify this appropriately.
}

}  // namespace syslog
