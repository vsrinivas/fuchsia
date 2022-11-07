// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/story_runner/module_controller_impl.h"

#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fidl/cpp/interface_request.h>

#include "lib/ui/scenic/cpp/view_ref_pair.h"
#include "src/modular/bin/sessionmgr/storage/encode_module_path.h"
#include "src/modular/lib/common/teardown.h"
#include "src/modular/lib/fidl/clone.h"

namespace modular {

ModuleControllerImpl::ModuleControllerImpl(fuchsia::sys::Launcher* const launcher,
                                           fuchsia::modular::session::AppConfig module_config,
                                           const fuchsia::modular::ModuleData* const module_data,
                                           fuchsia::sys::ServiceListPtr service_list,
                                           ModuleControllerImplViewParams view_params)
    : app_client_(launcher, CloneStruct(module_config), std::move(service_list)),
      module_data_(module_data) {
  app_client_.SetAppErrorHandler([this] { OnAppConnectionError(); });

  fuchsia::ui::app::ViewProviderPtr view_provider;
  app_client_.services().Connect(view_provider.NewRequest());

  if (std::holds_alternative<fuchsia::ui::views::ViewCreationToken>(view_params)) {
    fuchsia::ui::app::CreateView2Args args;
    args.set_view_creation_token(
        std::move(std::get<fuchsia::ui::views::ViewCreationToken>(view_params)));
    view_provider->CreateView2(std::move(args));
  } else {
    auto& view_pair =
        std::get<std::pair<fuchsia::ui::views::ViewToken, scenic::ViewRefPair>>(view_params);
    view_provider->CreateViewWithViewRef(std::move(view_pair.first.value),
                                         std::move(view_pair.second.control_ref),
                                         std::move(view_pair.second.view_ref));
  }
}

ModuleControllerImpl::~ModuleControllerImpl() = default;

// If the ComponentController connection closes, it means the module cannot be
// started. We indicate this by the ERROR state.
void ModuleControllerImpl::OnAppConnectionError() {
  FX_LOGS(WARNING) << "Module " << EncodeModulePath(module_data_->module_path()) << " (URL "
                   << module_data_->module_url() << ") terminated unexpectedly.";
}

void ModuleControllerImpl::Teardown(fit::function<void()> done) {
  // At this point, it's no longer an error if the module closes its
  // connection, or the application exits.
  app_client_.SetAppErrorHandler(nullptr);

  // Tear down the module application through the normal procedure with timeout.
  app_client_.Teardown(kBasicTimeout, std::move(done));
}

}  // namespace modular
