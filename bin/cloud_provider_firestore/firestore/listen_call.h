// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_LISTEN_CALL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_LISTEN_CALL_H_

#include <functional>
#include <memory>

#include <google/firestore/v1beta1/firestore.grpc.pb.h>
#include <grpc++/grpc++.h>

#include "lib/fxl/functional/closure.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/cloud_provider_firestore/firestore/listen_call_client.h"

namespace cloud_provider_firestore {

using ListenStream = grpc::ClientAsyncReaderWriterInterface<
    google::firestore::v1beta1::ListenRequest,
    google::firestore::v1beta1::ListenResponse>;

class ListenCall {
 public:
  // Creates a new instance.
  //
  // |stream_factory| is used within the constructor and not retained.
  ListenCall(
      ListenCallClient* client,
      std::function<std::unique_ptr<ListenStream>(grpc::ClientContext* context,
                                                  void* tag)> stream_factory);
  ~ListenCall();

  void set_on_empty(fxl::Closure on_empty) { on_empty_ = std::move(on_empty); }

  void Write(google::firestore::v1beta1::ListenRequest request);

  void OnHandlerGone();

 private:
  void ReadNext();

  void FinishIfNeeded();

  void Finish();

  void HandleFinished(grpc::Status status);

  void CheckEmpty();

  // Context used to make the remote call.
  grpc::ClientContext context_;

  // Pointer to the client of the call. It is unset when the call handler is
  // deleted.
  ListenCallClient* client_;

  fxl::Closure on_empty_;

  bool connected_ = false;
  bool finish_requested_ = false;

  // Count of pending async tasks posted on the completion queue. The class
  // cannot be deleted until the count is 0, because the posted tasks reference
  // member callables as completion tags.
  int pending_cq_operations_ = 0;

  std::function<void(bool)> on_connected_;
  std::function<void(bool)> on_read_;
  std::function<void(bool)> on_write_;
  std::function<void(bool)> on_finish_;

  // Most recent response from the response stream.
  google::firestore::v1beta1::ListenResponse response_;

  // Final status of the stream set by the server.
  grpc::Status status_;

  // gRPC stream handler.
  std::unique_ptr<ListenStream> stream_;
};

class ListenCallHandlerImpl : public ListenCallHandler {
 public:
  explicit ListenCallHandlerImpl(ListenCall* call) : call_(call) {}

  ~ListenCallHandlerImpl() override { call_->OnHandlerGone(); }

  void Write(google::firestore::v1beta1::ListenRequest request) override {
    call_->Write(std::move(request));
  }

 private:
  ListenCall* const call_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ListenCallHandlerImpl);
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_FIRESTORE_LISTEN_CALL_H_
