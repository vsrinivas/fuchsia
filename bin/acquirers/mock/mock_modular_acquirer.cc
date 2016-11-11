// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/acquirers/mock/mock_modular_acquirer.h"

namespace maxwell {
namespace acquirers {

constexpr char ModularAcquirer::kLabel[];
constexpr char ModularAcquirer::kSchema[];

MockModularAcquirer::MockModularAcquirer(
    const context::ContextEnginePtr& context_engine)
    : ctl_(this) {
  maxwell::context::ContextAcquirerClientPtr cx;
  context_engine->RegisterContextAcquirer("MockModularAcquirer", GetProxy(&cx));

  fidl::InterfaceHandle<context::PublisherController> ctl_handle;
  ctl_.Bind(&ctl_handle);

  cx->Publish(kLabel, kSchema, std::move(ctl_handle), GetProxy(&out_));
}

void MockModularAcquirer::Publish(int modular_state) {
  std::ostringstream json;
  json << "{ \"modular_state\": " << modular_state << " }";
  out_->Update(json.str());
}

void MockModularAcquirer::OnHasSubscribers() {
  has_subscribers_ = true;
}

void MockModularAcquirer::OnNoSubscribers() {
  has_subscribers_ = false;
}

}  // namespace acquirers
}  // namespace maxwell
