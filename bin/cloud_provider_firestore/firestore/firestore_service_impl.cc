// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/firestore/firestore_service_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>

namespace cloud_provider_firestore {

namespace {
// Handles the general case of call response that returns a status and a
// response value.
template <typename ResponseType>
struct ResponseVariant {
  using CallbackType = fit::function<void(grpc::Status, ResponseType)>;

  static void Call(const CallbackType& callback, grpc::Status status,
                   ResponseType response) {
    callback(std::move(status), std::move(response));
  }
};

// Handles a special case of response type being empty, in which case we skip
// the google::protobuf::Empty value and only pass the status to the caller.
template <>
struct ResponseVariant<google::protobuf::Empty> {
  using CallbackType = fit::function<void(grpc::Status)>;

  static void Call(const CallbackType& callback, grpc::Status status,
                   google::protobuf::Empty /*response*/) {
    callback(std::move(status));
  }
};

template <typename ResponseType>
void MakeCall(
    SingleResponseCall<ResponseType>* call,
    std::unique_ptr<SingleResponseReader<ResponseType>> response_reader,
    typename ResponseVariant<ResponseType>::CallbackType callback) {
  call->response_reader = std::move(response_reader);

  call->on_complete = [call, callback = std::move(callback)](bool ok) {
    ResponseVariant<ResponseType>::Call(callback, std::move(call->status),
                                        std::move(call->response));
    if (call->on_empty) {
      call->on_empty();
    }
  };
  call->response_reader->Finish(&call->response, &call->status,
                                &call->on_complete);
}

}  // namespace

FirestoreServiceImpl::FirestoreServiceImpl(
    std::string server_id, async_dispatcher_t* dispatcher,
    std::shared_ptr<grpc::Channel> channel)
    : server_id_(std::move(server_id)),
      database_path_("projects/" + server_id_ + "/databases/(default)"),
      root_path_(database_path_ + "/documents"),
      dispatcher_(dispatcher),
      firestore_(google::firestore::v1beta1::Firestore::NewStub(channel)) {
  polling_thread_ = std::thread(&FirestoreServiceImpl::Poll, this);
}

FirestoreServiceImpl::~FirestoreServiceImpl() {}

void FirestoreServiceImpl::GetDocument(
    google::firestore::v1beta1::GetDocumentRequest request,
    std::shared_ptr<grpc::CallCredentials> call_credentials,
    fit::function<void(grpc::Status, google::firestore::v1beta1::Document)>
        callback) {
  FXL_DCHECK(dispatcher_ == async_get_default_dispatcher());
  DocumentResponseCall& call = document_response_calls_.emplace();
  call.context.set_credentials(call_credentials);
  auto response_reader =
      firestore_->AsyncGetDocument(&call.context, request, &cq_);
  MakeCall<google::firestore::v1beta1::Document>(
      &call, std::move(response_reader), std::move(callback));
}

void FirestoreServiceImpl::ListDocuments(
    google::firestore::v1beta1::ListDocumentsRequest request,
    std::shared_ptr<grpc::CallCredentials> call_credentials,
    fit::function<void(grpc::Status,
                       google::firestore::v1beta1::ListDocumentsResponse)>
        callback) {
  FXL_DCHECK(dispatcher_ == async_get_default_dispatcher());
  ListDocumentsResponseCall& call = list_documents_response_calls_.emplace();
  call.context.set_credentials(call_credentials);
  auto response_reader =
      firestore_->AsyncListDocuments(&call.context, request, &cq_);
  MakeCall<google::firestore::v1beta1::ListDocumentsResponse>(
      &call, std::move(response_reader), std::move(callback));
}

void FirestoreServiceImpl::CreateDocument(
    google::firestore::v1beta1::CreateDocumentRequest request,
    std::shared_ptr<grpc::CallCredentials> call_credentials,
    fit::function<void(grpc::Status, google::firestore::v1beta1::Document)>
        callback) {
  FXL_DCHECK(dispatcher_ == async_get_default_dispatcher());
  DocumentResponseCall& call = document_response_calls_.emplace();
  call.context.set_credentials(call_credentials);
  auto response_reader =
      firestore_->AsyncCreateDocument(&call.context, request, &cq_);

  MakeCall<google::firestore::v1beta1::Document>(
      &call, std::move(response_reader), std::move(callback));
}

void FirestoreServiceImpl::DeleteDocument(
    google::firestore::v1beta1::DeleteDocumentRequest request,
    std::shared_ptr<grpc::CallCredentials> call_credentials,
    fit::function<void(grpc::Status)> callback) {
  FXL_DCHECK(dispatcher_ == async_get_default_dispatcher());
  EmptyResponseCall& call = empty_response_calls_.emplace();
  call.context.set_credentials(call_credentials);
  auto response_reader =
      firestore_->AsyncDeleteDocument(&call.context, request, &cq_);

  MakeCall<google::protobuf::Empty>(&call, std::move(response_reader),
                                    std::move(callback));
}

void FirestoreServiceImpl::Commit(
    google::firestore::v1beta1::CommitRequest request,
    std::shared_ptr<grpc::CallCredentials> call_credentials,
    fit::function<void(grpc::Status,
                       google::firestore::v1beta1::CommitResponse)>
        callback) {
  FXL_DCHECK(dispatcher_ == async_get_default_dispatcher());
  CommitResponseCall& call = commit_response_calls_.emplace();
  call.context.set_credentials(call_credentials);
  auto response_reader = firestore_->AsyncCommit(&call.context, request, &cq_);

  MakeCall<google::firestore::v1beta1::CommitResponse>(
      &call, std::move(response_reader), std::move(callback));
}

void FirestoreServiceImpl::RunQuery(
    google::firestore::v1beta1::RunQueryRequest request,
    std::shared_ptr<grpc::CallCredentials> call_credentials,
    fit::function<
        void(grpc::Status,
             std::vector<google::firestore::v1beta1::RunQueryResponse>)>
        callback) {
  FXL_DCHECK(dispatcher_ == async_get_default_dispatcher());
  auto context = std::make_unique<grpc::ClientContext>();
  context->set_credentials(call_credentials);

  auto stream = firestore_->PrepareAsyncRunQuery(context.get(), request, &cq_);
  auto& call = run_query_calls_.emplace(std::move(context), std::move(stream));
  call.Drain(std::move(callback));
}

std::unique_ptr<ListenCallHandler> FirestoreServiceImpl::Listen(
    std::shared_ptr<grpc::CallCredentials> call_credentials,
    ListenCallClient* client) {
  FXL_DCHECK(dispatcher_ == async_get_default_dispatcher());
  auto context = std::make_unique<grpc::ClientContext>();
  context->set_credentials(call_credentials);

  auto stream = firestore_->PrepareAsyncListen(context.get(), &cq_);
  auto& call =
      listen_calls_.emplace(client, std::move(context), std::move(stream));
  return std::make_unique<ListenCallHandlerImpl>(&call);
}

void FirestoreServiceImpl::ShutDown(fit::closure callback) {
  // Ask the CompletionQueue to shut down. This makes cq_.Next() to start
  // returning false once the pending operations are drained.
  cq_.Shutdown();

  // Wait for the polling thread to exit.
  polling_thread_.join();

  // The CQ polling thread might have posted new tasks on the main thread before
  // exiting, completing the calls that were active when we initated the
  // CompletionQueue shut down. These must be processed before we call the
  // client callback, so post the client callback on the main thread too.
  async::PostTask(dispatcher_, std::move(callback));
}

void FirestoreServiceImpl::Poll() {
  void* tag;
  bool ok = false;
  while (cq_.Next(&tag, &ok)) {
    FXL_DCHECK(tag);
    auto callable = reinterpret_cast<fit::function<void(bool)>*>(tag);
    async::PostTask(dispatcher_, [callable, ok] { (*callable)(ok); });
  }
}

}  // namespace cloud_provider_firestore
