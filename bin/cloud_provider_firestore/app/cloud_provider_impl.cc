// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/cloud_provider_impl.h"

#include <utility>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/concatenate.h"
#include "lib/fxl/strings/string_view.h"
#include "peridot/bin/cloud_provider_firestore/app/credentials_provider_impl.h"
#include "peridot/bin/cloud_provider_firestore/firestore/encoding.h"
#include "peridot/bin/ledger/storage/public/constants.h"
#include "peridot/lib/convert/convert.h"

namespace cloud_provider_firestore {

constexpr char kSeparator[] = "/";
constexpr char kUsersCollection[] = "users";
constexpr char kPageCollection[] = "pages";
constexpr char kNamespaceCollection[] = "namespaces";
constexpr char kDefaultDocument[] = "default_document";

std::string GetUserPath(fxl::StringView root_path, fxl::StringView user_id) {
  return fxl::Concatenate({root_path, kSeparator, kUsersCollection, kSeparator,
                           user_id, kSeparator, storage::kSerializationVersion,
                           kSeparator, kDefaultDocument});
}

std::string GetNamespacePath(fxl::StringView user_path,
                             fxl::StringView namespace_id) {
  std::string encoded_namespace_id = EncodeKey(namespace_id);
  return fxl::Concatenate({user_path, kSeparator, kNamespaceCollection,
                           kSeparator, encoded_namespace_id});
}

std::string GetPagePath(fxl::StringView namespace_path,
                        fxl::StringView page_id) {
  std::string encoded_page_id = EncodeKey(page_id);
  return fxl::Concatenate({namespace_path, kSeparator, kPageCollection,
                           kSeparator, encoded_page_id});
}

CloudProviderImpl::CloudProviderImpl(
    std::string user_id,
    std::unique_ptr<firebase_auth::FirebaseAuth> firebase_auth,
    std::unique_ptr<FirestoreService> firestore_service,
    fidl::InterfaceRequest<cloud_provider::CloudProvider> request)
    : user_id_(std::move(user_id)),
      firestore_service_(std::move(firestore_service)),
      binding_(this, std::move(request)) {
  // The class shuts down when the client connection is disconnected.
  binding_.set_error_handler([this] { ShutDownAndReportEmpty(); });
  // The class also shuts down when the auth provider is disconnected.
  firebase_auth->set_error_handler([this] {
    FXL_LOG(ERROR) << "Lost connection to the token provider, "
                   << "shutting down the cloud provider.";
    ShutDownAndReportEmpty();
  });

  credentials_provider_ =
      std::make_unique<CredentialsProviderImpl>(std::move(firebase_auth));
}

CloudProviderImpl::~CloudProviderImpl() {}

void CloudProviderImpl::ShutDownAndReportEmpty() {
  firestore_service_->ShutDown([this] {
    if (on_empty_) {
      on_empty_();
    }
  });
}

void CloudProviderImpl::GetDeviceSet(
    fidl::InterfaceRequest<cloud_provider::DeviceSet> device_set,
    GetDeviceSetCallback callback) {
  std::string user_path =
      GetUserPath(firestore_service_->GetRootPath(), user_id_);
  device_sets_.emplace(std::move(user_path), credentials_provider_.get(),
                       firestore_service_.get(), std::move(device_set));
  callback(cloud_provider::Status::OK);
}

void CloudProviderImpl::GetPageCloud(
    fidl::VectorPtr<uint8_t> app_id, fidl::VectorPtr<uint8_t> page_id,
    fidl::InterfaceRequest<cloud_provider::PageCloud> page_cloud,
    GetPageCloudCallback callback) {
  const std::string user_path =
      GetUserPath(firestore_service_->GetRootPath(), user_id_);
  const std::string namespace_path =
      GetNamespacePath(user_path, convert::ToString(app_id));
  std::string page_path =
      GetPagePath(namespace_path, convert::ToString(page_id));
  page_clouds_.emplace(std::move(page_path), credentials_provider_.get(),
                       firestore_service_.get(), std::move(page_cloud));
  callback(cloud_provider::Status::OK);
}

}  // namespace cloud_provider_firestore
