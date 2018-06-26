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
#include "peridot/bin/cloud_provider_firestore/grpc/stream_controller.h"
#include "peridot/bin/cloud_provider_firestore/grpc/stream_reader.h"
#include "peridot/bin/cloud_provider_firestore/grpc/stream_writer.h"

namespace cloud_provider_firestore {

using ListenStream = grpc::ClientAsyncReaderWriterInterface<
    google::firestore::v1beta1::ListenRequest,
    google::firestore::v1beta1::ListenResponse>;

class ListenCall {
 public:
  // Creates a new instance.
  ListenCall(ListenCallClient* client,
             std::unique_ptr<grpc::ClientContext> context,
             std::unique_ptr<ListenStream> stream);
  ~ListenCall();

  void set_on_empty(fxl::Closure on_empty) { on_empty_ = std::move(on_empty); }

  void Write(google::firestore::v1beta1::ListenRequest request);

  void OnHandlerGone();

 private:
  void FinishIfNeeded();

  void Finish();

  void HandleFinished(grpc::Status status);

  bool IsEmpty();

  bool CheckEmpty();

  // Pointer to the client of the call. It is unset when the call handler is
  // deleted.
  ListenCallClient* client_;

  // Context used to make the remote call.
  std::unique_ptr<grpc::ClientContext> context_;

  // gRPC stream handler.
  std::unique_ptr<ListenStream> stream_;

  StreamController<ListenStream> stream_controller_;
  StreamReader<ListenStream, google::firestore::v1beta1::ListenResponse>
      stream_reader_;
  StreamWriter<ListenStream, google::firestore::v1beta1::ListenRequest>
      stream_writer_;

  fxl::Closure on_empty_;

  bool connected_ = false;
  bool finish_requested_ = false;
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
