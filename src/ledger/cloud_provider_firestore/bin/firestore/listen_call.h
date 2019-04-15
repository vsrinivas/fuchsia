// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_FIRESTORE_LISTEN_CALL_H_
#define SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_FIRESTORE_LISTEN_CALL_H_

#include <google/firestore/v1beta1/firestore.grpc.pb.h>
#include <grpc++/grpc++.h>
#include <lib/fit/function.h>

#include <memory>

#include "src/ledger/cloud_provider_firestore/bin/firestore/listen_call_client.h"
#include "src/ledger/cloud_provider_firestore/bin/grpc/stream_controller.h"
#include "src/ledger/cloud_provider_firestore/bin/grpc/stream_reader.h"
#include "src/ledger/cloud_provider_firestore/bin/grpc/stream_writer.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

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

  void set_on_empty(fit::closure on_empty) { on_empty_ = std::move(on_empty); }

  void Write(google::firestore::v1beta1::ListenRequest request);

  void OnHandlerGone();

  std::unique_ptr<ListenCallHandler> MakeHandler();

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

  fit::closure on_empty_;

  bool connected_ = false;
  bool finish_requested_ = false;

  // Must be the last member.
  fxl::WeakPtrFactory<ListenCall> weak_ptr_factory_;
};

}  // namespace cloud_provider_firestore

#endif  // SRC_LEDGER_CLOUD_PROVIDER_FIRESTORE_BIN_FIRESTORE_LISTEN_CALL_H_
