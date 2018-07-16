// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/agent/cpp/agent_impl.h>

#include <fs/service.h>

namespace modular {

AgentImpl::AgentImpl(component::ServiceNamespace* const service_namespace,
                     Delegate* const delegate)
    : delegate_(delegate), binding_(this) {
  service_namespace->AddService<fuchsia::modular::Agent>(
      [this](fidl::InterfaceRequest<fuchsia::modular::Agent> request) {
        binding_.Bind(std::move(request));
      });
}

AgentImpl::AgentImpl(fbl::RefPtr<fs::PseudoDir> directory,
                     Delegate* const delegate)
    : delegate_(delegate), binding_(this) {
  directory->AddEntry(
      fuchsia::modular::Agent::Name_,
      fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
        binding_.Bind(std::move(channel));
        return ZX_OK;
      })));
}

// |fuchsia::modular::Agent|
void AgentImpl::Connect(
    fidl::StringPtr requestor_url,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> services_request) {
  delegate_->Connect(std::move(services_request));
}

// |fuchsia::modular::Agent|
void AgentImpl::RunTask(fidl::StringPtr task_id, RunTaskCallback callback) {
  delegate_->RunTask(task_id, callback);
}

}  // namespace modular
