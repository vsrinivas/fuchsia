// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include <fuchsia/sys/cpp/fidl.h>
#include <zircon/types.h>

#include "lib/sys/cpp/service_directory.h"
#include "src/developer/debug/shared/component_utils.h"

namespace debug_agent {

// When preparing a component, this is information the debugger will use in
// order to be able to attach to the newly starting process.
struct ComponentDescription {
  uint64_t component_id = 0;  // 0 is invalid.
  std::string url;
  std::string process_name;
  std::string filter;
};

struct ComponentHandles {
  zx::socket out;
  zx::socket err;
};

// Class designed to help setup a component and then launch it. These setps are
// necessary because the agent needs some information about how the component
// will be launch before it actually launches it. This is because the debugger
// will set itself to "catch" the component when it starts as a process.
class ComponentLauncher {
 public:
  explicit ComponentLauncher(std::shared_ptr<sys::ServiceDirectory> services);

  // Will fail if |argv| is invalid. The first element should be the component
  // url needed to launch.
  zx_status_t Prepare(std::vector<std::string> argv,
                      ComponentDescription* description,
                      ComponentHandles* handles);

  // The launcher has to be already successfully prepared.
  // The lifetime of the controller is bound to the lifetime of the component.
  fuchsia::sys::ComponentControllerPtr Launch();

 private:
  std::shared_ptr<sys::ServiceDirectory> services_;
  fuchsia::sys::LaunchInfo launch_info_;
};

}  // namespace debug_agent
