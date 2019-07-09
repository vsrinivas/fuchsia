// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_SERVER_SERVER_OPERATION_STATE_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_SERVER_SERVER_OPERATION_STATE_H_

#include <grpc++/grpc++.h>
#include <grpc/support/log.h>
#include <src/lib/fxl/logging.h>

#include "src/virtualization/lib/guest_interaction/common.h"
#include "src/virtualization/lib/guest_interaction/platform_interface/platform_interface.h"
#include "src/virtualization/lib/guest_interaction/proto/guest_interaction.grpc.pb.h"

// Manages the transfer of a file from the guest VM to the host.
//
// When the client requests a file, the server sends a stream of messages
// containing the file's contents until either gRPC breaks or the server
// hits the end of the requested file.
template <class T>
class GetCallData final : public CallData {
 public:
  GetCallData(GuestInteractionService::AsyncService* service, grpc::ServerCompletionQueue* cq);
  void Proceed(bool ok);

  T platform_interface_;

 private:
  void TryRead();
  void Finish();

  GuestInteractionService::AsyncService* service_;
  grpc::ServerCompletionQueue* cq_;
  grpc::ServerContext ctx_;
  grpc::ServerAsyncWriter<GetResponse> writer_;
  GetRequest request_;
  int32_t fd_;

  enum CallStatus { CREATE, INITIATE_TRANSFER, TRANSFER, END_TRANSFER, FINISH };
  CallStatus status_;
};

template <class T>
GetCallData<T>::GetCallData(GuestInteractionService::AsyncService* service,
                            grpc::ServerCompletionQueue* cq)
    : service_(service), cq_(cq), writer_(&ctx_), fd_(0), status_(CREATE) {
  Proceed(true);
}

// Tracks the state of a file transfer from the guest VM to the host.
//
// When instantiated, GetCallData immediately calls Proceed which calls
// RequestGet so that the server can handle incoming Get requests.
//
// The client specifies the source file in its initial request and then the
// server streams the file contents back to the guest until either the gRPC
// channel breaks (ok == false) or it hits EOF on the source file.
//
// According to the gRPC docs for a server Write operation:
// ok means that the data/metadata/status/etc is going to go to the wire. If it
// is false, it not going to the wire because the call is already dead (i.e.,
// canceled, deadline expired, other side dropped the channel, etc).
template <class T>
void GetCallData<T>::Proceed(bool ok) {
  if (!ok) {
    Finish();
    return;
  }

  switch (status_) {
    case CREATE:
      status_ = INITIATE_TRANSFER;
      service_->RequestGet(&ctx_, &request_, &writer_, cq_, cq_, this);
      return;
    case INITIATE_TRANSFER:
      // Allow new GetRequest streams to be handled.
      new GetCallData<T>(service_, cq_);

      if (!platform_interface_.FileExists(request_.source())) {
        GetResponse get_response;
        get_response.clear_data();
        get_response.set_status(OperationStatus::SERVER_MISSING_FILE_FAILURE);
        writer_.Write(get_response, this);
        status_ = END_TRANSFER;
        return;
      }

      fd_ = platform_interface_.OpenFile(request_.source(), READ);
      if (fd_ < 0) {
        GetResponse get_response;
        get_response.clear_data();
        get_response.set_status(OperationStatus::SERVER_FILE_READ_FAILURE);
        writer_.Write(get_response, this);
        status_ = END_TRANSFER;
        return;
      }

      status_ = TRANSFER;
      TryRead();
      return;
    case TRANSFER:
      TryRead();
      return;
    case END_TRANSFER:
      writer_.Finish(grpc::Status::OK, this);
      status_ = FINISH;
      return;
    case FINISH:
      Finish();
      return;
  }
}

template <class T>
void GetCallData<T>::TryRead() {
  GetResponse get_response;
  char data_chunk[CHUNK_SIZE];
  int32_t data_read = platform_interface_.ReadFile(fd_, data_chunk, CHUNK_SIZE);

  if (data_read < 0) {
    if (-data_read == EAGAIN || -data_read == EWOULDBLOCK) {
      // Reading would have caused blocking, so send back an empty message.
      get_response.set_status(OperationStatus::OK);
      get_response.clear_data();
      writer_.Write(get_response, this);
    } else {
      // Reading failed in an unexpected way.  Notify client and finish.
      get_response.set_status(OperationStatus::SERVER_FILE_READ_FAILURE);
      get_response.clear_data();
      writer_.Write(get_response, this);
      status_ = END_TRANSFER;
    }
  } else if (data_read == 0) {
    // Read size of 0 indicates EOF.
    get_response.set_status(OperationStatus::OK);
    get_response.clear_data();
    writer_.Write(get_response, this);
    status_ = END_TRANSFER;
  } else {
    get_response.set_status(OperationStatus::OK);
    get_response.set_data(data_chunk, data_read);
    writer_.Write(get_response, this);
  }
}

template <class T>
void GetCallData<T>::Finish() {
  if (fd_ > 0) {
    platform_interface_.CloseFile(fd_);
  }
  delete this;
}

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_SERVER_SERVER_OPERATION_STATE_H_
