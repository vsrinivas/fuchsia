// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/runner_holder.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <utility>

#include "garnet/bin/appmgr/component_controller_impl.h"
#include "garnet/bin/appmgr/realm.h"
#include "garnet/bin/appmgr/util.h"
#include "lib/fsl/vmo/file.h"

using fuchsia::sys::TerminationReason;

namespace component {

RunnerHolder::RunnerHolder(Services services,
                           fuchsia::sys::ComponentControllerPtr controller,
                           fuchsia::sys::LaunchInfo launch_info, Realm* realm,
                           std::function<void()> error_handler)
    : services_(std::move(services)),
      controller_(std::move(controller)),
      impl_object_(nullptr),
      error_handler_(error_handler),
      component_id_counter_(0) {
  realm->CreateComponent(std::move(launch_info), controller_.NewRequest(),
                         [this](ComponentControllerImpl* component) {
                           CreateComponentCallback(component);
                         });

  controller_.events().OnTerminated =
      [this](int64_t return_code, TerminationReason termination_reason) {
        if (termination_reason != TerminationReason::EXITED) {
          FXL_LOG(ERROR) << "Runner terminating, status " << termination_reason;
        }
        Cleanup();

        if (error_handler_) {
          error_handler_();
        }
      };

  services_.ConnectToService(runner_.NewRequest());
}

RunnerHolder::~RunnerHolder() = default;

void RunnerHolder::Cleanup() {
  impl_object_ = nullptr;
  // Terminate all bridges currently owned by this runner.
  for (auto& component : components_) {
    component.second->SetTerminationReason(
        TerminationReason::RUNNER_TERMINATED);
  }
  components_.clear();
}

void RunnerHolder::CreateComponentCallback(ComponentControllerImpl* component) {
  impl_object_ = component;
  koid_ = component->koid();

  // add error handler
  impl_object_->Wait([this](int exit_code) { Cleanup(); });

  // update hub
  for (auto& n : components_) {
    n.second->SetParentJobId(koid_);
    impl_object_->AddSubComponentHub(n.second->HubInfo());
  }
}

void RunnerHolder::StartComponent(
    fuchsia::sys::Package package, fuchsia::sys::StartupInfo startup_info,
    fxl::RefPtr<Namespace> ns,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller,
    TerminationCallback termination_callback) {
  auto url = startup_info.launch_info.url;
  const std::string args =
      Util::GetArgsString(startup_info.launch_info.arguments);
  auto channels = Util::BindDirectory(&startup_info.launch_info);

  fuchsia::sys::ComponentControllerPtr remote_controller;
  auto remote_controller_request = remote_controller.NewRequest();

  // TODO(anmittal): Create better unique instance id, instead of 1,2,3,4,...
  auto component = std::make_unique<ComponentBridge>(
      std::move(controller), std::move(remote_controller), this, url,
      std::move(args), Util::GetLabelFromURL(url),
      std::to_string(++component_id_counter_), std::move(ns),
      ExportedDirType::kLegacyFlatLayout, std::move(channels.exported_dir),
      std::move(channels.client_request), std::move(termination_callback));

  // update hub
  if (impl_object_) {
    component->SetParentJobId(koid_);
    impl_object_->AddSubComponentHub(component->HubInfo());
  }

  ComponentBridge* key = component.get();
  components_.emplace(key, std::move(component));

  runner_->StartComponent(std::move(package), std::move(startup_info),
                          std::move(remote_controller_request));
}

std::unique_ptr<ComponentBridge> RunnerHolder::ExtractComponent(
    ComponentBridge* controller) {
  auto it = components_.find(controller);
  if (it == components_.end()) {
    return nullptr;
  }
  auto component = std::move(it->second);

  // update hub
  if (impl_object_) {
    impl_object_->RemoveSubComponentHub(component->HubInfo());
  }

  components_.erase(it);
  return component;
}

}  // namespace component
