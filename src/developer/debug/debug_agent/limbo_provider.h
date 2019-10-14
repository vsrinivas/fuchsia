// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_LIMBO_PROVIDER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_LIMBO_PROVIDER_H_

#include <fuchsia/exception/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>

#include <map>
#include <optional>
#include <vector>

#include "src/lib/fxl/memory/weak_ptr.h"

namespace debug_agent {

// In charge of providing access to the ProcessLimbo.
//
// Fuchsia can be configured to keep processes that have excepted in a suspension state, called
// Limbo. This provides the possibility for debuggers to attach to those process way after the
// exception occurred. We call this process Just In Time Debugging (JITD).
class LimboProvider {
 public:
  explicit LimboProvider(std::shared_ptr<sys::ServiceDirectory> services);
  virtual ~LimboProvider();

  virtual zx_status_t ListProcessesOnLimbo(
      std::vector<fuchsia::exception::ProcessExceptionMetadata>* out);

 private:
  std::shared_ptr<sys::ServiceDirectory> services_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_LIMBO_PROVIDER_H_
