// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_REQUEST_H
#define DEVICE_REQUEST_H

#include <memory>

#include "magma_util/macros.h"
#include "magma_util/status.h"
#include "platform_event.h"

class MsdArmDevice;

class DeviceRequest {
 public:
  virtual ~DeviceRequest() {}

  class Reply {
   public:
    Reply() : status_(MAGMA_STATUS_OK), event_(magma::PlatformEvent::Create()) { DASSERT(event_); }

    void Signal(magma::Status status) {
      status_ = status;
      event_->Signal();
    }

    magma::Status Wait() {
      event_->Wait();
      return status_;
    }

   private:
    magma::Status status_;
    std::unique_ptr<magma::PlatformEvent> event_;
  };

  std::shared_ptr<Reply> GetReply() {
    if (!reply_)
      reply_ = std::shared_ptr<Reply>(new Reply());
    return reply_;
  }

  void ProcessAndReply(MsdArmDevice* device) {
    magma::Status status = Process(device);

    if (reply_)
      reply_->Signal(status);
  }

 protected:
  virtual magma::Status Process(MsdArmDevice* device) { return MAGMA_STATUS_OK; }

 private:
  std::shared_ptr<Reply> reply_;
};

#endif
