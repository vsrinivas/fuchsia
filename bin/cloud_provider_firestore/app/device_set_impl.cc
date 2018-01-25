// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/device_set_impl.h"

#include <google/firestore/v1beta1/firestore.pb.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/concatenate.h"
#include "lib/fxl/strings/string_view.h"
#include "peridot/bin/cloud_provider_firestore/app/grpc_status.h"
#include "peridot/bin/cloud_provider_firestore/firestore/encoding.h"
#include "peridot/lib/convert/convert.h"

namespace cloud_provider_firestore {

namespace {
constexpr char kSeparator[] = "/";
constexpr char kDeviceCollection[] = "devices";
constexpr char kExistsKey[] = "exists";

std::string GetDevicePath(fxl::StringView user_path,
                          fxl::StringView fingerprint) {
  std::string encoded_fingerprint = EncodeKey(fingerprint);
  return fxl::Concatenate({user_path, kSeparator, kDeviceCollection, kSeparator,
                           encoded_fingerprint});
}
}  // namespace

DeviceSetImpl::DeviceSetImpl(
    std::string user_path,
    CredentialsProvider* credentials_provider,
    FirestoreService* firestore_service,
    fidl::InterfaceRequest<cloud_provider::DeviceSet> request)
    : user_path_(std::move(user_path)),
      credentials_provider_(credentials_provider),
      firestore_service_(firestore_service),
      binding_(this, std::move(request)) {
  FXL_DCHECK(!user_path_.empty());
  FXL_DCHECK(credentials_provider_);
  FXL_DCHECK(firestore_service_);

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
  auto request = google::firestore::v1beta1::GetDocumentRequest();
  request.set_name(
      GetDevicePath(user_path_, convert::ToStringView(fingerprint)));

  credentials_provider_->GetCredentials(
      [this, request = std::move(request),
       callback](auto call_credentials) mutable {
        firestore_service_->GetDocument(
            std::move(request), std::move(call_credentials),
            [callback](auto status, auto result) {
              if (!status.ok()) {
                FXL_LOG(ERROR) << "Server request failed, "
                               << "error message: " << status.error_message()
                               << ", error details: " << status.error_details();
                callback(ConvertGrpcStatus(status.error_code()));
                return;
              }

              callback(cloud_provider::Status::OK);
            });
      });
}

void DeviceSetImpl::SetFingerprint(fidl::Array<uint8_t> fingerprint,
                                   const SetFingerprintCallback& callback) {
  auto request = google::firestore::v1beta1::CreateDocumentRequest();
  request.set_parent(user_path_);
  request.set_collection_id(kDeviceCollection);
  request.set_document_id(EncodeKey(convert::ToString(fingerprint)));
  google::firestore::v1beta1::Value exists;
  exists.set_boolean_value(true);
  (*(request.mutable_document()->mutable_fields()))[kExistsKey] = exists;

  credentials_provider_->GetCredentials(
      [this, request = std::move(request),
       callback](auto call_credentials) mutable {
        firestore_service_->CreateDocument(
            std::move(request), std::move(call_credentials),
            [callback](auto status, auto result) {
              if (!status.ok()) {
                FXL_LOG(ERROR) << "Server request failed, "
                               << "error message: " << status.error_message()
                               << ", error details: " << status.error_details();
                callback(ConvertGrpcStatus(status.error_code()));
                return;
              }
              callback(cloud_provider::Status::OK);
            });
      });
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
