// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/acquirers/mock/mock_focus.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "apps/modular/services/user/focus.fidl.h"

namespace maxwell {
namespace acquirers {

constexpr char FocusAcquirer::kLabel[];
constexpr char FocusAcquirer::kSchema[];

MockFocusAcquirer::MockFocusAcquirer(context::ContextEngine* context_engine)
    : ctl_(this) {
  maxwell::context::ContextAcquirerClientPtr cx;
  context_engine->RegisterContextAcquirer("MockFocusAcquirer", GetProxy(&cx));

  fidl::InterfaceHandle<context::PublisherController> ctl_handle;
  ctl_.Bind(&ctl_handle);

  cx->Publish(kLabel, kSchema, std::move(ctl_handle), GetProxy(&out_));
}

void MockFocusAcquirer::Publish(int modular_state) {
  std::ostringstream json;
  json << "{ \"modular_state\": " << modular_state << " }";
  out_->Update(json.str());
}

void MockFocusAcquirer::OnHasSubscribers() {
  has_subscribers_ = true;
}

void MockFocusAcquirer::OnNoSubscribers() {
  has_subscribers_ = false;
}

}  // namespace acquirers
}  // namespace maxwell
