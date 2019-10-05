// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#pragma once

#include <fuchsia/exception/cpp/fidl.h>

#include <optional>
#include <vector>

namespace debug_agent {

// In charge of providing access to the ProcessLimbo.
//
// Fuchsia can be configured to keep processes that have excepted in a suspension state, called
// Limbo. This provides the possibility for debuggers to attach to those process way after the
// exception occurred. We call this process Just In Time Debugging (JITD).
class LimboProvider {
 public:
  virtual ~LimboProvider();
  virtual zx_status_t ListProcessesOnLimbo(
      std::vector<fuchsia::exception::ProcessExceptionMetadata>* out);
};

}  // namespace debug_agent
