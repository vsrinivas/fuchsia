// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_CHECK_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_CHECK_H_

namespace fidl {

// Outputs a formatted check-failure message to stderr, and abort()s.
void LogMessageAndAbort(const char* file, int line, const char* condition, const char* message);

}  // namespace fidl

// TODO(unification): This can be replaced by FX_CHECK(condition) << message
// when //sdk/lib/syslog can be used here.
#define FIDL_CHECK(condition, message)                                     \
  do {                                                                     \
    if (!(condition)) {                                                    \
      ::fidl::LogMessageAndAbort(__FILE__, __LINE__, #condition, message); \
    }                                                                      \
  } while (0)

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_CHECK_H_
