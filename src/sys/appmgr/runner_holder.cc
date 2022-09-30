// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/runner_holder.h"

#include <lib/fit/function.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <utility>

#include "lib/sys/cpp/termination_reason.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/sys/appmgr/component_controller_impl.h"
#include "src/sys/appmgr/realm.h"
#include "src/sys/appmgr/util.h"

using fuchsia::sys::TerminationReason;

namespace component {

RunnerHolder::RunnerHolder(std::shared_ptr<sys::ServiceDirectory> services,
                           fuchsia::sys::ComponentControllerPtr controller,
                           fuchsia::sys::LaunchInfo launch_info, Realm* realm,
                           fit::function<void()> error_handler)
    : services_(std::move(services)),
      controller_(std::move(controller)),
      error_handler_(std::move(error_handler)),
      component_id_counter_(0) {
  auto url = launch_info.url;
  realm->CreateComponent(std::move(launch_info), controller_.NewRequest(),
                         [this](std::weak_ptr<ComponentControllerImpl> component) {
                           CreateComponentCallback(component);
                         });

  controller_.events().OnTerminated = [this, url](int64_t return_code,
                                                  TerminationReason termination_reason) {
    if (termination_reason != TerminationReason::EXITED) {
      FX_LOGS(ERROR) << "Runner (" << url << ") terminating, reason: "
                     << sys::HumanReadableTerminationReason(termination_reason);
    }
    Cleanup();

    if (error_handler_) {
      error_handler_();
    }
  };

  services_->Connect(runner_.NewRequest());
}

RunnerHolder::~RunnerHolder() = default;

void RunnerHolder::Cleanup() {
  impl_object_.reset();
  // Terminate all bridges currently owned by this runner.
  for (auto& component : components_) {
    component.second->SetTerminationReason(TerminationReason::RUNNER_TERMINATED);
  }
  components_.clear();
}

void RunnerHolder::CreateComponentCallback(std::weak_ptr<ComponentControllerImpl> component) {
  impl_object_ = component;

  if (auto impl = impl_object_.lock()) {
    koid_ = impl->koid();
    // update hub
    for (auto& n : components_) {
      n.second->SetParentJobId(koid_);
      impl->AddSubComponentHub(n.second->HubInfo());
    }
  }
}

void RunnerHolder::StartComponent(
    fuchsia::sys::Package package, fuchsia::sys::StartupInfo startup_info,
    fxl::RefPtr<Namespace> ns, fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller,
    std::optional<zx::channel> package_handle) {
  auto url = startup_info.launch_info.url;
  const std::string args = Util::GetArgsString(startup_info.launch_info.arguments);
  auto channels = Util::BindDirectory(&startup_info.launch_info);

  fuchsia::sys::ComponentControllerPtr remote_controller;
  auto remote_controller_request = remote_controller.NewRequest();

  fxl::RefPtr<Namespace> ns_copy(ns);

  // TODO(anmittal): Create better unique instance id, instead of 1,2,3,4,...
  auto id = std::to_string(++component_id_counter_);
  ns->set_component_id(id);
  auto component = std::make_shared<ComponentBridge>(
      std::move(controller), std::move(remote_controller), this, url, std::move(args),
      Util::GetLabelFromURL(url), id, std::move(ns), std::move(channels.exported_dir),
      std::move(channels.client_request), std::move(package_handle));

  // update hub
  if (auto impl = impl_object_.lock()) {
    component->SetParentJobId(koid_);
    impl->AddSubComponentHub(component->HubInfo());
  }

  ns_copy->NotifyComponentStarted(component->url(), component->label(),
                                  component->hub_instance_id());

  ComponentBridge* key = component.get();
  components_.emplace(key, std::move(component));

  runner_->StartComponent(std::move(package), std::move(startup_info),
                          std::move(remote_controller_request));
}

std::shared_ptr<ComponentBridge> RunnerHolder::ExtractComponent(ComponentBridge* controller) {
  auto it = components_.find(controller);
  if (it == components_.end()) {
    return nullptr;
  }
  auto component = std::move(it->second);

  component->NotifyStopped();

  // update hub
  if (auto impl = impl_object_.lock()) {
    impl->RemoveSubComponentHub(component->HubInfo());
  }

  components_.erase(it);
  return component;
}

}  // namespace component
