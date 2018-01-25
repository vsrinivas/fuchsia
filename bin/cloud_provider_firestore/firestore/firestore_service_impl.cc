// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/firestore/firestore_service_impl.h"

namespace cloud_provider_firestore {

namespace {
// Handles the general case of call response that returns a status and a
// response value.
template <typename ResponseType>
struct ResponseVariant {
  using CallbackType = std::function<void(grpc::Status, ResponseType)>;

  static void Call(const CallbackType& callback,
                   grpc::Status status,
                   ResponseType response) {
    callback(std::move(status), std::move(response));
  }
};

// Handles a special case of response type being empty, in which case we skip
// the google::protobuf::Empty value and only pass the status to the caller.
template <>
struct ResponseVariant<google::protobuf::Empty> {
  using CallbackType = std::function<void(grpc::Status)>;

  static void Call(const CallbackType& callback,
                   grpc::Status status,
                   google::protobuf::Empty response) {
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
    std::string server_id,
    fxl::RefPtr<fxl::TaskRunner> main_runner,
    std::shared_ptr<grpc::Channel> channel)
    : server_id_(std::move(server_id)),
      database_path_("projects/" + server_id_ + "/databases/(default)"),
      root_path_(database_path_ + "/documents"),
      main_runner_(std::move(main_runner)),
      firestore_(google::firestore::v1beta1::Firestore::NewStub(channel)) {
  polling_thread_ = std::thread(&FirestoreServiceImpl::Poll, this);
}

FirestoreServiceImpl::~FirestoreServiceImpl() {
  cq_.Shutdown();
  polling_thread_.join();
}

void FirestoreServiceImpl::GetDocument(
    google::firestore::v1beta1::GetDocumentRequest request,
    std::shared_ptr<grpc::CallCredentials> call_credentials,
    std::function<void(grpc::Status, google::firestore::v1beta1::Document)>
        callback) {
  FXL_DCHECK(main_runner_->RunsTasksOnCurrentThread());
  DocumentResponseCall& call = document_response_calls_.emplace();
  call.context.set_credentials(call_credentials);
  auto response_reader =
      firestore_->AsyncGetDocument(&call.context, std::move(request), &cq_);
  MakeCall<google::firestore::v1beta1::Document>(
      &call, std::move(response_reader), std::move(callback));
}

void FirestoreServiceImpl::CreateDocument(
    google::firestore::v1beta1::CreateDocumentRequest request,
    std::shared_ptr<grpc::CallCredentials> call_credentials,
    std::function<void(grpc::Status, google::firestore::v1beta1::Document)>
        callback) {
  FXL_DCHECK(main_runner_->RunsTasksOnCurrentThread());
  DocumentResponseCall& call = document_response_calls_.emplace();
  call.context.set_credentials(call_credentials);
  auto response_reader =
      firestore_->AsyncCreateDocument(&call.context, std::move(request), &cq_);

  MakeCall<google::firestore::v1beta1::Document>(
      &call, std::move(response_reader), std::move(callback));
}

void FirestoreServiceImpl::DeleteDocument(
    google::firestore::v1beta1::DeleteDocumentRequest request,
    std::shared_ptr<grpc::CallCredentials> call_credentials,
    std::function<void(grpc::Status)> callback) {
  FXL_DCHECK(main_runner_->RunsTasksOnCurrentThread());
  EmptyResponseCall& call = empty_response_calls_.emplace();
  call.context.set_credentials(call_credentials);
  auto response_reader =
      firestore_->AsyncDeleteDocument(&call.context, std::move(request), &cq_);

  MakeCall<google::protobuf::Empty>(&call, std::move(response_reader),
                                    std::move(callback));
}

std::unique_ptr<ListenCallHandler> FirestoreServiceImpl::Listen(
    std::shared_ptr<grpc::CallCredentials> call_credentials,
    ListenCallClient* client) {
  FXL_DCHECK(main_runner_->RunsTasksOnCurrentThread());

  auto stream_factory = [cq = &cq_, firestore = firestore_.get(),
                         call_credentials = std::move(call_credentials)](
                            grpc::ClientContext* context, void* tag) {
    context->set_credentials(call_credentials);
    return firestore->AsyncListen(context, cq, tag);
  };
  auto& call = listen_calls_.emplace(client, std::move(stream_factory));
  return std::make_unique<ListenCallHandlerImpl>(&call);
}

void FirestoreServiceImpl::Poll() {
  void* tag;
  bool ok = false;
  while (cq_.Next(&tag, &ok)) {
    FXL_DCHECK(tag);
    auto callable = reinterpret_cast<std::function<void(bool)>*>(tag);
    main_runner_->PostTask([callable, ok] { (*callable)(ok); });
  }
}

}  // namespace cloud_provider_firestore
