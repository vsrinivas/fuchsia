// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_firestore/bin/firestore/listen_call.h"

namespace cloud_provider_firestore {

namespace {

class ListenCallHandlerImpl : public ListenCallHandler {
 public:
  explicit ListenCallHandlerImpl(fxl::WeakPtr<ListenCall> call) : call_(call) {}

  ~ListenCallHandlerImpl() override {
    if (!call_) {
      return;
    }
    call_->OnHandlerGone();
  }

  void Write(google::firestore::v1beta1::ListenRequest request) override {
    // It's an arror to call Write() after OnFinished() is delivered to the
    // client (which happens before |call_| is deleted).
    FXL_DCHECK(call_);
    call_->Write(std::move(request));
  }

 private:
  fxl::WeakPtr<ListenCall> const call_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ListenCallHandlerImpl);
};

}  // namespace

ListenCall::ListenCall(ListenCallClient* client, std::unique_ptr<grpc::ClientContext> context,
                       std::unique_ptr<ListenStream> stream)
    : client_(client),
      context_(std::move(context)),
      stream_(std::move(stream)),
      stream_controller_(stream_.get()),
      stream_reader_(stream_.get()),
      stream_writer_(stream_.get()),
      weak_ptr_factory_(this) {
  // Configure reading from the stream.
  stream_reader_.SetOnError([this] { FinishIfNeeded(); });
  stream_reader_.SetOnMessage([this](google::firestore::v1beta1::ListenResponse response) {
    if (CheckDiscardable()) {
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
  stream_writer_.SetOnSuccess([this] { CheckDiscardable(); });

  // Finally, start the stream.
  stream_controller_.StartCall([this](bool ok) {
    if (!ok) {
      FXL_LOG(ERROR) << "Failed to establish the stream.";
      HandleFinished(grpc::Status(grpc::StatusCode::UNKNOWN, "unknown"));
      return;
    }

    if (CheckDiscardable()) {
      return;
    }

    // Notify the client that the connection is now established and start
    // reading the server stream.
    connected_ = true;
    client_->OnConnected();
    stream_reader_.Read();
  });
}

ListenCall::~ListenCall() { FXL_DCHECK(IsDiscardable()); }

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
  CheckDiscardable();
}

std::unique_ptr<ListenCallHandler> ListenCall::MakeHandler() {
  return std::make_unique<ListenCallHandlerImpl>(weak_ptr_factory_.GetWeakPtr());
}

void ListenCall::FinishIfNeeded() {
  if (!finish_requested_ && client_) {
    Finish();
    return;
  }

  CheckDiscardable();
}

void ListenCall::Finish() {
  FXL_DCHECK(!finish_requested_);
  finish_requested_ = true;

  stream_controller_.Finish([this](bool ok, grpc::Status status) {
    if (!client_) {
      CheckDiscardable();
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
  CheckDiscardable();
}

void ListenCall::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

bool ListenCall::IsDiscardable() const {
  return client_ == nullptr && stream_controller_.IsDiscardable() &&
         stream_reader_.IsDiscardable() && stream_writer_.IsDiscardable();
}

bool ListenCall::CheckDiscardable() {
  if (!IsDiscardable()) {
    return false;
  }

  if (on_discardable_) {
    on_discardable_();
  }
  return true;
}

}  // namespace cloud_provider_firestore
