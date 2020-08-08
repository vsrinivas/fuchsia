// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_MODULE_CONTROLLER_IMPL_H_
#define SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_MODULE_CONTROLLER_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>

#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/modular/lib/fidl/app_client.h"

namespace modular {

class StoryControllerImpl;

// Manages the lifecycle of a single Module.
class ModuleControllerImpl {
 public:
  ModuleControllerImpl(fuchsia::sys::Launcher* launcher,
                       fuchsia::modular::session::AppConfig module_config,
                       const fuchsia::modular::ModuleData* module_data,
                       fuchsia::sys::ServiceListPtr service_list,
                       fuchsia::ui::views::ViewToken view_token, scenic::ViewRefPair view_ref_pair);
  ~ModuleControllerImpl();

  // Calls Teardown() on the AppClient of the module component instance,
  // notifies state change, and then calls |done|.
  void Teardown(fit::function<void()> done);

  component::Services& services() { return app_client_.services(); }

 private:
  // Used as application error handler on the Module app client.
  void OnAppConnectionError();

  AppClient<fuchsia::modular::Lifecycle> app_client_;

  // The Module path and other information about the module instance.
  const fuchsia::modular::ModuleData* const module_data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleControllerImpl);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_MODULE_CONTROLLER_IMPL_H_
