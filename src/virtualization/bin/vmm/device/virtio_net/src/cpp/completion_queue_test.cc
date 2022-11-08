// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/virtio_net/src/cpp/completion_queue.h"

#include <src/lib/testing/loop_fixture/test_loop_fixture.h>

namespace {

// To simplify the tests, buffer IDs and buffer lengths will sequentially increase across and
// between each batch.
constexpr uint32_t kFirstBufferId = 1;
constexpr uint32_t kFirstBufferLength = 128;

constexpr uint8_t kPort = 5;

class FakeNetDevice : public ddk::NetworkDeviceIfcProtocol<FakeNetDevice> {
 public:
  ddk::NetworkDeviceIfcProtocolClient GetClient() {
    const network_device_ifc_protocol_t protocol = {
        .ops = &network_device_ifc_protocol_ops_,
        .ctx = this,
    };

    return ddk::NetworkDeviceIfcProtocolClient(&protocol);
  }

  void NetworkDeviceIfcCompleteRx(const rx_buffer_t* rx_list, size_t rx_count) {
    std::vector<rx_buffer_part_t> batch;
    for (size_t i = 0; i < rx_count; i++) {
      // Static values which should be the same for every completed buffer.
      ASSERT_EQ(rx_list->data_list->offset, 0u);
      ASSERT_EQ(rx_list->meta.port, kPort);
      ASSERT_EQ(rx_list->data_count, 1u);
      ASSERT_EQ(rx_list->meta.frame_type,
                static_cast<uint8_t>(::fuchsia::hardware::network::FrameType::ETHERNET));

      batch.push_back(*(rx_list->data_list));
      rx_list++;
    }
    rx_batches_.push_back(batch);
  }

  void NetworkDeviceIfcCompleteTx(const tx_result_t* tx_list, size_t tx_count) {
    std::vector<tx_result_t> batch;
    for (size_t i = 0; i < tx_count; i++) {
      batch.push_back(*tx_list);
      tx_list++;
    }
    tx_batches_.push_back(batch);
  }

  void NetworkDeviceIfcPortStatusChanged(uint8_t port_id, const port_status_t* new_status) {
    FAIL() << "Not supported by the FakeNetDevice";
  }
  void NetworkDeviceIfcAddPort(uint8_t port_id, const network_port_protocol_t* port) {
    FAIL() << "Not supported by the FakeNetDevice";
  }
  void NetworkDeviceIfcRemovePort(uint8_t port_id) {
    FAIL() << "Not supported by the FakeNetDevice";
  }
  void NetworkDeviceIfcSnoop(const rx_buffer_t* rx_list, size_t rx_count) {
    FAIL() << "Not supported by the FakeNetDevice";
  }

  std::vector<std::vector<tx_result_t>> tx_batches_;
  std::vector<std::vector<rx_buffer_part_t>> rx_batches_;
};

class CompletionQueueTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    client_ = device_.GetClient();
  }

  void ValidateTxBatches(std::vector<uint32_t> expected) {
    ASSERT_EQ(device_.tx_batches_.size(), expected.size());
    uint32_t buffer_id = kFirstBufferId;
    for (uint32_t i = 0; i < expected.size(); i++) {
      ASSERT_EQ(device_.tx_batches_[i].size(), expected[i]);
      for (const auto& result : device_.tx_batches_[i]) {
        ASSERT_EQ(result.id, buffer_id++);
        ASSERT_EQ(result.status, ZX_OK);
      }
    }
  }

  void ValidateRxBatches(std::vector<uint32_t> expected) {
    ASSERT_EQ(device_.rx_batches_.size(), expected.size());
    uint32_t buffer_id = kFirstBufferId;
    uint32_t buffer_length = kFirstBufferLength;
    for (uint32_t i = 0; i < expected.size(); i++) {
      ASSERT_EQ(device_.rx_batches_[i].size(), expected[i]);
      for (const auto& result : device_.rx_batches_[i]) {
        ASSERT_EQ(result.id, buffer_id++);
        ASSERT_EQ(result.length, buffer_length++);
      }
    }
  }

  FakeNetDevice device_;
  ddk::NetworkDeviceIfcProtocolClient client_;
};

TEST_F(CompletionQueueTest, TxCompleteFewerThanMaxDepth) {
  HostToGuestCompletionQueue queue(dispatcher(), &client_);
  queue.Complete(kFirstBufferId, ZX_OK);

  // Dispatch loop hasn't run the task yet.
  ASSERT_TRUE(device_.tx_batches_.empty());

  RunLoopUntilIdle();

  // Single element in a single batch.
  ASSERT_NO_FATAL_FAILURE(ValidateTxBatches({1}));
}

TEST_F(CompletionQueueTest, TxCompleteMoreThanMaxDepth) {
  HostToGuestCompletionQueue queue(dispatcher(), &client_);
  uint32_t buffer_id = kFirstBufferId;
  for (uint32_t i = 0; i < HostToGuestCompletionQueue::kMaxDepth + 1; i++) {
    queue.Complete(buffer_id++, ZX_OK);
  }

  // Dispatch loop hasn't run the task yet.
  ASSERT_TRUE(device_.tx_batches_.empty());

  RunLoopUntilIdle();

  // Two batches, one full and one with one element.
  ASSERT_NO_FATAL_FAILURE(ValidateTxBatches({HostToGuestCompletionQueue::kMaxDepth, 1}));
}

TEST_F(CompletionQueueTest, TxCompleteMoreThanQueueSize) {
  HostToGuestCompletionQueue queue(dispatcher(), &client_);
  uint32_t buffer_id = kFirstBufferId;
  for (uint32_t i = 0; i < HostToGuestCompletionQueue::kQueueDepth + 3; i++) {
    queue.Complete(buffer_id++, ZX_OK);
  }

  // Dispatch loop hasn't run the task yet.
  ASSERT_TRUE(device_.tx_batches_.empty());

  RunLoopUntilIdle();

  // Stick some more completions into the now empty queue.
  for (uint32_t i = 0; i < HostToGuestCompletionQueue::kMaxDepth / 2; i++) {
    queue.Complete(buffer_id++, ZX_OK);
  }

  RunLoopUntilIdle();

  // Six batches. The first two are batches from the completion queue, and the next 3 are single
  // element overflows, and the last is another iteration from the completion queue.
  ASSERT_NO_FATAL_FAILURE(ValidateTxBatches({HostToGuestCompletionQueue::kMaxDepth,
                                             HostToGuestCompletionQueue::kMaxDepth, 1, 1, 1,
                                             HostToGuestCompletionQueue::kMaxDepth / 2}));
}

TEST_F(CompletionQueueTest, RxCompleteFewerThanMaxDepth) {
  GuestToHostCompletionQueue queue(kPort, dispatcher(), &client_);
  queue.Complete(kFirstBufferId, kFirstBufferLength);

  // Dispatch loop hasn't run the task yet.
  ASSERT_TRUE(device_.rx_batches_.empty());

  RunLoopUntilIdle();

  // Single element in a single batch.
  ASSERT_NO_FATAL_FAILURE(ValidateRxBatches({1}));
}

TEST_F(CompletionQueueTest, RxCompleteMoreThanMaxDepth) {
  GuestToHostCompletionQueue queue(kPort, dispatcher(), &client_);
  uint32_t buffer_id = kFirstBufferId;
  uint32_t buffer_length = kFirstBufferLength;
  for (uint32_t i = 0; i < GuestToHostCompletionQueue::kMaxDepth + 1; i++) {
    queue.Complete(buffer_id++, buffer_length++);
  }

  RunLoopUntilIdle();

  for (uint32_t i = 0; i < GuestToHostCompletionQueue::kMaxDepth / 2; i++) {
    queue.Complete(buffer_id++, buffer_length++);
  }

  RunLoopUntilIdle();

  // Three batches, one full, one with one element, and then the last half full.
  ASSERT_NO_FATAL_FAILURE(ValidateRxBatches(
      {GuestToHostCompletionQueue::kMaxDepth, 1, GuestToHostCompletionQueue::kMaxDepth / 2}));
}

TEST_F(CompletionQueueTest, RxCompleteMoreThanQueueSize) {
  GuestToHostCompletionQueue queue(kPort, dispatcher(), &client_);
  uint32_t buffer_id = kFirstBufferId;
  uint32_t buffer_length = kFirstBufferLength;
  for (uint32_t i = 0; i < GuestToHostCompletionQueue::kQueueDepth + 2; i++) {
    queue.Complete(buffer_id++, buffer_length++);
  }

  RunLoopUntilIdle();

  // Four batches, two from the completion queue and two single element overflows.
  ASSERT_NO_FATAL_FAILURE(ValidateRxBatches(
      {GuestToHostCompletionQueue::kMaxDepth, GuestToHostCompletionQueue::kMaxDepth, 1, 1}));
}

}  // namespace
