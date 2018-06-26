// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/a11y_manager/manager_impl.h"

namespace a11y_manager {

void ManagerImpl::ConnectSemanticsProvider(
    uint32_t id,
    fidl::InterfaceHandle<fuchsia::accessibility::SemanticsProvider>
        semantics_provider) {
  FXL_LOG(INFO) << "Connecting semantics provider with id:" << id;
  fuchsia::accessibility::SemanticsProviderPtr provider =
      semantics_provider.Bind();
  provider.set_error_handler([this, id]() {
    this->semantic_provider_by_id_.erase(id);
    FXL_LOG(ERROR) << "A11y manager lost connection with id: " << id;
  });
  semantic_provider_by_id_.emplace(id, std::move(provider));
}

void ManagerImpl::NotifyEvent(uint32_t id,
                                  fuchsia::ui::input::InputEvent event) {
  // TODO(SCN-816) Only use this for debugging purposes before actual input
  // listening system is available

  auto it = semantic_provider_by_id_.find(id);
  if (it != semantic_provider_by_id_.end()) {
    FXL_LOG(INFO) << "Event affected semantic info frontend with id: " << id;
  } else {
    FXL_LOG(INFO) << "No semantic info available for id: " << id;
  }
}

}  // namespace a11y_manager
