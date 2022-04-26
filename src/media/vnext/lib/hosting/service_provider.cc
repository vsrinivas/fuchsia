// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/hosting/service_provider.h"

#include <lib/syslog/cpp/macros.h>

namespace fmlib {

void ServiceProvider::RegisterService(std::string protocol_name,
                                      std::unique_ptr<ServiceBinder> binder) {
  FX_CHECK(thread_.is_current());
  binderies_by_protocol_name_.emplace(std::move(protocol_name), std::move(binder));
}

void ServiceProvider::ConnectToService(const std::string& protocol_name, zx::channel channel) {
  thread_.PostTask([this, protocol_name, channel = std::move(channel)]() mutable {
    auto iter = binderies_by_protocol_name_.find(protocol_name);
    if (iter != binderies_by_protocol_name_.end()) {
      FX_CHECK(iter->second);
      iter->second->Bind(std::move(channel));
    } else if (component_context_) {
      component_context_->svc()->Connect(protocol_name, std::move(channel));
    }
  });
}

}  // namespace fmlib
