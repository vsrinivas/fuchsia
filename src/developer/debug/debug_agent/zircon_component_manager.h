// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_COMPONENT_MANAGER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_COMPONENT_MANAGER_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>

#include "src/developer/debug/debug_agent/component_manager.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace debug_agent {

class ZirconComponentManager : public ComponentManager {
 public:
  struct ComponentDescription {
    uint64_t component_id = 0;  // 0 is invalid.
    std::string url;
    std::string process_name;
    std::string filter;
  };

  explicit ZirconComponentManager(std::shared_ptr<sys::ServiceDirectory> services);
  ~ZirconComponentManager() override = default;

  // ComponentManager implementation.
  zx_status_t LaunchComponent(DebuggedJob* root_job, const std::vector<std::string>& argv,
                              uint64_t* component_id) override;
  uint64_t OnProcessStart(const std::string& filter, StdioHandles& out_stdio) override;

 private:
  void OnComponentTerminated(int64_t return_code, const ComponentDescription& description,
                             fuchsia::sys::TerminationReason reason);

  std::shared_ptr<sys::ServiceDirectory> services_;

  // Each component launch is assigned an unique filter and id. This is because new components are
  // attached via the job filter mechanism. When a particular filter attached, we use this id to
  // know which component launch just happened and we can communicate it to the client.
  struct ExpectedComponent {
    ComponentDescription description;
    StdioHandles handles;
    fuchsia::sys::ComponentControllerPtr controller;
  };
  std::map<std::string, ExpectedComponent> expected_components_;

  // References to the running components. These need to be kept alive to keep the components
  // running.
  std::map<uint64_t, fuchsia::sys::ComponentControllerPtr> running_components_;

  fxl::WeakPtrFactory<ZirconComponentManager> weak_factory_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ZIRCON_COMPONENT_MANAGER_H_
