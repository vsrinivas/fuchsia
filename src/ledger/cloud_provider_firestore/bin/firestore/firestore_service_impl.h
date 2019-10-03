// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_FIRESTORE_FIRESTORE_SERVICE_IMPL_H_
#define SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_FIRESTORE_FIRESTORE_SERVICE_IMPL_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include <memory>
#include <thread>

#include "src/ledger/cloud_provider_firestore/bin/firestore/firestore_service.h"
#include "src/ledger/cloud_provider_firestore/bin/firestore/listen_call.h"
#include "src/ledger/cloud_provider_firestore/bin/grpc/read_stream_drainer.h"
#include "src/lib/callback/auto_cleanable.h"

namespace cloud_provider_firestore {

template <typename ResponseType>
using SingleResponseReader = grpc::ClientAsyncResponseReader<ResponseType>;

template <typename ResponseType>
class SingleResponseCall {
 public:
  void SetOnDiscardable(fit::closure on_discardable) {
    on_discardable_ = std::move(on_discardable);
  }

  bool IsDiscardable() const { return discardable_; }

  void Done() {
    discardable_ = true;
    if (on_discardable_) {
      on_discardable_();
    }
  }

  // Context used to make the remote call.
  grpc::ClientContext& context() { return context_; }

  // Reader used to retrieve the result of the remote call.
  std::unique_ptr<SingleResponseReader<ResponseType>>& response_reader() {
    return response_reader_;
  }

  // Response of the remote call.
  ResponseType& response() { return response_; }

  // Response status of the remote call.
  grpc::Status& status() { return status_; }

  // Callback to be called upon completing the remote call.
  fit::function<void(bool)>& on_complete() { return on_complete_; }

 private:
  // Context used to make the remote call.
  grpc::ClientContext context_;

  // Reader used to retrieve the result of the remote call.
  std::unique_ptr<SingleResponseReader<ResponseType>> response_reader_;

  // Response of the remote call.
  ResponseType response_;

  // Response status of the remote call.
  grpc::Status status_;

  // Callback to be called upon completing the remote call.
  fit::function<void(bool)> on_complete_;

  // Callback to be called when the call object can be deleted.
  fit::closure on_discardable_;
  bool discardable_ = false;
};

using DocumentResponseCall = SingleResponseCall<google::firestore::v1beta1::Document>;

using CommitResponseCall = SingleResponseCall<google::firestore::v1beta1::CommitResponse>;

using ListDocumentsResponseCall =
    SingleResponseCall<google::firestore::v1beta1::ListDocumentsResponse>;

using EmptyResponseCall = SingleResponseCall<google::protobuf::Empty>;

using RunQueryCall = ReadStreamDrainer<
    grpc::ClientAsyncReaderInterface<google::firestore::v1beta1::RunQueryResponse>,
    google::firestore::v1beta1::RunQueryResponse>;

// Polls results of gRPC calls from a source that resembles gRPC completion
// queue Next() method. Extracted into a separate free function for testability.
void PollEvents(const std::function<bool(void** tag, bool* ok)>& get_next_tag,
                async_dispatcher_t* dispatcher);

// Implementation of the FirestoreService interface.
//
// This class is implemented as a wrapper over the Firestore connection. We use
// a polling thread to wait for request completion on the completion queue and
// expose a callback-based API to the client.
class FirestoreServiceImpl : public FirestoreService {
 public:
  FirestoreServiceImpl(std::string server_id, async_dispatcher_t* dispatcher,
                       std::shared_ptr<grpc::Channel> channel);

  ~FirestoreServiceImpl() override;

  // FirestoreService:
  const std::string& GetDatabasePath() override { return database_path_; }

  const std::string& GetRootPath() override { return root_path_; }

  void GetDocument(
      google::firestore::v1beta1::GetDocumentRequest request,
      std::shared_ptr<grpc::CallCredentials> call_credentials,
      fit::function<void(grpc::Status, google::firestore::v1beta1::Document)> callback) override;

  void ListDocuments(
      google::firestore::v1beta1::ListDocumentsRequest request,
      std::shared_ptr<grpc::CallCredentials> call_credentials,
      fit::function<void(grpc::Status, google::firestore::v1beta1::ListDocumentsResponse)> callback)
      override;

  void CreateDocument(
      google::firestore::v1beta1::CreateDocumentRequest request,
      std::shared_ptr<grpc::CallCredentials> call_credentials,
      fit::function<void(grpc::Status, google::firestore::v1beta1::Document)> callback) override;

  void DeleteDocument(google::firestore::v1beta1::DeleteDocumentRequest request,
                      std::shared_ptr<grpc::CallCredentials> call_credentials,
                      fit::function<void(grpc::Status)> callback) override;

  void Commit(google::firestore::v1beta1::CommitRequest request,
              std::shared_ptr<grpc::CallCredentials> call_credentials,
              fit::function<void(grpc::Status, google::firestore::v1beta1::CommitResponse)>
                  callback) override;

  void RunQuery(
      google::firestore::v1beta1::RunQueryRequest request,
      std::shared_ptr<grpc::CallCredentials> call_credentials,
      fit::function<void(grpc::Status, std::vector<google::firestore::v1beta1::RunQueryResponse>)>
          callback) override;

  std::unique_ptr<ListenCallHandler> Listen(std::shared_ptr<grpc::CallCredentials> call_credentials,
                                            ListenCallClient* client) override;

  void ShutDown(fit::closure callback) override;

 private:
  void SetUpContext(grpc::ClientContext* context,
                    std::shared_ptr<grpc::CallCredentials> call_credentials);

  void Poll();

  const std::string server_id_;
  const std::string database_path_;
  const std::string root_path_;

  async_dispatcher_t* const dispatcher_;
  std::thread polling_thread_;

  std::unique_ptr<google::firestore::v1beta1::Firestore::Stub> firestore_;
  grpc::CompletionQueue cq_;

  // Single-request single-response calls.
  callback::AutoCleanableSet<DocumentResponseCall> document_response_calls_;
  callback::AutoCleanableSet<CommitResponseCall> commit_response_calls_;
  callback::AutoCleanableSet<ListDocumentsResponseCall> list_documents_response_calls_;
  callback::AutoCleanableSet<EmptyResponseCall> empty_response_calls_;

  // Single-request stream-response calls.
  callback::AutoCleanableSet<RunQueryCall> run_query_calls_;

  // Stream-request stream-response calls.
  callback::AutoCleanableSet<ListenCall> listen_calls_;
};

}  // namespace cloud_provider_firestore

#endif  // SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_FIRESTORE_FIRESTORE_SERVICE_IMPL_H_
