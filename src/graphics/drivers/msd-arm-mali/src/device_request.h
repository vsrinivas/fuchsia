// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_REQUEST_H
#define DEVICE_REQUEST_H
#include <lib/fit/function.h>

#include <chrono>
#include <memory>
#include <optional>

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

  void OnEnqueued() { enqueue_time_ = std::chrono::steady_clock::now(); }

  std::chrono::steady_clock::time_point enqueue_time() const {
    DASSERT(enqueue_time_.has_value());
    return *enqueue_time_;
  }

 protected:
  virtual magma::Status Process(MsdArmDevice* device) { return MAGMA_STATUS_OK; }

 private:
  std::optional<std::chrono::steady_clock::time_point> enqueue_time_;
  std::shared_ptr<Reply> reply_;
};

using FitCallbackTask = fit::callback<magma::Status(MsdArmDevice*)>;

#endif
