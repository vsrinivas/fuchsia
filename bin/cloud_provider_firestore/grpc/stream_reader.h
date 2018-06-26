// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_GRPC_STREAM_READER_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_GRPC_STREAM_READER_H_

#include <functional>

#include <grpc++/grpc++.h>

#include "lib/fxl/logging.h"

namespace cloud_provider_firestore {

// Handler for gRPC read streams.
//
// |GrpcStream| template type can be any class inheriting from
// grpc::internal::AsyncReaderInterface.
template <typename GrpcStream, typename Message>
class StreamReader {
 public:
  StreamReader(GrpcStream* grpc_stream) : grpc_stream_(grpc_stream) {
    FXL_DCHECK(grpc_stream_);

    on_read_ = [this](bool ok) {
      read_is_pending_ = false;
      OnRead(ok);
    };
  }

  ~StreamReader() {
    // The class cannot go away while completion queue operations are pending,
    // as they reference member function objects as operation tags.
    FXL_DCHECK(!read_is_pending_);
  }

  bool IsEmpty() const { return !read_is_pending_; }

  // Sets a callback which is called when a read operation fails.
  //
  // This error is unrecoverable and means that there is no more messages to
  // read or that the connection is broken.
  void SetOnError(std::function<void()> on_error) {
    FXL_DCHECK(on_error);
    on_error_ = std::move(on_error);
  }

  // Sets a callback which is called each time a message is read.
  void SetOnMessage(std::function<void(Message)> on_message) {
    FXL_DCHECK(on_message);
    on_message_ = std::move(on_message);
  }

  // Attempts to read a message from the stream.
  //
  // SetOnError() and SetOnMessage() must be called before calling Read() for
  // the first time.
  //
  // Cannot be called while another read is already pending.
  void Read() {
    FXL_DCHECK(on_error_);
    FXL_DCHECK(on_message_);

    FXL_DCHECK(!read_is_pending_);
    read_is_pending_ = true;
    grpc_stream_->Read(&message_, &on_read_);
  }

 private:
  void OnRead(bool ok) {
    if (!ok) {
      FXL_LOG(ERROR) << "Read failed, closing the stream.";
      on_error_();
      return;
    }

    on_message_(std::move(message_));
  }

  // gRPC stream handler.
  GrpcStream* const grpc_stream_;

  // Whether a read operation is currently in progress.
  bool read_is_pending_ = false;

  // Callables posted as CompletionQueue tags:
  std::function<void(bool)> on_read_;

  // Internal callables not posted on CompletionQueue:
  std::function<void()> on_error_;
  std::function<void(Message)> on_message_;

  Message message_;
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_GRPC_STREAM_READER_H_
