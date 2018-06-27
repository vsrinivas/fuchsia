// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_GRPC_STREAM_CONTROLLER_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_GRPC_STREAM_CONTROLLER_H_

#include <functional>

#include <grpc++/grpc++.h>
#include <lib/fit/function.h>

#include "lib/fxl/logging.h"

namespace cloud_provider_firestore {

// Handler common for all gRPC streams.
//
// |GrpcStream| template type can be any class inheriting from
// grpc::internal::ClientAsyncStreamingInterface.
template <typename GrpcStream>
class StreamController {
 public:
  StreamController(GrpcStream* grpc_stream) : grpc_stream_(grpc_stream) {
    FXL_DCHECK(grpc_stream_);
  }

  ~StreamController() {
    // The class cannot go away while completion queue operations are pending,
    // as they reference member function objects as operation tags.
    FXL_DCHECK(pending_cq_operations_ == 0);
  }

  bool IsEmpty() const { return pending_cq_operations_ == 0; }

  // Attempts to start the stream.
  void StartCall(fit::function<void(bool)> callback) {
    on_connected_ = [this, callback = std::move(callback)](bool ok) {
      pending_cq_operations_--;
      callback(ok);
    };

    pending_cq_operations_++;
    grpc_stream_->StartCall(&on_connected_);
  }

  // Attempts to finish the stream and read the final status.
  //
  // Note that calling Finish() by itself does *not* make any pending read/write
  // operations fail early. For that, call TryCancel() on the associated client
  // context.
  void Finish(fit::function<void(bool, grpc::Status)> callback) {
    on_finish_ = [this, callback = std::move(callback)](bool ok) {
      pending_cq_operations_--;
      callback(ok, status_);
    };

    pending_cq_operations_++;
    grpc_stream_->Finish(&status_, &on_finish_);
  }

 private:
  // gRPC stream handler.
  GrpcStream* const grpc_stream_;

  // Count of pending async tasks posted on the completion queue.
  int pending_cq_operations_ = 0;

  // Callables posted as CompletionQueue tags:
  fit::function<void(bool)> on_connected_;
  fit::function<void(bool)> on_finish_;

  // Final status of the stream set by the server.
  grpc::Status status_;
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_GRPC_STREAM_CONTROLLER_H_
