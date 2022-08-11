// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_COMPONENT_MANAGER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_COMPONENT_MANAGER_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/types.h>

#include <set>

#include "src/developer/debug/debug_agent/component_manager.h"
#include "src/developer/debug/debug_agent/system_interface.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace debug_agent {

class ZirconComponentManager : public ComponentManager, public fuchsia::sys2::EventStream {
 public:
  ZirconComponentManager(SystemInterface* system_interface,
                         std::shared_ptr<sys::ServiceDirectory> services);
  ~ZirconComponentManager() override = default;

  // ComponentManager implementation.
  std::optional<debug_ipc::ComponentInfo> FindComponentInfo(zx_koid_t job_koid) const override;
  debug::Status LaunchComponent(const std::vector<std::string>& argv) override;
  bool OnProcessStart(const ProcessHandle& process, StdioHandles* out_stdio) override;

  // fuchsia::sys2::EventStream implementation.
  void OnEvent(fuchsia::sys2::Event event) override;

  // (For test only) Set the callback that will be invoked when the initialization is ready.
  // If the initialization is already done, callback will still be invoked in the message loop.
  void SetReadyCallback(fit::callback<void()> callback);

 private:
  debug::Status LaunchV1Component(const std::vector<std::string>& argv);
  debug::Status LaunchV2Component(const std::vector<std::string>& argv);

  fit::callback<void()> ready_callback_ = []() {};

  std::shared_ptr<sys::ServiceDirectory> services_;

  // Information of all running components in the system, indexed by their job koids.
  std::map<zx_koid_t, debug_ipc::ComponentInfo> running_component_info_;
  fidl::Binding<fuchsia::sys2::EventStream> event_stream_binding_;

  // Mapping from the process names to the stdio handles of v1 components that have been launched
  // but haven't been seen by |OnProcessStart|.
  std::map<std::string, StdioHandles> expected_v1_components_;

  // Monikers of v2 components we're expecting.
  // There's no way to set stdio handle for v2 components yet.
  std::set<std::string> expected_v2_components_;

  fxl::WeakPtrFactory<ZirconComponentManager> weak_factory_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_COMPONENT_MANAGER_H_
