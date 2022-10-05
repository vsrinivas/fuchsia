// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_LIMBO_PROVIDER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_LIMBO_PROVIDER_H_

#include <fuchsia/exception/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>

#include <map>
#include <vector>

#include "src/developer/debug/debug_agent/limbo_provider.h"

namespace debug_agent {

// In charge of providing access to the ProcessLimbo.
//
// Fuchsia can be configured to keep processes that have excepted in a suspension state, called
// Limbo. This provides the possibility for debuggers to attach to those process way after the
// exception occurred. We call this process Just In Time Debugging (JITD).
class ZirconLimboProvider final : public LimboProvider {
 public:
  explicit ZirconLimboProvider(std::shared_ptr<sys::ServiceDirectory> services);
  virtual ~ZirconLimboProvider() = default;

  // LimboProvider implementation.
  bool Valid() const override { return valid_; }
  bool IsProcessInLimbo(zx_koid_t process_koid) const override;
  const RecordMap& GetLimboRecords() const override { return limbo_; }
  fit::result<debug::Status, RetrievedException> RetrieveException(zx_koid_t process_koid) override;
  debug::Status ReleaseProcess(zx_koid_t process_koid) override;

 private:
  void WatchActive();
  void WatchLimbo();

  bool valid_ = false;

  // Because the Process Limbo uses hanging gets (async callbacks) and this class exposes a
  // synchronous inteface, we need to keep track of the current state in order to be able to
  // return it immediatelly.
  RecordMap limbo_;

  bool is_limbo_active_ = false;

  fuchsia::exception::ProcessLimboPtr connection_;

  std::shared_ptr<sys::ServiceDirectory> services_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_LIMBO_PROVIDER_H_
