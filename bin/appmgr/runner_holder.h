// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_RUNNER_HOLDER_H_
#define GARNET_BIN_APPMGR_RUNNER_HOLDER_H_

#include <lib/zx/vmo.h>

#include <fuchsia/sys/cpp/fidl.h>
#include "garnet/bin/appmgr/component_container.h"
#include "garnet/bin/appmgr/component_controller_impl.h"
#include "garnet/bin/appmgr/namespace.h"
#include "garnet/lib/farfs/file_system.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/svc/cpp/services.h"

namespace fuchsia {
namespace sys {

class Realm;

class RunnerHolder : public ComponentContainer<ComponentBridge> {
 public:
  RunnerHolder(Services services, ComponentControllerPtr controller,
               LaunchInfo launch_info, Realm* realm,
               std::function<void()> error_handler = nullptr);
  ~RunnerHolder();

  void StartComponent(Package package, StartupInfo startup_info,
                      std::unique_ptr<archive::FileSystem> file_system,
                      fxl::RefPtr<Namespace> ns,
                      fidl::InterfaceRequest<ComponentController> controller);

  std::unique_ptr<ComponentBridge> ExtractComponent(
      ComponentBridge* controller) override;

 private:
  void CreateComponentCallback(ComponentControllerImpl* component);
  void Cleanup();

  Services services_;
  ComponentControllerPtr controller_;
  RunnerPtr runner_;
  ComponentControllerImpl* impl_object_;
  std::function<void()> error_handler_;
  std::unordered_map<ComponentBridge*, std::unique_ptr<ComponentBridge>>
      components_;
  uint64_t component_id_counter_;
  std::string koid_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RunnerHolder);
};

}  // namespace sys
}  // namespace fuchsia

#endif  // GARNET_BIN_APPMGR_RUNNER_HOLDER_H_
