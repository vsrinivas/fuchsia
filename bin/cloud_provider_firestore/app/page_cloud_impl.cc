// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/page_cloud_impl.h"

#include "lib/fsl/socket/strings.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/strings/concatenate.h"
#include "peridot/bin/cloud_provider_firestore/app/grpc_status.h"
#include "peridot/bin/cloud_provider_firestore/firestore/encoding.h"
#include "peridot/lib/convert/convert.h"

namespace cloud_provider_firestore {
namespace {

constexpr char kSeparator[] = "/";
constexpr char kObjectCollection[] = "objects";
constexpr char kDataKey[] = "data";
constexpr size_t kFirestoreMaxDocumentSize = 1'000'000;
// Ledger stores objects chunked to ~64k, so even 500kB is more than should ever
// be needed.
constexpr size_t kMaxObjectSize = kFirestoreMaxDocumentSize / 2;

std::string GetObjectPath(fxl::StringView page_path,
                          fxl::StringView object_id) {
  std::string encoded_object_id = EncodeKey(object_id);
  return fxl::Concatenate({page_path, kSeparator, kObjectCollection, kSeparator,
                           encoded_object_id});
}

}  // namespace

PageCloudImpl::PageCloudImpl(
    std::string page_path,
    CredentialsProvider* credentials_provider,
    FirestoreService* firestore_service,
    f1dl::InterfaceRequest<cloud_provider::PageCloud> request)
    : page_path_(std::move(page_path)),
      credentials_provider_(credentials_provider),
      firestore_service_(firestore_service),
      binding_(this, std::move(request)) {
  // The class shuts down when the client connection is disconnected.
  binding_.set_error_handler([this] {
    if (on_empty_) {
      on_empty_();
    }
  });
}

PageCloudImpl::~PageCloudImpl() {}

void PageCloudImpl::AddCommits(
    f1dl::VectorPtr<cloud_provider::CommitPtr> /*commits*/,
    const AddCommitsCallback& callback) {
  FXL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

void PageCloudImpl::GetCommits(f1dl::VectorPtr<uint8_t> /*min_position_token*/,
                               const GetCommitsCallback& callback) {
  FXL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR, nullptr, nullptr);
}

void PageCloudImpl::AddObject(f1dl::VectorPtr<uint8_t> id,
                              fsl::SizedVmoTransportPtr data,
                              const AddObjectCallback& callback) {
  std::string data_str;
  fsl::SizedVmo vmo;
  if (!fsl::StringFromVmo(data, &data_str) ||
      data_str.size() > kMaxObjectSize) {
    callback(cloud_provider::Status::ARGUMENT_ERROR);
    return;
  }

  auto request = google::firestore::v1beta1::CreateDocumentRequest();
  request.set_parent(page_path_);
  request.set_collection_id(kObjectCollection);
  google::firestore::v1beta1::Document* document = request.mutable_document();
  request.set_document_id(EncodeKey(convert::ToString(id)));
  *((*document->mutable_fields())[kDataKey].mutable_bytes_value()) =
      std::move(data_str);

  credentials_provider_->GetCredentials(
      [this, request = std::move(request),
       callback](auto call_credentials) mutable {
        firestore_service_->CreateDocument(
            std::move(request), std::move(call_credentials),
            [callback](auto status, auto result) {
              if (LogGrpcRequestError(status)) {
                callback(ConvertGrpcStatus(status.error_code()));
                return;
              }
              callback(cloud_provider::Status::OK);
            });
      });
}

void PageCloudImpl::GetObject(f1dl::VectorPtr<uint8_t> id,
                              const GetObjectCallback& callback) {
  auto request = google::firestore::v1beta1::GetDocumentRequest();
  request.set_name(GetObjectPath(page_path_, convert::ToString(id)));

  credentials_provider_->GetCredentials([this, request = std::move(request),
                                         callback](
                                            auto call_credentials) mutable {
    firestore_service_->GetDocument(
        std::move(request), std::move(call_credentials),
        [callback](auto status, auto result) {
          if (LogGrpcRequestError(status)) {
            callback(ConvertGrpcStatus(status.error_code()), 0u, zx::socket());
            return;
          }

          if (result.fields().count(kDataKey) != 1) {
            FXL_LOG(ERROR)
                << "Incorrect format of the retrieved object document";
            callback(cloud_provider::Status::INTERNAL_ERROR, 0u, zx::socket());
          }

          const std::string& bytes = result.fields().at(kDataKey).bytes_value();
          callback(cloud_provider::Status::OK, bytes.size(),
                   fsl::WriteStringToSocket(bytes));
        });
  });
}

void PageCloudImpl::SetWatcher(
    f1dl::VectorPtr<uint8_t> /*min_position_token*/,
    f1dl::InterfaceHandle<cloud_provider::PageCloudWatcher> /*watcher*/,
    const SetWatcherCallback& callback) {
  FXL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

}  // namespace cloud_provider_firestore
