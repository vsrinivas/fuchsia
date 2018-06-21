// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/firestore/listen_call.h"

namespace cloud_provider_firestore {

ListenCall::ListenCall(ListenCallClient* client,
                       std::unique_ptr<grpc::ClientContext> context,
                       std::unique_ptr<ListenStream> stream)
    : client_(client),
      context_(std::move(context)),
      stream_(std::move(stream)),
      stream_controller_(stream_.get()),
      stream_reader_(stream_.get()),
      stream_writer_(stream_.get()) {
  // Configure reading from the stream.
  stream_reader_.SetOnError([this] { FinishIfNeeded(); });
  stream_reader_.SetOnMessage(
      [this](google::firestore::v1beta1::ListenResponse response) {
        if (CheckEmpty()) {
          return;
        }

        client_->OnResponse(std::move(response));
        if (finish_requested_) {
          return;
        }
        stream_reader_.Read();
      });

  // Configure writing to the stream.
  stream_writer_.SetOnError([this] { FinishIfNeeded(); });
  stream_writer_.SetOnSuccess([this] { CheckEmpty(); });

  // Finally, start the stream.
  stream_controller_.StartCall([this](bool ok) {
    if (!ok) {
      FXL_LOG(ERROR) << "Failed to establish the stream.";
      HandleFinished(grpc::Status(grpc::StatusCode::UNKNOWN, "unknown"));
      return;
    }

    if (CheckEmpty()) {
      return;
    }

    // Notify the client that the connection is now established and start
    // reading the server stream.
    connected_ = true;
    client_->OnConnected();
    stream_reader_.Read();
  });
}

ListenCall::~ListenCall() { FXL_DCHECK(IsEmpty()); }

void ListenCall::Write(google::firestore::v1beta1::ListenRequest request) {
  // It's only valid to perform a write after the connection was established,
  // and before the Finish() call was made.
  FXL_DCHECK(connected_);
  FXL_DCHECK(!finish_requested_);
  stream_writer_.Write(std::move(request));
}

void ListenCall::OnHandlerGone() {
  // Unset the |client_| pointer, so that no client notifications are made
  // after the handler is deleted.
  client_ = nullptr;

  context_->TryCancel();
  CheckEmpty();
}

void ListenCall::FinishIfNeeded() {
  if (!finish_requested_ && client_) {
    Finish();
    return;
  }

  CheckEmpty();
}

void ListenCall::Finish() {
  FXL_DCHECK(!finish_requested_);
  finish_requested_ = true;

  stream_controller_.Finish([this](bool ok, grpc::Status status) {
    if (!client_) {
      CheckEmpty();
      return;
    }

    if (!ok) {
      FXL_LOG(ERROR) << "Failed to retrieve the final status of the stream";
      HandleFinished(grpc::Status(grpc::StatusCode::UNKNOWN, "unknown"));
      return;
    }

    HandleFinished(status);
  });
}

void ListenCall::HandleFinished(grpc::Status status) {
  if (client_) {
    client_->OnFinished(status);
    // No client notifications can be delivered after |OnFinished|.
    client_ = nullptr;
  }
  CheckEmpty();
}

bool ListenCall::IsEmpty() {
  return client_ == nullptr && stream_controller_.IsEmpty() &&
         stream_reader_.IsEmpty() && stream_writer_.IsEmpty();
}

bool ListenCall::CheckEmpty() {
  if (!IsEmpty()) {
    return false;
  }

  if (on_empty_) {
    on_empty_();
  }
  return true;
}

}  // namespace cloud_provider_firestore
