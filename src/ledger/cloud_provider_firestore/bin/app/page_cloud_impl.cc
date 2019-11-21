// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_firestore/bin/app/page_cloud_impl.h"

#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>

#include "src/ledger/cloud_provider_firestore/bin/app/grpc_status.h"
#include "src/ledger/cloud_provider_firestore/bin/firestore/encoding.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/scoped_callback.h"
#include "src/lib/fsl/socket/strings.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/strings/concatenate.h"

namespace cloud_provider_firestore {
namespace {

constexpr char kSeparator[] = "/";
constexpr char kObjectCollection[] = "objects";
constexpr char kCommitLogCollection[] = "commit-log";
constexpr char kDataKey[] = "data";
constexpr char kTimestampField[] = "timestamp";
constexpr size_t kFirestoreMaxDocumentSize = 1'000'000;
// Ledger stores objects chunked to ~64k, so even 500kB is more than should ever
// be needed.
constexpr size_t kMaxObjectSize = kFirestoreMaxDocumentSize / 2;

std::string GetObjectPath(fxl::StringView page_path, fxl::StringView object_id) {
  std::string encoded_object_id = EncodeKey(object_id);
  return fxl::Concatenate(
      {page_path, kSeparator, kObjectCollection, kSeparator, encoded_object_id});
}

std::string GetCommitBatchPath(fxl::StringView page_path, fxl::StringView batch_id) {
  std::string encoded_batch_id = EncodeKey(batch_id);
  return fxl::Concatenate(
      {page_path, kSeparator, kCommitLogCollection, kSeparator, encoded_batch_id});
}

google::firestore::v1beta1::StructuredQuery MakeCommitQuery(
    std::unique_ptr<google::protobuf::Timestamp> timestamp_or_null) {
  google::firestore::v1beta1::StructuredQuery query;

  // Sub-collections to be queried.
  google::firestore::v1beta1::StructuredQuery::CollectionSelector& selector = *query.add_from();
  selector.set_collection_id(kCommitLogCollection);
  selector.set_all_descendants(false);

  // Ordering.
  google::firestore::v1beta1::StructuredQuery::Order& order_by = *query.add_order_by();
  order_by.mutable_field()->set_field_path(kTimestampField);

  // Filtering.
  if (timestamp_or_null) {
    google::firestore::v1beta1::StructuredQuery::Filter& filter = *query.mutable_where();
    google::firestore::v1beta1::StructuredQuery::FieldFilter& field_filter =
        *filter.mutable_field_filter();

    field_filter.mutable_field()->set_field_path(kTimestampField);
    field_filter.set_op(
        google::firestore::v1beta1::StructuredQuery_FieldFilter_Operator_GREATER_THAN_OR_EQUAL);
    field_filter.mutable_value()->mutable_timestamp_value()->Swap(timestamp_or_null.get());
  }
  return query;
}

}  // namespace

PageCloudImpl::PageCloudImpl(std::string page_path, rng::Random* random,
                             CredentialsProvider* credentials_provider,
                             FirestoreService* firestore_service,
                             fidl::InterfaceRequest<cloud_provider::PageCloud> request)
    : page_path_(std::move(page_path)),
      random_(random),
      credentials_provider_(credentials_provider),
      firestore_service_(firestore_service),
      binding_(this, std::move(request)),
      weak_ptr_factory_(this) {
  // The class shuts down when the client connection is disconnected.
  binding_.set_error_handler([this](zx_status_t status) {
    binding_.Unbind();
    if (on_discardable_) {
      on_discardable_();
    }
  });
}

PageCloudImpl::~PageCloudImpl() = default;

void PageCloudImpl::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

bool PageCloudImpl::IsDiscardable() const { return binding_.is_bound(); }

void PageCloudImpl::ScopedGetCredentials(
    fit::function<void(std::shared_ptr<grpc::CallCredentials>)> callback) {
  credentials_provider_->GetCredentials(
      callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void PageCloudImpl::AddCommits(cloud_provider::CommitPack commits, AddCommitsCallback callback) {
  std::vector<cloud_provider::CommitPackEntry> commit_pack_entries;
  if (!cloud_provider::DecodeCommitPack(commits, &commit_pack_entries)) {
    callback(cloud_provider::Status::ARGUMENT_ERROR);
    return;
  }

  auto request = google::firestore::v1beta1::CommitRequest();
  request.set_database(firestore_service_->GetDatabasePath());

  // Set the document name to a new UUID. Firestore Commit() API doesn't allow
  // to request the ID to be assigned by the server.
  const std::string document_name =
      GetCommitBatchPath(page_path_, convert::ToHex(random_->RandomUniqueBytes()));

  // The commit batch is added in a single commit containing multiple writes.
  //
  // First write adds the document containing the encoded commit batch.
  google::firestore::v1beta1::Write& add_batch_write = *(request.add_writes());
  EncodeCommitBatch(commits, add_batch_write.mutable_update());
  (*add_batch_write.mutable_update()->mutable_name()) = document_name;
  // Ensure that the write doesn't overwrite an existing document.
  add_batch_write.mutable_current_document()->set_exists(false);

  // The second write sets the timestamp field to the server-side request
  // timestamp.
  google::firestore::v1beta1::Write& set_timestamp_write = *(request.add_writes());
  (*set_timestamp_write.mutable_transform()->mutable_document()) = document_name;

  google::firestore::v1beta1::DocumentTransform_FieldTransform& transform =
      *(set_timestamp_write.mutable_transform()->add_field_transforms());
  *(transform.mutable_field_path()) = kTimestampField;
  transform.set_set_to_server_value(
      google::firestore::v1beta1::DocumentTransform_FieldTransform_ServerValue_REQUEST_TIME);

  ScopedGetCredentials([this, request = std::move(request),
                        callback = std::move(callback)](auto call_credentials) mutable {
    firestore_service_->Commit(std::move(request), std::move(call_credentials),
                               [callback = std::move(callback)](auto status, auto result) {
                                 if (LogGrpcRequestError(status)) {
                                   callback(ConvertGrpcStatus(status.error_code()));
                                   return;
                                 }
                                 callback(cloud_provider::Status::OK);
                               });
  });
}

void PageCloudImpl::GetCommits(std::unique_ptr<cloud_provider::PositionToken> min_position_token,
                               GetCommitsCallback callback) {
  std::unique_ptr<google::protobuf::Timestamp> timestamp_or_null;
  if (min_position_token) {
    timestamp_or_null = std::make_unique<google::protobuf::Timestamp>();
    if (!timestamp_or_null->ParseFromString(convert::ToString(min_position_token->opaque_id))) {
      callback(cloud_provider::Status::ARGUMENT_ERROR, nullptr, nullptr);
      return;
    }
  }

  auto request = google::firestore::v1beta1::RunQueryRequest();
  request.set_parent(page_path_);
  auto query = MakeCommitQuery(std::move(timestamp_or_null));
  request.mutable_structured_query()->Swap(&query);

  ScopedGetCredentials([this, request = std::move(request),
                        callback = std::move(callback)](auto call_credentials) mutable {
    firestore_service_->RunQuery(
        std::move(request), std::move(call_credentials),
        [callback = std::move(callback)](auto status, auto result) {
          if (LogGrpcRequestError(status)) {
            callback(ConvertGrpcStatus(status.error_code()), nullptr, nullptr);
            return;
          }

          std::vector<cloud_provider::CommitPackEntry> commit_entries;
          std::string timestamp;

          for (const auto& response : result) {
            if (!response.has_document()) {
              continue;
            }

            std::vector<cloud_provider::CommitPackEntry> batch_entries;
            if (!DecodeCommitBatch(response.document(), &batch_entries, &timestamp)) {
              callback(cloud_provider::Status::PARSE_ERROR, nullptr, nullptr);
              return;
            }

            std::move(batch_entries.begin(), batch_entries.end(),
                      std::back_inserter(commit_entries));
          }

          cloud_provider::CommitPack commit_pack;
          if (!cloud_provider::EncodeCommitPack(commit_entries, &commit_pack)) {
            callback(cloud_provider::Status::INTERNAL_ERROR, nullptr, nullptr);
            return;
          }

          std::unique_ptr<cloud_provider::PositionToken> token;
          if (!commit_entries.empty()) {
            token = std::make_unique<cloud_provider::PositionToken>();
            token->opaque_id = convert::ToArray(timestamp);
          }
          callback(cloud_provider::Status::OK, fidl::MakeOptional(std::move(commit_pack)),
                   std::move(token));
        });
  });
}

void PageCloudImpl::AddObject(std::vector<uint8_t> id, fuchsia::mem::Buffer data,
                              cloud_provider::ReferencePack /*references*/,
                              AddObjectCallback callback) {
  std::string data_str;
  fsl::SizedVmo vmo;
  if (!fsl::StringFromVmo(data, &data_str) || data_str.size() > kMaxObjectSize) {
    callback(cloud_provider::Status::ARGUMENT_ERROR);
    return;
  }

  auto request = google::firestore::v1beta1::CreateDocumentRequest();
  request.set_parent(page_path_);
  request.set_collection_id(kObjectCollection);
  google::firestore::v1beta1::Document* document = request.mutable_document();
  request.set_document_id(EncodeKey(convert::ToString(id)));
  *((*document->mutable_fields())[kDataKey].mutable_bytes_value()) = std::move(data_str);

  ScopedGetCredentials([this, request = std::move(request),
                        callback = std::move(callback)](auto call_credentials) mutable {
    firestore_service_->CreateDocument(std::move(request), std::move(call_credentials),
                                       [callback = std::move(callback)](auto status, auto result) {
                                         if (status.error_code() == grpc::ALREADY_EXISTS) {
                                           callback(cloud_provider::Status::OK);
                                           return;
                                         }
                                         if (LogGrpcRequestError(status)) {
                                           callback(ConvertGrpcStatus(status.error_code()));
                                           return;
                                         }
                                         callback(cloud_provider::Status::OK);
                                       });
  });
}

void PageCloudImpl::GetObject(std::vector<uint8_t> id, GetObjectCallback callback) {
  auto request = google::firestore::v1beta1::GetDocumentRequest();
  request.set_name(GetObjectPath(page_path_, convert::ToString(id)));

  ScopedGetCredentials([this, request = std::move(request),
                        callback = std::move(callback)](auto call_credentials) mutable {
    firestore_service_->GetDocument(
        std::move(request), std::move(call_credentials),
        [callback = std::move(callback)](auto status, auto result) {
          if (LogGrpcRequestError(status)) {
            callback(ConvertGrpcStatus(status.error_code()), nullptr);
            return;
          }

          if (result.fields().count(kDataKey) != 1) {
            FXL_LOG(ERROR) << "Incorrect format of the retrieved object document";
            callback(cloud_provider::Status::PARSE_ERROR, nullptr);
            return;
          }

          const std::string& bytes = result.fields().at(kDataKey).bytes_value();
          ::fuchsia::mem::Buffer buffer;
          if (!fsl::VmoFromString(bytes, &buffer)) {
            callback(cloud_provider::Status::INTERNAL_ERROR, nullptr);
            return;
          }
          callback(cloud_provider::Status::OK, fidl::MakeOptional(std::move(buffer)));
        });
  });
}

void PageCloudImpl::SetWatcher(std::unique_ptr<cloud_provider::PositionToken> min_position_token,
                               fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
                               SetWatcherCallback callback) {
  std::unique_ptr<google::protobuf::Timestamp> timestamp_or_null;
  if (min_position_token) {
    timestamp_or_null = std::make_unique<google::protobuf::Timestamp>();
    if (!timestamp_or_null->ParseFromString(convert::ToString(min_position_token->opaque_id))) {
      callback(cloud_provider::Status::ARGUMENT_ERROR);
      return;
    }
  }

  watcher_ = watcher.Bind();
  watcher_.set_error_handler([this](zx_status_t status) { ShutDownWatcher(); });
  watcher_timestamp_or_null_ = std::move(timestamp_or_null);
  set_watcher_callback_ = std::move(callback);

  ScopedGetCredentials([this](auto call_credentials) mutable {
    // Initiate the listen RPC. We will receive a call on OnConnected() when the
    // listen stream is ready.
    listen_call_handler_ = firestore_service_->Listen(std::move(call_credentials), this);
  });
}

void PageCloudImpl::GetDiff(std::vector<uint8_t> commit_id,
                            std::vector<std::vector<uint8_t>> possible_bases,
                            GetDiffCallback callback) {
  callback(cloud_provider::Status::NOT_SUPPORTED, {});
}

void PageCloudImpl::OnConnected() {
  auto request = google::firestore::v1beta1::ListenRequest();
  request.set_database(firestore_service_->GetDatabasePath());
  google::firestore::v1beta1::Target::QueryTarget& query_target =
      *request.mutable_add_target()->mutable_query();
  query_target.set_parent(page_path_);
  auto query = MakeCommitQuery(std::move(watcher_timestamp_or_null_));
  query_target.mutable_structured_query()->Swap(&query);
  listen_call_handler_->Write(std::move(request));
}

void PageCloudImpl::OnResponse(google::firestore::v1beta1::ListenResponse response) {
  if (response.has_target_change()) {
    if (response.target_change().target_change_type() ==
        google::firestore::v1beta1::TargetChange_TargetChangeType_CURRENT) {
      if (set_watcher_callback_) {
        set_watcher_callback_(cloud_provider::Status::OK);
        set_watcher_callback_ = nullptr;
      }
    }
    return;
  }

  if (response.has_document_change()) {
    std::string timestamp;

    std::vector<cloud_provider::CommitPackEntry> commit_entries;
    if (!DecodeCommitBatch(response.document_change().document(), &commit_entries, &timestamp)) {
      watcher_->OnError(cloud_provider::Status::PARSE_ERROR);
      ShutDownWatcher();
    }

    cloud_provider::PositionToken token;
    token.opaque_id = convert::ToArray(timestamp);
    HandleCommits(std::move(commit_entries), std::move(token));
  }
}

void PageCloudImpl::OnFinished(grpc::Status status) {
  if (status.error_code() == grpc::UNAVAILABLE || status.error_code() == grpc::UNAUTHENTICATED) {
    if (watcher_) {
      watcher_->OnError(cloud_provider::Status::NETWORK_ERROR);
    }
    return;
  }
  LogGrpcConnectionError(status);
  watcher_.Unbind();
}

void PageCloudImpl::HandleCommits(std::vector<cloud_provider::CommitPackEntry> commit_entries,
                                  cloud_provider::PositionToken token) {
  std::move(commit_entries.begin(), commit_entries.end(),
            std::back_inserter(commits_waiting_for_ack_));
  token_for_waiting_commits_ = std::move(token);

  if (!waiting_for_watcher_to_ack_commits_) {
    SendWaitingCommits();
  }
}

void PageCloudImpl::SendWaitingCommits() {
  FXL_DCHECK(watcher_);
  FXL_DCHECK(!commits_waiting_for_ack_.empty());
  cloud_provider::PositionToken token = std::move(token_for_waiting_commits_);
  cloud_provider::CommitPack commit_pack;
  if (!cloud_provider::EncodeCommitPack(commits_waiting_for_ack_, &commit_pack)) {
    watcher_->OnError(cloud_provider::Status::INTERNAL_ERROR);
    ShutDownWatcher();
    return;
  }
  watcher_->OnNewCommits(std::move(commit_pack), std::move(token), [this] {
    waiting_for_watcher_to_ack_commits_ = false;
    if (!commits_waiting_for_ack_.empty()) {
      SendWaitingCommits();
    }
  });
  waiting_for_watcher_to_ack_commits_ = true;
  commits_waiting_for_ack_.clear();
}

void PageCloudImpl::ShutDownWatcher() {
  if (watcher_) {
    watcher_.Unbind();
  }
  if (listen_call_handler_) {
    listen_call_handler_.reset();
  }
}

}  // namespace cloud_provider_firestore
