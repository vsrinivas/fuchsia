// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_NET_RS_SRC_CPP_COMPLETION_QUEUE_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_NET_RS_SRC_CPP_COMPLETION_QUEUE_H_

// Don't reorder this header to avoid conflicting implementations of MAX_PORTS.
// clang-format off
#include <fuchsia/net/virtualization/cpp/fidl.h>
// clang-format on

#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/compiler.h>

#include <mutex>

class TxCompletionQueue {
 public:
  TxCompletionQueue(async_dispatcher_t* dispatcher, ddk::NetworkDeviceIfcProtocolClient* device)
      : dispatcher_(dispatcher), device_(device) {}

  // Write a completion to the queue, scheduling a task to send a completion to the netstack if
  // needed. If the queue is full, this won't be batched and instead will be scheduled
  // independently.
  //
  // This is safe to call from any thread.
  void Complete(uint32_t buffer_id, zx_status_t status);

  static constexpr uint32_t kMaxTxDepth = 128;
  static constexpr uint32_t kQueueDepth = kMaxTxDepth * 2;

 private:
  // Send all pending completions in `kMaxTxDepth` batches. This runs on the dispatch thread.
  void SendBatched();

  // Schedule a single `tx_result` to complete. Used if the queue is full (and implies that the
  // queue depth is too shallow).
  void ScheduleIndividual(uint32_t buffer_id, zx_status_t status);

  std::mutex mutex_;

  async_dispatcher_t* const dispatcher_ = nullptr;                     // Unowned.
  const ddk::NetworkDeviceIfcProtocolClient* const device_ = nullptr;  // Unowned.

  std::array<tx_result, kQueueDepth> result_ __TA_GUARDED(mutex_);
  uint32_t count_ __TA_GUARDED(mutex_) = 0;
};

class RxCompletionQueue {
 public:
  RxCompletionQueue(uint8_t port, async_dispatcher_t* dispatcher,
                    ddk::NetworkDeviceIfcProtocolClient* device);

  // Write a completion to the queue, scheduling a task to send a completion to the netstack if
  // needed. If the queue is full, this won't be batched and instead will be scheduled
  // independently.
  //
  // The `buffer_` queue is what's sent to the netstack, but its values are static. During queue
  // initialization its elements were configured to point back the `buffer_part_` queue which
  // contains the dynamic complete values.
  //
  // This is safe to call from any thread.
  void Complete(uint32_t buffer_id, uint32_t length);

  static constexpr uint32_t kMaxRxDepth = 128;
  static constexpr uint32_t kQueueDepth = kMaxRxDepth * 2;

 private:
  // Send all pending completions in `kMaxRxDepth` batches. This runs on the dispatch thread.
  void SendBatched();

  // Schedule a single `rx_buffer` to complete. Used if the queue is full (and implies that the
  // queue depth is too shallow).
  void ScheduleIndividual(uint32_t buffer_id, uint32_t length);

  std::mutex mutex_;
  const uint8_t port_;

  async_dispatcher_t* const dispatcher_ = nullptr;                     // Unowned.
  const ddk::NetworkDeviceIfcProtocolClient* const device_ = nullptr;  // Unowned.

  // Only one buffer part is supported (no scatter/gather), so there is a 1:1 mapping between
  // buffer_ and buffer_part_.
  std::array<rx_buffer, kQueueDepth> buffer_ __TA_GUARDED(mutex_);
  std::array<rx_buffer_part, kQueueDepth> buffer_part_ __TA_GUARDED(mutex_);
  uint32_t count_ __TA_GUARDED(mutex_) = 0;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_NET_RS_SRC_CPP_COMPLETION_QUEUE_H_
