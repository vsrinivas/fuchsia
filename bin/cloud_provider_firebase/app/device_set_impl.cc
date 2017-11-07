// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/app/device_set_impl.h"

#include "lib/fxl/logging.h"
#include "peridot/bin/cloud_provider_firebase/page_handler/impl/paths.h"
#include "peridot/bin/ledger/convert/convert.h"

namespace cloud_provider_firebase {

DeviceSetImpl::DeviceSetImpl(
    auth_provider::AuthProvider* auth_provider,
    std::unique_ptr<CloudDeviceSet> cloud_device_set,
    fidl::InterfaceRequest<cloud_provider::DeviceSet> request)
    : auth_provider_(auth_provider),
      cloud_device_set_(std::move(cloud_device_set)),
      binding_(this, std::move(request)) {
  FXL_DCHECK(auth_provider_);
  // The class shuts down when the client connection is disconnected.
  binding_.set_connection_error_handler([this] {
    if (on_empty_) {
      on_empty_();
    }
  });
}

DeviceSetImpl::~DeviceSetImpl() {}

void DeviceSetImpl::CheckFingerprint(fidl::Array<uint8_t> fingerprint,
                                     const CheckFingerprintCallback& callback) {
  auto request = auth_provider_->GetFirebaseToken(
      [this, fingerprint = convert::ToString(fingerprint), callback](
          auth_provider::AuthStatus auth_status,
          std::string auth_token) mutable {
        if (auth_status != auth_provider::AuthStatus::OK) {
          callback(cloud_provider::Status::AUTH_ERROR);
          return;
        }

        cloud_device_set_->CheckFingerprint(
            std::move(auth_token), std::move(fingerprint),
            [this,
             callback = std::move(callback)](CloudDeviceSet::Status status) {
              switch (status) {
                case CloudDeviceSet::Status::OK:
                  callback(cloud_provider::Status::OK);
                  return;
                case CloudDeviceSet::Status::ERASED:
                  callback(cloud_provider::Status::NOT_FOUND);
                  return;
                case CloudDeviceSet::Status::NETWORK_ERROR:
                  callback(cloud_provider::Status::NETWORK_ERROR);
                  return;
              }
            });
      });
  auth_token_requests_.emplace(request);
}

void DeviceSetImpl::SetFingerprint(fidl::Array<uint8_t> fingerprint,
                                   const SetFingerprintCallback& callback) {
  auto request = auth_provider_->GetFirebaseToken(
      [this, fingerprint = convert::ToString(fingerprint), callback](
          auth_provider::AuthStatus auth_status,
          std::string auth_token) mutable {
        if (auth_status != auth_provider::AuthStatus::OK) {
          callback(cloud_provider::Status::AUTH_ERROR);
          return;
        }

        cloud_device_set_->SetFingerprint(
            std::move(auth_token), std::move(fingerprint),
            [this,
             callback = std::move(callback)](CloudDeviceSet::Status status) {
              switch (status) {
                case CloudDeviceSet::Status::OK:
                  callback(cloud_provider::Status::OK);
                  return;
                case CloudDeviceSet::Status::ERASED:
                  FXL_NOTREACHED();
                  callback(cloud_provider::Status::INTERNAL_ERROR);
                  return;
                case CloudDeviceSet::Status::NETWORK_ERROR:
                  callback(cloud_provider::Status::NETWORK_ERROR);
                  return;
              }
            });
      });
  auth_token_requests_.emplace(request);
}

void DeviceSetImpl::SetWatcher(
    fidl::Array<uint8_t> fingerprint,
    fidl::InterfaceHandle<cloud_provider::DeviceSetWatcher> watcher,
    const SetWatcherCallback& callback) {
  watcher_ = cloud_provider::DeviceSetWatcherPtr::Create(std::move(watcher));
  set_watcher_callback_called_ = false;
  timestamp_update_request_sent_ = false;

  auto request = auth_provider_->GetFirebaseToken(
      [this, fingerprint = convert::ToString(fingerprint), callback](
          auth_provider::AuthStatus auth_status,
          std::string auth_token) mutable {
        if (auth_status != auth_provider::AuthStatus::OK) {
          callback(cloud_provider::Status::AUTH_ERROR);
          set_watcher_callback_called_ = true;
          return;
        }

        // Note that the callback passed to WatchFingerprint() can be called
        // up to 3 times: if setting the watcher is successful, it is first
        // called with status = OK, then again with the status = OK after the
        // request to update the fingerprint timestamp completed, and then again
        // with an error status when the connection breaks or the fingerprint is
        // erased.
        // We use |set_watcher_callback_called_| to ensure that the client
        // callback is called exactly once.
        // We use |timestamp_update_request_sent_| to ensure that the request to
        // update the timestamp occurs only once.
        cloud_device_set_->WatchFingerprint(
            auth_token, fingerprint,
            [this, callback = std::move(callback),
             auth_token = std::move(auth_token),
             fingerprint =
                 std::move(fingerprint)](CloudDeviceSet::Status status) {
              cloud_provider::Status response_status;
              switch (status) {
                case CloudDeviceSet::Status::OK:
                  response_status = cloud_provider::Status::OK;
                  if (!timestamp_update_request_sent_) {
                    timestamp_update_request_sent_ = true;
                    cloud_device_set_->UpdateTimestampAssociatedWithFingerprint(
                        auth_token, fingerprint);
                  }
                  break;
                case CloudDeviceSet::Status::ERASED:
                  response_status = cloud_provider::Status::NOT_FOUND;
                  if (watcher_) {
                    watcher_->OnCloudErased();
                  }
                  break;
                case CloudDeviceSet::Status::NETWORK_ERROR:
                  response_status = cloud_provider::Status::NETWORK_ERROR;
                  if (watcher_) {
                    watcher_->OnNetworkError();
                  }
                  break;
              }

              if (!set_watcher_callback_called_) {
                callback(response_status);
                set_watcher_callback_called_ = true;
              }
            });
      });
  auth_token_requests_.emplace(request);
}

void DeviceSetImpl::Erase(const EraseCallback& callback) {
  auto request = auth_provider_->GetFirebaseToken(
      [this, callback](auth_provider::AuthStatus auth_status,
                       std::string auth_token) mutable {
        if (auth_status != auth_provider::AuthStatus::OK) {
          callback(cloud_provider::Status::AUTH_ERROR);
          return;
        }
        cloud_device_set_->EraseAllFingerprints(
            auth_token, [this, callback = std::move(callback)](
                            CloudDeviceSet::Status status) {
              switch (status) {
                case CloudDeviceSet::Status::OK:
                  callback(cloud_provider::Status::OK);
                  return;
                case CloudDeviceSet::Status::ERASED:
                  FXL_NOTREACHED();
                  callback(cloud_provider::Status::INTERNAL_ERROR);
                  return;
                case CloudDeviceSet::Status::NETWORK_ERROR:
                  callback(cloud_provider::Status::NETWORK_ERROR);
                  return;
              }
            });

      });
  auth_token_requests_.emplace(request);
}

}  // namespace cloud_provider_firebase
