// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/cloud_provider_firebase/device_set_impl.h"

#include "garnet/public/lib/ftl/logging.h"

namespace cloud_provider_firebase {

DeviceSetImpl::DeviceSetImpl(
    auth_provider::AuthProvider* auth_provider,
    fidl::InterfaceRequest<cloud_provider::DeviceSet> request)
    : auth_provider_(auth_provider), binding_(this, std::move(request)) {
  FTL_DCHECK(auth_provider_);
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
  FTL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

void DeviceSetImpl::SetFingerprint(fidl::Array<uint8_t> /*fingerprint*/,
                                   const SetFingerprintCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

void DeviceSetImpl::SetWatcher(
    fidl::InterfaceHandle<cloud_provider::DeviceSetWatcher> /*watcher*/,
    fidl::Array<uint8_t> /*fingerprint*/,
    const SetWatcherCallback& callback) {
  FTL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

}  // namespace cloud_provider_firebase
