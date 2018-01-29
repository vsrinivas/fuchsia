// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/firestore/listen_call.h"

namespace cloud_provider_firestore {

ListenCall::ListenCall(
    ListenCallClient* client,
    std::function<std::unique_ptr<ListenStream>(grpc::ClientContext* context,
                                                void* tag)> stream_factory)
    : client_(client) {
  on_connected_ = [this](bool ok) {
    pending_cq_operations_--;
    if (!ok) {
      FXL_LOG(ERROR) << "Failed to establish the stream.";
      HandleFinished(grpc::Status(grpc::StatusCode::UNKNOWN, "unknown"));
      return;
    }

    // If the stream was successfully connected, but the client went away in the
    // meantime, we should close it.
    if (!client_) {
      FinishIfNeeded();
      return;
    }

    // Notify the client that the connection is now established and start
    // reading the server stream.
    connected_ = true;
    client_->OnConnected();
    ReadNext();
  };

  on_read_ = [this](bool ok) {
    pending_cq_operations_--;
    if (!client_) {
      CheckEmpty();
      return;
    }

    if (!ok) {
      FXL_LOG(ERROR) << "Read failed, closing the stream.";
      FinishIfNeeded();
      return;
    }

    client_->OnResponse(std::move(response_));
    ReadNext();
  };

  on_write_ = [this](bool ok) {
    pending_cq_operations_--;
    if (!client_) {
      CheckEmpty();
      return;
    }

    if (!ok) {
      FXL_LOG(ERROR) << "Write failed, closing the stream.";
      FinishIfNeeded();
    }
  };

  on_finish_ = [this](bool ok) {
    pending_cq_operations_--;
    if (!client_) {
      CheckEmpty();
      return;
    }

    if (!ok) {
      FXL_LOG(ERROR) << "Failed to retrieve the final status of the stream";
      HandleFinished(grpc::Status(grpc::StatusCode::UNKNOWN, "unknown"));
      return;
    }

    HandleFinished(status_);
  };

  stream_ = stream_factory(&context_, &on_connected_);
  pending_cq_operations_++;
}

ListenCall::~ListenCall() {
  // The class cannot go away while completion queue operations are pending, as
  // they reference member function objects as operation tags.
  FXL_DCHECK(pending_cq_operations_ == 0);
}

void ListenCall::Write(google::firestore::v1beta1::ListenRequest request) {
  // It's only valid to perform a write after the connection was established,
  // and before the Finish() call was made.
  FXL_DCHECK(connected_);
  FXL_DCHECK(!finish_requested_);
  stream_->Write(request, &on_write_);
  pending_cq_operations_++;
}

void ListenCall::OnHandlerGone() {
  // Unset the |client_| pointer, so that no client notifications are made
  // after the handler is deleted.
  client_ = nullptr;

  context_.TryCancel();
  CheckEmpty();
}

void ListenCall::ReadNext() {
  // It's only valid to perform a read after the connection was established,
  // and before the Finish() call was made.
  FXL_DCHECK(connected_);
  FXL_DCHECK(!finish_requested_);
  stream_->Read(&response_, &on_read_);
  pending_cq_operations_++;
}

void ListenCall::FinishIfNeeded() {
  if (!finish_requested_ && client_) {
    Finish();
  } else {
    CheckEmpty();
  }
}

void ListenCall::Finish() {
  FXL_DCHECK(!finish_requested_);
  finish_requested_ = true;

  stream_->Finish(&status_, &on_finish_);
  pending_cq_operations_++;
}

void ListenCall::HandleFinished(grpc::Status status) {
  if (client_) {
    client_->OnFinished(status);
    // No client notifications can be delivered after |OnFinished|.
    client_ = nullptr;
  }
  CheckEmpty();
}

void ListenCall::CheckEmpty() {
  if (on_empty_ && pending_cq_operations_ == 0 && client_ == nullptr) {
    on_empty_();
  }
}
}  // namespace cloud_provider_firestore
