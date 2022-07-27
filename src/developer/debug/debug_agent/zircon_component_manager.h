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

#include "src/developer/debug/debug_agent/component_manager.h"
#include "src/developer/debug/debug_agent/system_interface.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace debug_agent {

class ZirconComponentManager : public ComponentManager, public fuchsia::sys2::EventStream {
 public:
  struct ComponentDescription {
    uint64_t component_id = 0;  // 0 is invalid.
    std::string url;
    std::string filter;
  };

  explicit ZirconComponentManager(std::shared_ptr<sys::ServiceDirectory> services);
  ~ZirconComponentManager() override = default;

  // ComponentManager implementation.
  std::optional<debug_ipc::ComponentInfo> FindComponentInfo(zx_koid_t job_koid) const override;
  debug::Status LaunchComponent(DebugAgent& debug_agent, const std::vector<std::string>& argv,
                                uint64_t* component_id) override;
  uint64_t OnProcessStart(const Filter& filter, StdioHandles& out_stdio) override;

  // fuchsia::sys2::EventStream implementation.
  void OnEvent(fuchsia::sys2::Event event) override;

  // (For test only) Set the callback that will be invoked when the initialization is ready.
  // If the initialization is already done, callback will still be invoked in the message loop.
  void SetReadyCallback(fit::callback<void()> callback);

 private:
  void OnV1ComponentTerminated(int64_t return_code, const ComponentDescription& description,
                               fuchsia::sys::TerminationReason reason);

  fit::callback<void()> ready_callback_ = []() {};

  std::shared_ptr<sys::ServiceDirectory> services_;

  std::map<zx_koid_t, debug_ipc::ComponentInfo> running_component_info_;
  fidl::Binding<fuchsia::sys2::EventStream> event_stream_binding_;

  // Each component launch is assigned an unique filter and id. This is because new components are
  // attached via the job filter mechanism. When a particular filter attached, we use this id to
  // know which component launch just happened and we can communicate it to the client.
  struct ExpectedV1Component {
    ComponentDescription description;
    StdioHandles handles;
    fuchsia::sys::ComponentControllerPtr controller;
  };
  std::map<std::string, ExpectedV1Component> expected_v1_components_;

  // References to the running components. These need to be kept alive to keep the components
  // running.
  std::map<uint64_t, fuchsia::sys::ComponentControllerPtr> running_v1_components_;

  fxl::WeakPtrFactory<ZirconComponentManager> weak_factory_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_COMPONENT_MANAGER_H_
