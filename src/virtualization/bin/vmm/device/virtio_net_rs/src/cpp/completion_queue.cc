// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/virtio_net_rs/src/cpp/completion_queue.h"

#include <lib/async/cpp/task.h>

void HostToGuestCompletionQueue::Complete(uint32_t buffer_id, zx_status_t status) {
  std::lock_guard guard(mutex_);
  if (count_ == result_.size()) {
    ScheduleIndividual(buffer_id, status);
    return;
  }

  // Schedule a batched completion on the dispatch thread.
  if (count_ == 0) {
    async::PostTask(dispatcher_, fit::bind_member<&HostToGuestCompletionQueue::SendBatched>(this));
  }

  result_[count_++] = {
      .id = buffer_id,
      .status = status,
  };
}

void HostToGuestCompletionQueue::SendBatched() {
  std::lock_guard guard(mutex_);

  uint32_t idx = 0;
  while (idx != count_) {
    uint32_t batch_count = std::min(count_ - idx, kMaxDepth);
    device_->CompleteTx(&result_[idx], batch_count);
    idx += batch_count;
  }

  count_ = 0;
}

void HostToGuestCompletionQueue::ScheduleIndividual(uint32_t buffer_id, zx_status_t status) {
  async::PostTask(dispatcher_, [this, buffer_id, status]() {
    tx_result result = {
        .id = buffer_id,
        .status = status,
    };

    device_->CompleteTx(&result, /*tx_count=*/1);
  });
}

GuestToHostCompletionQueue::GuestToHostCompletionQueue(uint8_t port, async_dispatcher_t* dispatcher,
                                                       ddk::NetworkDeviceIfcProtocolClient* device)
    : port_(port), dispatcher_(dispatcher), device_(device) {
  // Initialize the static parts of the completion notifications. These will be reused.
  FX_CHECK(buffer_.size() == buffer_part_.size());
  for (uint32_t i = 0; i < buffer_part_.size(); i++) {
    buffer_[i] = {
        .meta =
            {
                .port = port_,
                .frame_type =
                    static_cast<uint8_t>(::fuchsia::hardware::network::FrameType::ETHERNET),
            },
        .data_list = &buffer_part_[i],
        .data_count = 1,
    };
  }
}

void GuestToHostCompletionQueue::Complete(uint32_t buffer_id, uint32_t length) {
  std::lock_guard guard(mutex_);
  if (count_ == buffer_part_.size()) {
    ScheduleIndividual(buffer_id, length);
    return;
  }

  // Schedule a batched completion on the dispatch thread.
  if (count_ == 0) {
    async::PostTask(dispatcher_, fit::bind_member<&GuestToHostCompletionQueue::SendBatched>(this));
  }

  buffer_part_[count_++] = {
      .id = buffer_id,
      .offset = 0,
      .length = length,
  };
}

void GuestToHostCompletionQueue::SendBatched() {
  std::lock_guard guard(mutex_);

  uint32_t idx = 0;
  while (idx != count_) {
    uint32_t batch_count = std::min(count_ - idx, kMaxDepth);
    device_->CompleteRx(&buffer_[idx], batch_count);
    idx += batch_count;
  }

  count_ = 0;
}

void GuestToHostCompletionQueue::ScheduleIndividual(uint32_t buffer_id, uint32_t length) {
  async::PostTask(dispatcher_, [this, buffer_id, length]() {
    rx_buffer_part part = {
        .id = buffer_id,
        .offset = 0,
        .length = length,
    };
    rx_buffer rx_info = {
        .meta =
            {
                .port = port_,
                .frame_type =
                    static_cast<uint8_t>(::fuchsia::hardware::network::FrameType::ETHERNET),
            },
        .data_list = &part,
        .data_count = 1,
    };

    device_->CompleteRx(&rx_info, /*rx_count=*/1);
  });
}
