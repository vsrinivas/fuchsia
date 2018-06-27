// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_GRPC_STREAM_WRITER_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_GRPC_STREAM_WRITER_H_

#include <functional>

#include <grpc++/grpc++.h>
#include <lib/fit/function.h>

#include "lib/fxl/logging.h"

namespace cloud_provider_firestore {

// Handler for gRPC write streams.
//
// |GrpcStream| template type can be any class inheriting from
// grpc::internal::AsyncWriterInterface<Message>.
template <typename GrpcStream, typename Message>
class StreamWriter {
 public:
  StreamWriter(GrpcStream* grpc_stream) : grpc_stream_(grpc_stream) {
    FXL_DCHECK(grpc_stream_);

    on_write_ = [this](bool ok) {
      write_is_pending_ = false;
      OnWrite(ok);
    };
  }

  ~StreamWriter() {
    // The class cannot go away while completion queue operations are pending,
    // as they reference member function objects as operation tags.
    FXL_DCHECK(!write_is_pending_);
  }

  bool IsEmpty() const { return !write_is_pending_; }

  // Sets a callback which is called when a write operation fails.
  //
  // This error is unrecoverable and means that the write call cannot be made
  // because the connection is broken.
  void SetOnError(fit::function<void()> on_error) {
    on_error_ = std::move(on_error);
  }

  // Sets a callback which is called when a write operation succeeds.
  void SetOnSuccess(fit::function<void()> on_success) {
    on_success_ = std::move(on_success);
  }

  // Attempts to write a message to the stream.
  //
  // SetOnError() and SetOnSuccess() must be called before calling Read() for
  // the first time.
  //
  // Cannot be called while another write is already pending.
  void Write(Message message) {
    FXL_DCHECK(on_error_);
    FXL_DCHECK(on_success_);

    FXL_DCHECK(!write_is_pending_);
    write_is_pending_ = true;
    grpc_stream_->Write(message, &on_write_);
  }

 private:
  void OnWrite(bool ok) {
    if (!ok) {
      FXL_LOG(ERROR) << "Write failed, closing the stream.";
      on_error_();
      return;
    }

    on_success_();
  }

  // gRPC stream handler.
  GrpcStream* const grpc_stream_;

  // Whether a write operation is currently in progress.
  bool write_is_pending_ = false;

  // Callables posted as CompletionQueue tags:
  fit::function<void(bool)> on_write_;

  // Internal callables not posted on CompletionQueue:
  fit::function<void()> on_error_;
  fit::function<void()> on_success_;

  // Final status of the stream set by the server.
  grpc::Status status_;
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_GRPC_STREAM_WRITER_H_
