// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/entity/entity_watcher_impl.h"

namespace modular {

EntityWatcherImpl::EntityWatcherImpl() : binding_(this) {}

EntityWatcherImpl::EntityWatcherImpl(
    fit::function<void(std::unique_ptr<fuchsia::mem::Buffer> value)> callback)
    : callback_(std::move(callback)), binding_(this) {}

void EntityWatcherImpl::SetOnUpdated(
    fit::function<void(std::unique_ptr<fuchsia::mem::Buffer> value)> callback) {
  callback_ = std::move(callback);
}

void EntityWatcherImpl::Connect(fidl::InterfaceRequest<fuchsia::modular::EntityWatcher> request) {
  binding_.Bind(std::move(request));
}

void EntityWatcherImpl::OnUpdated(std::unique_ptr<fuchsia::mem::Buffer> value) {
  if (callback_) {
    callback_(std::move(value));
  }
}

}  // namespace modular
