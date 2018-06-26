// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_GRPC_READ_STREAM_DRAINER_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_GRPC_READ_STREAM_DRAINER_H_

#include <functional>
#include <memory>
#include <vector>

#include <grpc++/grpc++.h>

#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/cloud_provider_firestore/grpc/stream_controller.h"
#include "peridot/bin/cloud_provider_firestore/grpc/stream_reader.h"

namespace cloud_provider_firestore {

// Utility which can drain a read-only grpc::Stream and return the messages.
//
// |GrpcStream| template type can be any class inheriting from
// grpc::internal::AsyncReaderInterface.
template <typename GrpcStream, typename Message>
class ReadStreamDrainer {
 public:
  // Creates a new instance.
  ReadStreamDrainer(std::unique_ptr<grpc::ClientContext> context,
                    std::unique_ptr<GrpcStream> stream)
      : context_(std::move(context)),
        stream_(std::move(stream)),
        stream_controller_(stream_.get()),
        stream_reader_(stream_.get()) {}
  ~ReadStreamDrainer() {}

  void set_on_empty(fxl::Closure on_empty) { on_empty_ = std::move(on_empty); }

  // Reads messages from the stream until there is no more messages to read and
  // returns all the messages to the caller.
  //
  // Can be called at most once.
  void Drain(std::function<void(grpc::Status, std::vector<Message>)> callback) {
    FXL_DCHECK(!callback_);
    callback_ = std::move(callback);
    stream_controller_.StartCall([this](bool ok) {
      if (!ok) {
        Finish();
        return;
      }

      OnConnected();
    });
  }

 private:
  void OnConnected() {
    // Configure the stream reader.
    stream_reader_.SetOnError([this] { Finish(); });
    stream_reader_.SetOnMessage([this](Message message) {
      messages_.push_back(std::move(message));
      stream_reader_.Read();
    });

    // Start reading.
    stream_reader_.Read();
  }

  void Finish() {
    stream_controller_.Finish([this](bool ok, grpc::Status status) {
      if (status.ok()) {
        callback_(status, std::move(messages_));
      } else {
        callback_(status, std::vector<Message>{});
      }

      if (on_empty_) {
        on_empty_();
      }
    });
  }

  // Context used to make the remote call.
  std::unique_ptr<grpc::ClientContext> context_;

  // gRPC stream handler.
  std::unique_ptr<GrpcStream> stream_;

  StreamController<GrpcStream> stream_controller_;
  StreamReader<GrpcStream, Message> stream_reader_;

  fxl::Closure on_empty_;
  std::vector<Message> messages_;
  std::function<void(grpc::Status, std::vector<Message>)> callback_;
  FXL_DISALLOW_COPY_AND_ASSIGN(ReadStreamDrainer);
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_GRPC_READ_STREAM_DRAINER_H_
