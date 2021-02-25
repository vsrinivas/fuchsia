// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debug_data.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include <memory>

#include "event_stream.h"

DebugDataImpl::Inner::Inner(fidl::InterfaceRequest<fuchsia::debugdata::DebugData> request,
                            std::string test_url, async_dispatcher_t* dispatcher)
    : binding_(this, std::move(request), dispatcher), test_url_(std::move(test_url)) {
  binding_.set_error_handler([this](zx_status_t /*unused*/) {
    if (parent_) {
      auto self = parent_->Remove(this);
      // this object dies after this line
    }
  });
}

std::unique_ptr<DebugDataImpl::Inner> DebugDataImpl::Remove(DebugDataImpl::Inner* ptr) {
  auto search = inners_.find(ptr);
  if (search != inners_.end()) {
    return std::move(inners_.erase(search)->second);
  }
  return nullptr;
}

void DebugDataImpl::Inner::Publish(::std::string data_sink, ::zx::vmo data) {
  FX_LOGS(INFO) << "TODO: Implement Publish";
}

void DebugDataImpl::Inner::LoadConfig(::std::string config_name, LoadConfigCallback callback) {
  FX_LOGS(INFO) << "TODO: Implement LoadConfig";
  CloseConnection(ZX_ERR_NOT_FOUND);
}

void DebugDataImpl::Inner::CloseConnection(zx_status_t epitaph_value) {
  binding_.Close(epitaph_value);
  if (parent_) {
    auto self = parent_->Remove(this);
    // this object dies after this line
  }
}
