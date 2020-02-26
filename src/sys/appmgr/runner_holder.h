// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_RUNNER_HOLDER_H_
#define SRC_SYS_APPMGR_RUNNER_HOLDER_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/vmo.h>

#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/sys/appmgr/component_container.h"
#include "src/sys/appmgr/component_controller_impl.h"
#include "src/sys/appmgr/namespace.h"

namespace component {

class Realm;

class RunnerHolder : public ComponentContainer<ComponentBridge> {
 public:
  RunnerHolder(std::shared_ptr<sys::ServiceDirectory> services,
               fuchsia::sys::ComponentControllerPtr controller,
               fuchsia::sys::LaunchInfo launch_info, Realm* realm,
               fit::function<void()> error_handler = nullptr);
  ~RunnerHolder();

  void StartComponent(fuchsia::sys::Package package, fuchsia::sys::StartupInfo startup_info,
                      fxl::RefPtr<Namespace> ns,
                      fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller);

  std::shared_ptr<ComponentBridge> ExtractComponent(ComponentBridge* controller) override;

  const std::unordered_map<ComponentBridge*, std::shared_ptr<ComponentBridge>>& components() const {
    return components_;
  }

 private:
  void CreateComponentCallback(std::weak_ptr<ComponentControllerImpl> component);
  void Cleanup();

  std::shared_ptr<sys::ServiceDirectory> services_;
  fuchsia::sys::ComponentControllerPtr controller_;
  fuchsia::sys::RunnerPtr runner_;
  std::weak_ptr<ComponentControllerImpl> impl_object_;
  fit::function<void()> error_handler_;
  std::unordered_map<ComponentBridge*, std::shared_ptr<ComponentBridge>> components_;
  uint64_t component_id_counter_;
  std::string koid_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RunnerHolder);
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_RUNNER_HOLDER_H_
