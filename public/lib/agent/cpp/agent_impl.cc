// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/agent/cpp/agent_impl.h"

namespace modular {

AgentImpl::AgentImpl(app::ServiceNamespace* const service_namespace,
                     Delegate* const delegate)
    : delegate_(delegate), binding_(this) {
  service_namespace->AddService<Agent>(
      [this](fidl::InterfaceRequest<Agent> request) {
        binding_.Bind(std::move(request));
      });
}

// |Agent|
void AgentImpl::Initialize() {}

// |Agent|
void AgentImpl::Connect(
    const fidl::String& requestor_url,
    fidl::InterfaceRequest<app::ServiceProvider> services_request) {
  delegate_->Connect(std::move(services_request));
}

// |Agent|
void AgentImpl::RunTask(const fidl::String& task_id,
                        const RunTaskCallback& callback) {
  delegate_->RunTask(task_id, callback);
}

}  // namespace modular
