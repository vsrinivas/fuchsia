// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/device_set_impl.h"

#include "lib/fxl/logging.h"

namespace cloud_provider_firestore {

DeviceSetImpl::DeviceSetImpl(
    fidl::InterfaceRequest<cloud_provider::DeviceSet> request)
    : binding_(this, std::move(request)) {
  // The class shuts down when the client connection is disconnected.
  binding_.set_connection_error_handler([this] {
    if (on_empty_) {
      on_empty_();
    }
  });
}

DeviceSetImpl::~DeviceSetImpl() {}

void DeviceSetImpl::CheckFingerprint(fidl::Array<uint8_t> /*fingerprint*/,
                                     const CheckFingerprintCallback& callback) {
  FXL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

void DeviceSetImpl::SetFingerprint(fidl::Array<uint8_t> /*fingerprint*/,
                                   const SetFingerprintCallback& callback) {
  FXL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

void DeviceSetImpl::SetWatcher(
    fidl::Array<uint8_t> /*fingerprint*/,
    fidl::InterfaceHandle<cloud_provider::DeviceSetWatcher> /*watcher*/,
    const SetWatcherCallback& callback) {
  FXL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

void DeviceSetImpl::Erase(const EraseCallback& callback) {
  FXL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

}  // namespace cloud_provider_firestore
