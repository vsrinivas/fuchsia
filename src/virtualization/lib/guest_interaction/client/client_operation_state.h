// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_CLIENT_OPERATION_STATE_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_CLIENT_OPERATION_STATE_H_

#include <grpc++/grpc++.h>
#include <grpc/support/log.h>
#include <zircon/system/ulib/fit/include/lib/fit/function.h>

#include "src/virtualization/lib/guest_interaction/common.h"
#include "src/virtualization/lib/guest_interaction/platform_interface/platform_interface.h"
#include "src/virtualization/lib/guest_interaction/proto/guest_interaction.grpc.pb.h"

using TransferCallback = fit::function<void(OperationStatus status)>;

// Manages the transfer of a file from the guest VM to the Fuchsia host.
//
// GetCallData will continually write new data from the guest into the
// specified destination location.  When the gRPC channel is terminated, the
// termination status is queried and final status is reported through the
// caller-supplied callback.
template <class T>
class GetCallData final : public CallData {
 public:
  GetCallData<T>(int32_t fd, TransferCallback callback)
      : status_(CREATE),
        callback_(std::move(callback)),
        fd_(fd),
        exit_status_(OperationStatus::OK) {}

  void Proceed(bool ok);

  grpc::ClientContext ctx_;
  std::unique_ptr<grpc::ClientAsyncReaderInterface<GetResponse>> reader_;
  GetResponse response_;
  T platform_interface_;

 private:
  enum CallStatus { CREATE, TRANSFER, FINISH };
  CallStatus status_;

  TransferCallback callback_;
  int32_t fd_;

  grpc::Status termination_status_;
  OperationStatus exit_status_;
};

// Proceed is called when the completion queue signals that the most recent
// Read operation has completed and there is new data that can be processed.
//
// From the gRPC documentation for a client Read operation:
// `ok` indicates whether there is a valid message that got read. If not, you
// know that there are certainly no more messages that can ever be read from
// this stream. For the client-side operations, this only happens because the
// call is dead.
//
// The client attempts to write incoming data into the open file until gRPC
// indicates that the call is dead at which point it queries for final status
// and reports the transfer status back to the caller through the callback.
template <class T>
void GetCallData<T>::Proceed(bool ok) {
  switch (status_) {
    case CREATE:
      if (!ok) {
        reader_->Finish(&termination_status_, this);
        exit_status_ = OperationStatus::GRPC_FAILURE;
        status_ = FINISH;
        return;
      }
      reader_->Read(&response_, this);
      status_ = TRANSFER;
      return;
    case TRANSFER:
      if (!ok) {
        reader_->Finish(&termination_status_, this);
        status_ = FINISH;
        return;
      }

      if (response_.status() != OperationStatus::OK) {
        exit_status_ = response_.status();
        reader_->Finish(&termination_status_, this);
        status_ = FINISH;
        return;
      }

      if (platform_interface_.WriteFile(fd_, response_.data().c_str(), response_.data().size()) <
          0) {
        exit_status_ = OperationStatus::CLIENT_FILE_WRITE_FAILURE;
        reader_->Finish(&termination_status_, this);
        status_ = FINISH;
        return;
      }

      reader_->Read(&response_, this);
      status_ = TRANSFER;
      return;
    case FINISH:
      platform_interface_.CloseFile(fd_);
      if (ok || exit_status_ != OperationStatus::OK) {
        callback_(exit_status_);
      } else {
        callback_(OperationStatus::GRPC_FAILURE);
      }
      delete this;
      return;
  }
}

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_CLIENT_OPERATION_STATE_H_
