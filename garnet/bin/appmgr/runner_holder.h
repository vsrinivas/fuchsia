// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_RUNNER_HOLDER_H_
#define GARNET_BIN_APPMGR_RUNNER_HOLDER_H_

#include <lib/fit/function.h>
#include <lib/zx/vmo.h>

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/memory/ref_ptr.h>
#include <lib/sys/cpp/service_directory.h>
#include "garnet/bin/appmgr/component_container.h"
#include "garnet/bin/appmgr/component_controller_impl.h"
#include "garnet/bin/appmgr/namespace.h"
#include "src/lib/files/unique_fd.h"

namespace component {

class Realm;

class RunnerHolder : public ComponentContainer<ComponentBridge> {
 public:
  RunnerHolder(std::shared_ptr<sys::ServiceDirectory> services,
               fuchsia::sys::ComponentControllerPtr controller,
               fuchsia::sys::LaunchInfo launch_info, Realm* realm,
               fit::function<void()> error_handler = nullptr);
  ~RunnerHolder();

  void StartComponent(
      fuchsia::sys::Package package, fuchsia::sys::StartupInfo startup_info,
      fxl::RefPtr<Namespace> ns,
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller,
      TerminationCallback termination_callback);

  std::unique_ptr<ComponentBridge> ExtractComponent(
      ComponentBridge* controller) override;

 private:
  void CreateComponentCallback(ComponentControllerImpl* component);
  void Cleanup();

  std::shared_ptr<sys::ServiceDirectory> services_;
  fuchsia::sys::ComponentControllerPtr controller_;
  fuchsia::sys::RunnerPtr runner_;
  ComponentControllerImpl* impl_object_;
  fit::function<void()> error_handler_;
  std::unordered_map<ComponentBridge*, std::unique_ptr<ComponentBridge>>
      components_;
  uint64_t component_id_counter_;
  std::string koid_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RunnerHolder);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_RUNNER_HOLDER_H_
