// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_firestore/bin/app/cloud_provider_impl.h"

#include <lib/fit/function.h>

#include <utility>

#include "src/ledger/bin/app/serialization_version.h"
#include "src/ledger/cloud_provider_firestore/bin/app/credentials_provider_impl.h"
#include "src/ledger/cloud_provider_firestore/bin/app/grpc_status.h"
#include "src/ledger/cloud_provider_firestore/bin/firestore/encoding.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/scoped_callback.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/concatenate.h"
#include "src/lib/fxl/strings/string_view.h"

namespace cloud_provider_firestore {

constexpr char kSeparator[] = "/";
constexpr char kUsersCollection[] = "users";
constexpr char kVersionsCollection[] = "versions";
constexpr char kPageCollection[] = "pages";
constexpr char kNamespaceCollection[] = "namespaces";
constexpr char kExistsKey[] = "exists";

std::string GetUserPath(fxl::StringView root_path, fxl::StringView user_id) {
  return fxl::Concatenate({
      root_path,
      kSeparator,
      kUsersCollection,
      kSeparator,
      user_id,
  });
}

std::string GetVersionPath(fxl::StringView user_path) {
  return fxl::Concatenate({
      user_path,
      kSeparator,
      kVersionsCollection,
      kSeparator,
      ledger::kSerializationVersion,
  });
}

std::string GetNamespacePath(fxl::StringView version_path, fxl::StringView namespace_id) {
  std::string encoded_namespace_id = EncodeKey(namespace_id);
  return fxl::Concatenate(
      {version_path, kSeparator, kNamespaceCollection, kSeparator, encoded_namespace_id});
}

std::string GetPagePath(fxl::StringView namespace_path, fxl::StringView page_id) {
  std::string encoded_page_id = EncodeKey(page_id);
  return fxl::Concatenate(
      {namespace_path, kSeparator, kPageCollection, kSeparator, encoded_page_id});
}

CloudProviderImpl::CloudProviderImpl(async_dispatcher_t* dispatcher, rng::Random* random,
                                     std::string user_id,
                                     std::unique_ptr<firebase_auth::FirebaseAuth> firebase_auth,
                                     std::unique_ptr<FirestoreService> firestore_service,
                                     fidl::InterfaceRequest<cloud_provider::CloudProvider> request)
    : random_(random),
      user_id_(std::move(user_id)),
      firestore_service_(std::move(firestore_service)),
      binding_(this, std::move(request)),
      device_sets_(dispatcher),
      page_clouds_(dispatcher),
      weak_ptr_factory_(this) {
  // The class shuts down when the client connection is disconnected.
  binding_.set_error_handler([this](zx_status_t status) { ShutDownAndReportDiscardable(); });
  // The class also shuts down when the auth provider is disconnected.
  firebase_auth->set_error_handler([this] {
    FXL_LOG(ERROR) << "Lost connection to the token provider, "
                   << "shutting down the cloud provider.";
    ShutDownAndReportDiscardable();
  });

  credentials_provider_ =
      std::make_unique<CredentialsProviderImpl>(dispatcher, std::move(firebase_auth));
}

CloudProviderImpl::~CloudProviderImpl() = default;

void CloudProviderImpl::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

bool CloudProviderImpl::IsDiscardable() const { return discardable_; };

void CloudProviderImpl::ShutDownAndReportDiscardable() {
  if (binding_.is_bound()) {
    binding_.Unbind();
  }

  fit::closure shut_down = [this] {
    firestore_service_->ShutDown([this] {
      discardable_ = true;
      if (on_discardable_) {
        on_discardable_();
      }
    });
  };

  if (pending_placeholder_requests_.empty()) {
    shut_down();
    return;
  }

  pending_placeholder_requests_.SetOnDiscardable(std::move(shut_down));
}

void CloudProviderImpl::ScopedGetCredentials(
    fit::function<void(std::shared_ptr<grpc::CallCredentials>)> callback) {
  credentials_provider_->GetCredentials(
      callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void CloudProviderImpl::GetDeviceSet(fidl::InterfaceRequest<cloud_provider::DeviceSet> device_set,
                                     GetDeviceSetCallback callback) {
  const std::string user_path = GetUserPath(firestore_service_->GetRootPath(), user_id_);
  const std::string version_path = GetVersionPath(user_path);
  device_sets_.emplace(version_path, credentials_provider_.get(), firestore_service_.get(),
                       std::move(device_set));
  callback(cloud_provider::Status::OK);

  // Create a placeholder document for the root of the serialization version.
  CreatePlaceholderDocument(user_path, kVersionsCollection,
                            ledger::kSerializationVersion.ToString());
}

void CloudProviderImpl::GetPageCloud(std::vector<uint8_t> app_id, std::vector<uint8_t> page_id,
                                     fidl::InterfaceRequest<cloud_provider::PageCloud> page_cloud,
                                     GetPageCloudCallback callback) {
  const std::string user_path = GetUserPath(firestore_service_->GetRootPath(), user_id_);
  const std::string version_path = GetVersionPath(user_path);
  const std::string app_id_str = convert::ToString(app_id);
  const std::string namespace_path = GetNamespacePath(version_path, app_id_str);
  const std::string page_id_str = convert::ToString(page_id);
  const std::string page_path = GetPagePath(namespace_path, page_id_str);
  page_clouds_.emplace(page_path, random_, credentials_provider_.get(), firestore_service_.get(),
                       std::move(page_cloud));
  callback(cloud_provider::Status::OK);

  // Create a placeholder document for the root of the serialization version.
  CreatePlaceholderDocument(user_path, kVersionsCollection,
                            ledger::kSerializationVersion.ToString());
  // Create a placeholder document for the root of the app namespace.
  CreatePlaceholderDocument(version_path, kNamespaceCollection, EncodeKey(app_id_str));
  // Create a placeholder document for the root of the page.
  CreatePlaceholderDocument(namespace_path, kPageCollection, EncodeKey(page_id_str));
}

void CloudProviderImpl::CreatePlaceholderDocument(std::string parent_document_path,
                                                  std::string collection_id,
                                                  std::string document_id) {
  auto request = google::firestore::v1beta1::CreateDocumentRequest();
  request.set_parent(std::move(parent_document_path));
  request.set_collection_id(std::move(collection_id));
  request.set_document_id(std::move(document_id));
  google::firestore::v1beta1::Value exists;
  exists.set_boolean_value(true);
  (*(request.mutable_document()->mutable_fields()))[kExistsKey] = exists;

  // Create an object that tracks the request in progress, so that we don't shut
  // down between requesting and receiving the credentials (see
  // ShutDownAndReportEmoty()). The value |true| is not meaningful.
  auto pending_request_marker = pending_placeholder_requests_.Manage(true);
  ScopedGetCredentials(
      [this, request = std::move(request),
       pending_request_marker = std::move(pending_request_marker)](auto call_credentials) mutable {
        firestore_service_->CreateDocument(
            std::move(request), std::move(call_credentials), [](auto status, auto result) {
              if (status.error_code() != grpc::OK && status.error_code() != grpc::ALREADY_EXISTS) {
                LogGrpcRequestError(status);
              }
            });
      });
}

}  // namespace cloud_provider_firestore
