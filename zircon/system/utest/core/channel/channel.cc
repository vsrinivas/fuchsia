// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>
#include <vector>

#include <unistd.h>

#include <fbl/auto_call.h>
#include <lib/fit/function.h>
// Needed to test API coverage of null params in GCC.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull"
#include <lib/zx/channel.h>
#pragma GCC diagnostic pop
#include <lib/zx/event.h>
#include <lib/zx/fifo.h>
#include <lib/zx/object.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/rights.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

#include "utils.h"

namespace channel {
namespace {

// Data used for writing into a channel.
constexpr uint32_t kChannelData = 0xdeadbeef;

TEST(ChannelTest, CreateIsOkAndEndpointsAreRelated) {
  zx::channel local;
  zx::channel remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  zx_info_handle_basic_t info[2];
  ASSERT_OK(local.get_info(ZX_INFO_HANDLE_BASIC, &info[0], sizeof(zx_info_handle_basic_t), nullptr,
                           nullptr));
  ASSERT_OK(remote.get_info(ZX_INFO_HANDLE_BASIC, &info[1], sizeof(zx_info_handle_basic_t), nullptr,
                            nullptr));
  ASSERT_NE(info[0].koid, 0);
  ASSERT_NE(info[1].koid, 0);

  EXPECT_EQ(info[0].related_koid, info[1].koid);
  EXPECT_EQ(info[1].related_koid, info[0].koid);
}

TEST(ChannelTest, IsWriteableByDefault) {
  zx::channel local;
  zx::channel remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  zx_signals_t local_pending = 0;
  zx_signals_t remote_pending = 0;
  ASSERT_OK(local.wait_one(ZX_CHANNEL_WRITABLE, zx::time::infinite_past(), &local_pending));
  ASSERT_OK(remote.wait_one(ZX_CHANNEL_WRITABLE, zx::time::infinite_past(), &remote_pending));
  EXPECT_EQ(ZX_CHANNEL_WRITABLE, local_pending);
  EXPECT_EQ(ZX_CHANNEL_WRITABLE, remote_pending);
}

TEST(ChannelTest, WriteToEndpointCausesOtherToBecomeReadable) {
  zx::channel local;
  zx::channel remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  ASSERT_OK(local.write(0u, &kChannelData, sizeof(uint32_t), nullptr, 0u));

  zx_signals_t local_pending = 0;
  zx_signals_t remote_pending = 0;
  ASSERT_OK(local.wait_one(ZX_CHANNEL_WRITABLE, zx::time::infinite_past(), &local_pending));
  ASSERT_OK(remote.wait_one(ZX_CHANNEL_WRITABLE | ZX_CHANNEL_READABLE, zx::time::infinite_past(),
                            &remote_pending));

  EXPECT_EQ(ZX_CHANNEL_WRITABLE, local_pending);
  EXPECT_EQ(ZX_CHANNEL_WRITABLE | ZX_CHANNEL_READABLE, remote_pending);
}

TEST(ChannelTest, WriteConsumesAllHandles) {
  zx::channel local;
  zx::channel remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  constexpr uint32_t kHandleCount = ZX_CHANNEL_MAX_MSG_HANDLES + 1;
  std::vector<zx::event> safe_handles(kHandleCount);
  for (uint32_t j = 0; j < kHandleCount; ++j) {
    ASSERT_OK(zx::event::create(0, &safe_handles[j]));
  }

  zx_handle_t handles[kHandleCount];
  for (uint32_t j = 0; j < kHandleCount; ++j) {
    handles[j] = safe_handles[j].release();
  }

  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, local.write(0, nullptr, 0, handles, kHandleCount));

  for (uint32_t j = 0; j < kHandleCount; ++j) {
    EXPECT_EQ(ZX_ERR_BAD_HANDLE, zx_handle_close(handles[j]));
  }
}

enum class WorkerCompleteStatus : int {
  kSuccess = 0,
  kWaitError = 1,
  kReadFrom1Error = 2,
  kReadFrom2Error = 3,
  kDataMismatchFrom1Error = 4,
  kDataMismatchFrom2Error = 5,
};

void WaitOnChannels(zx::unowned_channel remote_1, zx::unowned_channel remote_2,
                    zx::unowned_event event, std::atomic<uint32_t>* total_packets,
                    std::atomic<uint32_t>* received_bytes_1,
                    std::atomic<uint32_t>* received_bytes_2,
                    std::atomic<WorkerCompleteStatus>* result) {
  zx_wait_item_t items[2];
  items[0].handle = remote_1->get();
  items[0].waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;

  items[1].handle = remote_2->get();
  items[1].waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;

  bool closed_1 = false;
  bool closed_2 = false;
  while (!closed_1 || !closed_2) {
    uint32_t data = 0u;
    uint32_t actual_bytes = 0u;
    if (zx::channel::wait_many(items, 2, zx::deadline_after(zx::duration::infinite())) != ZX_OK) {
      *result = WorkerCompleteStatus::kWaitError;
      return;
    }
    if (items[0].pending & ZX_CHANNEL_READABLE) {
      event->signal(0, ZX_USER_SIGNAL_0);
      if (remote_1->read(0u, &data, nullptr, sizeof(uint32_t), 0, &actual_bytes, nullptr) !=
          ZX_OK) {
        *result = WorkerCompleteStatus::kReadFrom1Error;
        return;
      }
      if (data != kChannelData) {
        *result = WorkerCompleteStatus::kDataMismatchFrom1Error;
        return;
      }
      *received_bytes_1 += actual_bytes;
      (*total_packets)++;
    } else if (items[1].pending & ZX_CHANNEL_READABLE) {
      event->signal(0, ZX_USER_SIGNAL_1);
      if (remote_2->read(0u, &data, nullptr, sizeof(uint32_t), 0, &actual_bytes, nullptr) !=
          ZX_OK) {
        *result = WorkerCompleteStatus::kReadFrom2Error;
        return;
      }
      if (data != kChannelData) {
        *result = WorkerCompleteStatus::kDataMismatchFrom2Error;
        return;
      }
      *received_bytes_2 += actual_bytes;
      (*total_packets)++;
    } else {
      if (items[0].pending & ZX_CHANNEL_PEER_CLOSED) {
        closed_1 = true;
      }
      if (items[1].pending & ZX_CHANNEL_PEER_CLOSED) {
        closed_2 = true;
      }
    }
  }

  *result = WorkerCompleteStatus::kSuccess;
  return;
}

TEST(ChannelTest, WaitManyIsSignaledOnAnyElementWrite) {
  zx::channel local_1, local_2;
  zx::channel remote_1, remote_2;

  ASSERT_OK(zx::channel::create(0, &local_1, &remote_1));
  ASSERT_OK(zx::channel::create(0, &local_2, &remote_2));
  std::atomic<uint32_t> received_packets = 0;
  std::atomic<uint32_t> received_bytes_1 = 0;
  std::atomic<uint32_t> received_bytes_2 = 0;
  std::atomic<WorkerCompleteStatus> result = WorkerCompleteStatus::kSuccess;
  zx::event event;

  ASSERT_OK(zx::event::create(0, &event));

  {
    AutoJoinThread worker(&WaitOnChannels, zx::unowned_channel(remote_1),
                          zx::unowned_channel(remote_2), zx::unowned_event(event),
                          &received_packets, &received_bytes_1, &received_bytes_2, &result);
    // On exit close the local handles to unblock the service thread.
    auto cleanup = fbl::MakeAutoCall([&local_1, &local_2]() {
      local_1.reset();
      local_2.reset();
    });
    ASSERT_OK(local_1.write(0, &kChannelData, sizeof(uint32_t), nullptr, 0));
    // We should expect only to be signalled for reading from remote_1.
    ASSERT_OK(event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr));
  }

  zx_signals_t event_signal;
  ASSERT_OK(event.wait_one(ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1, zx::time::infinite_past(),
                           &event_signal));
  zx_signals_t signal_1;
  ASSERT_EQ(remote_1.wait_one(0, zx::time::infinite_past(), &signal_1), ZX_ERR_TIMED_OUT);
  zx_signals_t signal_2;
  ASSERT_EQ(remote_2.wait_one(0, zx::time::infinite_past(), &signal_2), ZX_ERR_TIMED_OUT);

  ASSERT_EQ(result, WorkerCompleteStatus::kSuccess);
  ASSERT_EQ(ZX_USER_SIGNAL_0, event_signal);
  EXPECT_EQ(ZX_CHANNEL_PEER_CLOSED, signal_1);
  EXPECT_EQ(ZX_CHANNEL_PEER_CLOSED, signal_2);
  EXPECT_EQ(received_bytes_1.load(), 1 * sizeof(uint32_t));
  EXPECT_EQ(received_bytes_2.load(), 0);
  EXPECT_EQ(received_packets, 1u);
}

TEST(ChannelTest, WaitManyIsSignaledForBothWrites) {
  zx::channel local_1, local_2;
  zx::channel remote_1, remote_2;

  ASSERT_OK(zx::channel::create(0, &local_1, &remote_1));
  ASSERT_OK(zx::channel::create(0, &local_2, &remote_2));
  std::atomic<uint32_t> received_packets = 0;
  std::atomic<uint32_t> received_bytes_1 = 0;
  std::atomic<uint32_t> received_bytes_2 = 0;
  std::atomic<WorkerCompleteStatus> result = WorkerCompleteStatus::kSuccess;
  zx::event event;

  ASSERT_OK(zx::event::create(0, &event));

  {
    AutoJoinThread worker(&WaitOnChannels, zx::unowned_channel(remote_1),
                          zx::unowned_channel(remote_2), zx::unowned_event(event),
                          &received_packets, &received_bytes_1, &received_bytes_2, &result);
    // On exit close the local handles to unblock the service thread.
    auto cleanup = fbl::MakeAutoCall([&local_1, &local_2]() {
      local_1.reset();
      local_2.reset();
    });
    ASSERT_OK(local_2.write(0, &kChannelData, sizeof(uint32_t), nullptr, 0));
    ASSERT_OK(local_1.write(0, &kChannelData, sizeof(uint32_t), nullptr, 0));
    // We should expect only to be signalled for reading from remote_1.
    ASSERT_OK(event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr));
    ASSERT_OK(event.wait_one(ZX_USER_SIGNAL_1, zx::time::infinite(), nullptr));
  }

  zx_signals_t event_signal;
  ASSERT_OK(event.wait_one(ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1, zx::time::infinite_past(),
                           &event_signal));
  zx_signals_t signal_1;
  ASSERT_EQ(remote_1.wait_one(0, zx::time::infinite_past(), &signal_1), ZX_ERR_TIMED_OUT);
  zx_signals_t signal_2;
  ASSERT_EQ(remote_2.wait_one(0, zx::time::infinite_past(), &signal_2), ZX_ERR_TIMED_OUT);

  ASSERT_EQ(result, WorkerCompleteStatus::kSuccess);
  ASSERT_EQ(ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_1, event_signal);
  EXPECT_EQ(ZX_CHANNEL_PEER_CLOSED, signal_1);
  EXPECT_EQ(ZX_CHANNEL_PEER_CLOSED, signal_2);
  EXPECT_EQ(received_bytes_1.load(), 1 * sizeof(uint32_t));
  EXPECT_EQ(received_bytes_2.load(), 1 * sizeof(uint32_t));
  EXPECT_EQ(received_packets, 2u);
}

TEST(ChannelTest, ReadWhenEmptyReturnsShouldWait) {
  zx::channel local;
  zx::channel remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  ASSERT_EQ(remote.read(0, nullptr, nullptr, 0, 0, nullptr, nullptr), ZX_ERR_SHOULD_WAIT);
}

TEST(ChannelTest, ReadWhenEmptyAndClosedReturnsPeerClosed) {
  zx::channel local;
  zx::channel remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  local.reset();
  ASSERT_EQ(remote.read(0, nullptr, nullptr, 0, 0, nullptr, nullptr), ZX_ERR_PEER_CLOSED);
}

TEST(ChannelTest, ReadRemainingMessagesWhenPeerIsClosed) {
  constexpr uint32_t kMessageCount = 4;
  zx::channel local;
  zx::channel remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  for (uint32_t i = 0; i < kMessageCount; ++i) {
    ASSERT_OK(local.write(0, &kChannelData, sizeof(uint32_t), nullptr, 0));
  }

  local.reset();

  zx_signals_t signal;
  ASSERT_EQ(remote.wait_one(0, zx::time::infinite_past(), &signal), ZX_ERR_TIMED_OUT);
  ASSERT_EQ(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, signal);

  for (uint32_t i = 0; i < kMessageCount; ++i) {
    uint32_t data;
    uint32_t read_bytes;
    ASSERT_OK(remote.read(0, &data, nullptr, sizeof(uint32_t), 0, &read_bytes, nullptr));
    ASSERT_EQ(sizeof(uint32_t), read_bytes);
    ASSERT_EQ(kChannelData, data);
  }
  // The channel should not be readable, since there are no remaining messages on it.
  ASSERT_EQ(remote.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite_past(), nullptr),
            ZX_ERR_TIMED_OUT);
}

TEST(ChannelTest, CloseSignalsPeerClosed) {
  zx::channel local;
  zx::channel remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  local.reset();

  zx_signals_t signal;
  ASSERT_OK(remote.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &signal));
  EXPECT_TRUE(signal & ZX_CHANNEL_PEER_CLOSED);
}

TEST(ChannelTest, CloseClearsSignalsWriteable) {
  zx::channel local;
  zx::channel remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  zx_signals_t signal;
  ASSERT_EQ(remote.wait_one(0, zx::time::infinite_past(), &signal), ZX_ERR_TIMED_OUT);
  ASSERT_TRUE(signal & ZX_CHANNEL_WRITABLE);

  local.reset();

  ASSERT_OK(remote.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &signal));
  EXPECT_FALSE(signal & ZX_CHANNEL_WRITABLE);
}

TEST(ChannelTest, CloseSignalsPeerReturnsPeerClosed) {
  zx::channel local;
  zx::channel remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  local.reset();
  ASSERT_EQ(remote.signal_peer(0, ZX_USER_SIGNAL_0), ZX_ERR_PEER_CLOSED);
}

TEST(ChannelTest, OnFlightHandlesSignalledWhenPeerIsClosed) {
  zx::channel local;
  zx::channel remote;
  zx::channel on_flight_local[2];
  zx::channel on_flight_remote[2];
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  ASSERT_OK(zx::channel::create(0, &on_flight_local[0], &on_flight_remote[0]));
  ASSERT_OK(zx::channel::create(0, &on_flight_local[1], &on_flight_remote[1]));

  // Write each handle to the respective channel peer.
  zx_handle_t transferred = on_flight_remote[0].release();
  ASSERT_OK(local.write(0, nullptr, 0, &transferred, 1));

  transferred = on_flight_remote[1].release();
  ASSERT_OK(remote.write(0, nullptr, 0, &transferred, 1));

  // When the peer is closed, all unread handles should be closed.
  local.reset();

  // Now the local end of each transferred channel should be signalled.
  ASSERT_OK(on_flight_local[1].wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr));
  // Because |remote| is still not closed, then we can still read the remote end of the channel,
  // this should still be writeable.
  zx_signals_t signals;
  ASSERT_EQ(on_flight_local[0].wait_one(0, zx::time::infinite_past(), &signals), ZX_ERR_TIMED_OUT);
  ASSERT_NE(signals & ZX_CHANNEL_WRITABLE, 0);

  remote.reset();
  ASSERT_OK(on_flight_local[0].wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr));
  ASSERT_EQ(on_flight_local[1].wait_one(ZX_ERR_PEER_CLOSED, zx::time::infinite_past(), nullptr),
            ZX_ERR_TIMED_OUT);
}

TEST(ChannelTest, WriteNonTransferableHandleReturnsAccessDeniedAndClosesHandle) {
  zx::channel local;
  zx::channel remote;
  zx::event event;

  ASSERT_OK(zx::channel::create(0, &local, &remote));
  ASSERT_OK(zx::event::create(0, &event));

  zx_info_handle_basic_t event_info;
  ASSERT_OK(event.get_info(ZX_INFO_HANDLE_BASIC, &event_info, sizeof(zx_info_handle_basic_t),
                           nullptr, nullptr));

  // Remove the transfer right.
  zx_rights_t rights = event_info.rights & ~ZX_RIGHT_TRANSFER;
  zx::event non_transferable_event;
  ASSERT_OK(event.duplicate(rights, &non_transferable_event));

  zx_handle_t transferred = non_transferable_event.release();
  ASSERT_EQ(local.write(0, nullptr, 0, &transferred, 1), ZX_ERR_ACCESS_DENIED);
  ASSERT_EQ(zx_handle_close(transferred), ZX_ERR_BAD_HANDLE);
}

TEST(ChannelTest, WriteRepeatedHandlesReturnsBadHandlesAndClosesHandle) {
  zx::channel local;
  zx::channel remote;
  zx::event event;

  ASSERT_OK(zx::channel::create(0, &local, &remote));
  ASSERT_OK(zx::event::create(0, &event));

  zx_handle_t event_handle = event.release();
  zx_handle_t handles[2] = {event_handle, event_handle};

  ASSERT_EQ(local.write(0, nullptr, 0, handles, 2), ZX_ERR_BAD_HANDLE);
  ASSERT_EQ(zx_handle_close(event_handle), ZX_ERR_BAD_HANDLE);
}

TEST(ChannelTest, ConcurrentReadsConsumeUniqueElements) {
  zx::channel local;
  zx::channel remote;
  // Used to force both threads to stall until both are ready to run.
  zx::event event;

  constexpr uint32_t kNumMessages = 5000;
  enum class ReadMessageStatus {
    kUnset,
    kReadFailed,
    kOk,
  };

  struct Message {
    uint64_t data = 0;
    uint32_t data_size = 0;
    ReadMessageStatus status = ReadMessageStatus::kUnset;
  };

  std::vector<Message> read_messages;
  read_messages.resize(kNumMessages);

  auto reader_worker = [&read_messages, &event, &remote](uint32_t offset) {
    zx_status_t wait_status = event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr);
    if (wait_status != ZX_OK) {
      return;
    }
    for (uint32_t i = 0; i < kNumMessages / 2; ++i) {
      uint64_t data = 0;
      uint32_t read_bytes = 0;
      zx_status_t read_status =
          remote.read(0, &data, nullptr, sizeof(uint64_t), 0, &read_bytes, nullptr);
      uint32_t index = offset + i;
      auto& message = read_messages[index];
      if (read_status != ZX_OK) {
        message.status = ReadMessageStatus::kReadFailed;
        continue;
      }
      message.status = ReadMessageStatus::kOk;
      message.data = data;
      message.data_size = read_bytes;
    }
    return;
  };

  ASSERT_OK(zx::channel::create(0, &local, &remote));
  ASSERT_OK(zx::event::create(0, &event));
  constexpr uint32_t kReader1Offset = 0;
  constexpr uint32_t kReader2Offset = kNumMessages / 2;
  {
    AutoJoinThread worker_1(reader_worker, kReader1Offset);
    AutoJoinThread worker_2(reader_worker, kReader2Offset);
    auto cleanup = fbl::MakeAutoCall([&local, &event]() {
      // Unlock read.
      local.reset();
      // Notify cancelled.
      event.reset();
    });
    for (uint64_t i = 1; i <= kNumMessages; ++i) {
      ASSERT_OK(local.write(0, &i, sizeof(uint64_t), nullptr, 0));
    }

    ASSERT_OK(event.signal(0, ZX_USER_SIGNAL_0));
    // Join before cleanup.
    worker_1.Join();
    worker_2.Join();
  }

  std::set<uint64_t> read_data;
  // Check that data os within (0, kNumMessages] range and that is monotonically increasing per
  // each reader.
  auto ValidateMessages = [&read_data, &read_messages, kNumMessages](uint32_t offset) {
    uint64_t prev = 0;
    for (uint32_t i = offset; i < kNumMessages / 2 + offset; ++i) {
      const auto& message = read_messages[i];
      read_data.insert(message.data);
      EXPECT_GT(message.data, 0);
      EXPECT_LE(message.data, kNumMessages);
      EXPECT_GT(message.data, prev);
      prev = message.data;
      EXPECT_EQ(message.data_size, sizeof(uint64_t));
      EXPECT_EQ(message.status, ReadMessageStatus::kOk);
    }
  };
  ValidateMessages(kReader1Offset);
  ValidateMessages(kReader2Offset);

  // No repeated messages.
  ASSERT_EQ(read_data.size(), kNumMessages,
            "Read messages do not match the number of written messages.");
}

constexpr uint32_t kMaxDataSize = 1000;
constexpr uint32_t kMaxHandleCount = 10;
constexpr char kEmptyData[kMaxDataSize] = {};

// Writes |msg_size| zeroed bytes to |channel| and |handle_count| duplicates of |event| to
// |channel|.
void WriteDataAndHandles(const zx::channel& channel, const zx::event& event, uint32_t msg_size,
                         uint32_t handle_count) {
  zx::event duplicates[kMaxHandleCount] = {};
  zx_handle_t handles[kMaxHandleCount] = {};

  ASSERT_LE(msg_size, kMaxDataSize);
  ASSERT_LE(handle_count, kMaxHandleCount);

  for (uint32_t i = 0; i < handle_count; ++i) {
    ASSERT_OK(event.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicates[i]));
  }

  // This is separate, so all duplicate handles are close if any duplication fails.
  for (uint32_t i = 0; i < handle_count; ++i) {
    handles[i] = duplicates[i].release();
  }

  ASSERT_OK(channel.write(0, kEmptyData, msg_size, handles, handle_count));
}

template <typename T>
void CheckHandleCount(const zx::object<T>& zx_object, uint32_t expected_count) {
  // Only the handle to |event| remains.
  zx_info_handle_count_t handle_info;
  ASSERT_OK(zx_object.get_info(ZX_INFO_HANDLE_COUNT, &handle_info, sizeof(zx_info_handle_count_t),
                               nullptr, nullptr));
  ASSERT_EQ(expected_count, handle_info.handle_count);
}

template <uint32_t ByteBufferSize, uint32_t HandleCount,
          bool convert_zero_elements_to_nullptr = false>
void PerformChannelCallWithSmallBuffer(const zx::channel& local, const zx::channel& remote,
                                       uint32_t reply_byte_size, uint32_t reply_handle_count,
                                       uint32_t* actual_bytes, uint32_t* actual_handles) {
  // An extra element to prevent 0 sized arrays.
  uint8_t buffer[ByteBufferSize + 1] = {};
  zx_handle_t handles[HandleCount + 1] = {};

  uint8_t* buffer_ptr = (convert_zero_elements_to_nullptr) ? nullptr : buffer;
  zx_handle_t* handles_ptr = (convert_zero_elements_to_nullptr) ? nullptr : handles;

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  ASSERT_EQ(remote.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite_past(), nullptr),
            ZX_ERR_TIMED_OUT);
  ASSERT_NO_FATAL_FAILURES(WriteDataAndHandles(local, event, reply_byte_size, reply_handle_count));
  ASSERT_EQ(remote.read(ZX_CHANNEL_READ_MAY_DISCARD, buffer_ptr, handles_ptr, ByteBufferSize,
                        HandleCount, actual_bytes, actual_handles),
            ZX_ERR_BUFFER_TOO_SMALL);
  ASSERT_EQ(remote.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite_past(), nullptr),
            ZX_ERR_TIMED_OUT);
  // At the end, only one handle should remain.
  ASSERT_NO_FATAL_FAILURES(CheckHandleCount(event, 1));
}

TEST(ChannelTest, ReadMayDiscardWithNullBuffersReturnsBufferTooSmall) {
  constexpr uint32_t kDataSize = 0;
  constexpr uint32_t kHandleCount = 0;

  zx::channel local;
  zx::channel remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  uint32_t actual_bytes = 0;
  uint32_t actual_handle_count = 0;

  ASSERT_NO_FATAL_FAILURES((PerformChannelCallWithSmallBuffer<kDataSize, kHandleCount, true>(
      local, remote, kDataSize + 1, kHandleCount + 1, &actual_bytes, &actual_handle_count)));

  EXPECT_EQ(kHandleCount + 1, actual_handle_count);
  EXPECT_EQ(kDataSize + 1, actual_bytes);
}

TEST(ChannelTest, ReadMayDiscardWithNullBufferDiscardsDataReturnsBufferTooSmall) {
  constexpr uint32_t kDataSize = 1;
  constexpr uint32_t kHandleCount = 0;

  zx::channel local;
  zx::channel remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  uint32_t actual_bytes = 0;
  uint32_t actual_handle_count = 0;

  ASSERT_NO_FATAL_FAILURES((PerformChannelCallWithSmallBuffer<kDataSize, kHandleCount, true>(
      local, remote, kDataSize + 1, kHandleCount, &actual_bytes, &actual_handle_count)));

  EXPECT_EQ(kHandleCount, actual_handle_count);
  EXPECT_EQ(kDataSize + 1, actual_bytes);
}

TEST(ChannelTest, ReadMayDiscardWithNullBufferDiscardHandlesReturnsBufferTooSmall) {
  constexpr uint32_t kDataSize = 0;
  constexpr uint32_t kHandleCount = 1;

  zx::channel local;
  zx::channel remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  uint32_t actual_bytes = 0;
  uint32_t actual_handle_count = 0;

  ASSERT_NO_FATAL_FAILURES((PerformChannelCallWithSmallBuffer<kDataSize, kHandleCount, true>(
      local, remote, kDataSize, kHandleCount + 1, &actual_bytes, &actual_handle_count)));

  EXPECT_EQ(kHandleCount + 1, actual_handle_count);
  EXPECT_EQ(kDataSize, actual_bytes);
}

TEST(ChannelTest, ReadMayDiscardWithZeroSizeBuffersDiscardHandlesAndDataReturnsBufferTooSmall) {
  constexpr uint32_t kDataSize = 0;
  constexpr uint32_t kHandleCount = 0;

  zx::channel local;
  zx::channel remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  uint32_t actual_bytes = 0;
  uint32_t actual_handle_count = 0;

  ASSERT_NO_FATAL_FAILURES((PerformChannelCallWithSmallBuffer<kDataSize, kHandleCount, true>(
      local, remote, kDataSize + 1, kHandleCount + 1, &actual_bytes, &actual_handle_count)));

  EXPECT_EQ(kHandleCount + 1, actual_handle_count);
  EXPECT_EQ(kDataSize + 1, actual_bytes);
}

TEST(ChannelTest, ReadMayDiscardWithSmallerBufferDiscardHandlesAndDateReturnsBufferTooSmall) {
  constexpr uint32_t kDataSize = 10;
  constexpr uint32_t kHandleCount = 1;

  zx::channel local;
  zx::channel remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  uint32_t actual_bytes = 0;
  uint32_t actual_handle_count = 0;

  ASSERT_NO_FATAL_FAILURES((PerformChannelCallWithSmallBuffer<kDataSize, kHandleCount>(
      local, remote, kDataSize + 1, kHandleCount + 1, &actual_bytes, &actual_handle_count)));

  EXPECT_EQ(kHandleCount + 1, actual_handle_count);
  EXPECT_EQ(kDataSize + 1, actual_bytes);
}

struct Message {
  static constexpr uint32_t kDataSize = 64;
  static constexpr uint32_t kHeaderSize = sizeof(zx_txid_t);
  static constexpr uint32_t kMaxSize = kDataSize + kHeaderSize;
  static constexpr uint32_t kHandleCount = 10;

  zx_status_t Write(const zx::channel& channel) {
    return channel.write(0, start(), byte_size(), handles, handle_count);
  }

  zx_status_t Read(const zx::channel& channel, uint32_t* actual_bytes = nullptr,
                   uint32_t* actual_handles = nullptr) {
    return channel.read(0, start(), handles, byte_size(), handle_count, actual_bytes,
                        actual_handles);
  }

  Message(uint32_t data_size = 0, uint32_t handle_count = 0) {
    this->data_size = data_size;
    this->handle_count = handle_count;
  }

  const uint8_t* start() const { return reinterpret_cast<const uint8_t*>(&id); }

  const uint8_t* end() const { return reinterpret_cast<const uint8_t*>(data) + data_size; }

  uint8_t* start() { return reinterpret_cast<uint8_t*>(&id); }

  uint8_t* end() { return reinterpret_cast<uint8_t*>(data) + data_size; }

  bool IsEquivalent(const Message& rhs) const {
    if (data_size != rhs.data_size) {
      return false;
    }

    if (memcmp(data, rhs.data, data_size) != 0) {
      return false;
    }

    if (handle_count != rhs.handle_count) {
      return false;
    }

    return true;
  }

  uint32_t byte_size() const { return static_cast<uint32_t>(end() - start()); }

  void CloseHandles() {
    for (uint32_t i = 0; i < handle_count; ++i) {
      zx_handle_close(handles[i]);
    }
  }

  zx_txid_t id = 0;
  uint32_t data[kDataSize] = {0};
  uint32_t data_size = kDataSize * sizeof(uint32_t);
  zx_handle_t handles[kHandleCount];
  uint32_t handle_count = 0;
};

TEST(ChannelTest, CallWrittenBytesSmallerThanZxTxIdReturnsInvalidArgs) {
  zx::channel local;
  zx::channel remote;

  ASSERT_OK(zx::channel::create(0, &local, &remote));

  Message request;

  Message reply;
  zx_channel_call_args_t args = {
      .wr_bytes = &request,
      .wr_handles = nullptr,
      .rd_bytes = &reply,
      .rd_handles = nullptr,
      .wr_num_bytes = sizeof(zx_txid_t) - 1,
      .wr_num_handles = 0,
      .rd_num_bytes = Message::kMaxSize,
      .rd_num_handles = 0,
  };

  uint32_t actual_bytes = 0;
  uint32_t actual_handles = 0;
  ASSERT_EQ(local.call(0, zx::time::infinite(), &args, &actual_bytes, &actual_handles),
            ZX_ERR_INVALID_ARGS);
}

template <auto ReplyFiller, uint32_t accumulated_messages = 0>
void ReplyAndWait(const Message& request, uint32_t message_count, zx::channel svc,
                  std::atomic<const char*>* error, zx::event* wait_for_event) {
  std::set<zx_txid_t> live_ids;
  std::vector<Message> live_requests;
  auto cleanup = fbl::MakeAutoCall([&svc, &live_requests]() {
    svc.reset();
    for (auto req : live_requests) {
      for (uint32_t i = 0; i < req.handle_count; ++i) {
        zx_handle_close(req.handles[i]);
      }
    }
  });
  for (uint32_t i = 0; i < message_count; ++i) {
    svc.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr);
    Message read_request = Message(request.data_size, request.handle_count);
    if (read_request.Read(svc) != ZX_OK) {
      *error = "Failed to read request.";
      return;
    }
    if (!request.IsEquivalent(read_request)) {
      *error = "Failed to validate request.";
      return;
    }
    read_request.CloseHandles();

    if (i <= accumulated_messages) {
      if (live_ids.find(read_request.id) != live_ids.end()) {
        *error = "Repeated id used for live transaction.";
        return;
      }
      live_ids.insert(read_request.id);
      live_requests.push_back(read_request);
      if (i + 1 < accumulated_messages) {
        continue;
      }
    }

    // This is the last pending message, so we reply to all pending messages and then we reply.
    for (auto req : live_requests) {
      Message reply = Message(0, 0);
      reply.id = req.id;
      ReplyFiller(&reply);
      if (reply.Write(svc) != ZX_OK) {
        *error = "Failed to write reply.";
        return;
      }
    }
  }

  if (wait_for_event != nullptr) {
    if (wait_for_event->wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), nullptr) != ZX_OK) {
      *error = "Failed to wait for signal event.";
      return;
    }
  }
}

template <auto ReplyFiller, uint32_t accumulated_messages = 0>
void Reply(const Message& request, uint32_t message_count, zx::channel svc,
           std::atomic<const char*>* error) {
  ReplyAndWait<ReplyFiller, accumulated_messages>(request, message_count, std::move(svc), error,
                                                  nullptr);
}

zx_channel_call_args_t MakeArgs(const Message& request, Message* reply) {
  zx_channel_call_args_t args;
  args.wr_bytes = request.start();
  args.wr_handles = request.handles;
  args.wr_num_bytes = request.byte_size();
  args.wr_num_handles = request.handle_count;
  args.rd_bytes = reply->start();
  args.rd_handles = reply->handles;
  args.rd_num_bytes = reply->byte_size();
  args.rd_num_handles = reply->handle_count;
  return args;
}

template <int data_size, uint32_t handles>
void ReplyFiller(Message* reply) {
  reply->data_size = data_size;

  uint32_t i = 0;
  auto cleanup = fbl::MakeAutoCall([&i, reply]() {
    for (uint32_t j = 0; j < i; ++j) {
      zx_handle_close(reply->handles[j]);
    }
  });

  reply->handle_count = handles;
  for (i = 0; i < handles; ++i) {
    zx::event event;
    if (zx::event::create(0, &event) != ZX_OK) {
      return;
    }
    reply->handles[i] = event.release();
  }
  cleanup.cancel();
}

TEST(ChannelTest, CallResponseBiggerThanRdNumBytesReturnsBufferTooSmall) {
  constexpr uint32_t kReplyDataSize = 2;
  constexpr uint32_t kReplyHandleCount = 0;

  std::atomic<const char*> error = nullptr;
  zx::channel local;
  zx::channel remote;

  ASSERT_OK(zx::channel::create(0, &local, &remote));

  Message request = Message(5 * sizeof(uint32_t), 0);
  request.id = 0x112233;
  request.data[0] = 1;
  request.data[1] = 2;
  request.data[2] = 3;
  request.data[3] = 4;
  request.data[4] = 5;

  Message reply = Message(kReplyDataSize - 1, kReplyHandleCount);
  auto args = MakeArgs(request, &reply);

  {
    AutoJoinThread service_thread(Reply<ReplyFiller<kReplyDataSize, kReplyHandleCount>>, request, 1,
                                  std::move(remote), &error);

    uint32_t actual_bytes = 0;
    uint32_t actual_handles = 0;
    ASSERT_EQ(local.call(0, zx::time::infinite(), &args, &actual_bytes, &actual_handles),
              ZX_ERR_BUFFER_TOO_SMALL);
  }

  reply.CloseHandles();
  if (error != nullptr) {
    FAIL("Service Thread reported error: %s\n", error.load());
  }
}

TEST(ChannelTest, CallResponseBiggerThanRdNumHandlesReturnsBufferTooSmall) {
  constexpr uint32_t kReplyDataSize = 0;
  constexpr uint32_t kReplyHandleCount = 2;

  std::atomic<const char*> error = nullptr;
  zx::channel local;
  zx::channel remote;

  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  Message request = Message(0, 1);
  request.id = 0x112233;
  request.handles[0] = event.release();

  Message reply = Message(0, kReplyHandleCount - 1);
  auto args = MakeArgs(request, &reply);

  {
    AutoJoinThread service_thread(Reply<ReplyFiller<kReplyDataSize, kReplyHandleCount>>, request, 1,
                                  std::move(remote), &error);
    uint32_t actual_bytes = 0;
    uint32_t actual_handles = 0;
    ASSERT_EQ(local.call(0, zx::time::infinite(), &args, &actual_bytes, &actual_handles),
              ZX_ERR_BUFFER_TOO_SMALL);
  }
  reply.CloseHandles();

  if (error != nullptr) {
    FAIL("Service Thread reported error: %s\n", error.load());
  }
}

template <uint32_t ReplyDataSize, uint32_t ReplyHandleCount>
void SuccessfullChannelCall(zx::channel local, zx::channel remote, const Message& request) {
  std::atomic<const char*> error = nullptr;
  Message reply = Message(ReplyDataSize, ReplyHandleCount);

  auto args = MakeArgs(request, &reply);
  {
    AutoJoinThread service_thread(Reply<ReplyFiller<ReplyDataSize, ReplyHandleCount>>, request, 1,
                                  std::move(remote), &error);
    uint32_t hc, bc;
    ASSERT_OK(local.call(0, zx::time::infinite(), &args, &bc, &hc));
  }
  reply.CloseHandles();

  if (error != nullptr) {
    FAIL("Service Thread reported error: %s\n", error.load());
  }
}

TEST(ChannelTest, CallBytesFitIsOk) {
  constexpr uint32_t kReplyDataSize = 5;
  constexpr uint32_t kReplyHandleCount = 0;

  Message request = Message(4, 0);

  zx::channel local;
  zx::channel remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  ASSERT_NO_FATAL_FAILURES((SuccessfullChannelCall<kReplyDataSize, kReplyHandleCount>(
      std::move(local), std::move(remote), request)));
}

TEST(ChannelTest, CallHandlesFitIsOk) {
  constexpr uint32_t kReplyDataSize = 0;
  constexpr uint32_t kReplyHandleCount = 2;

  zx::channel local;
  zx::channel remote;

  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  Message request = Message(0, 1);
  request.handles[0] = event.release();
  ASSERT_NO_FATAL_FAILURES((SuccessfullChannelCall<kReplyDataSize, kReplyHandleCount>(
      std::move(local), std::move(remote), request)));
}

TEST(ChannelTest, CallHandleAndBytesFitsIsOk) {
  constexpr uint32_t kReplyDataSize = 2;
  constexpr uint32_t kReplyHandleCount = 2;

  zx::channel local;
  zx::channel remote;

  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  Message request = Message(2, 1);
  request.handles[0] = event.release();

  ASSERT_NO_FATAL_FAILURES((SuccessfullChannelCall<kReplyDataSize, kReplyHandleCount>(
      std::move(local), std::move(remote), request)));
}

// UBSan was triggering on passing nullptr to zx_channel_call which doesn't
// accept null arguments. This is what this specific test is checking though, so
// we can just wrap the call to zx_channel_call() in a function that disables
// this UBSan check.
#ifdef __clang__
[[clang::no_sanitize("undefined")]]
#else
// Inline this so GCC doesn't see there is only one caller and it uses nullptr.
[[gnu::noinline]]
#endif
zx_status_t local_call(const zx::channel &local, zx_channel_call_args_t &args,
                       uint32_t* bytes, uint32_t* handles) {
  return zx_channel_call(local.get(), 0, zx::time::infinite().get(), &args,
                         bytes, handles);
}

TEST(ChannelTest, CallNullptrNumBytesIsInvalidArgs) {
  constexpr uint32_t kReplyDataSize = 0;
  constexpr uint32_t kReplyHandleCount = 0;

  std::atomic<const char*> error = nullptr;
  zx::channel local;
  zx::channel remote;

  ASSERT_OK(zx::channel::create(0, &local, &remote));

  Message request = Message(2, 0);
  Message reply = Message(kReplyDataSize, kReplyHandleCount);
  auto args = MakeArgs(request, &reply);
  {
    AutoJoinThread service_thread(Reply<ReplyFiller<kReplyDataSize, kReplyHandleCount>>, request, 1,
                                  std::move(remote), &error);
    uint32_t hc;
    ASSERT_EQ(local_call(local, args, nullptr, &hc), ZX_ERR_INVALID_ARGS);
  }
  reply.CloseHandles();

  if (error != nullptr) {
    FAIL("Service Thread reported error: %s\n", error.load());
  }
}

TEST(ChannelTest, CallNullptrNumHandlesInvalidArgs) {
  constexpr uint32_t kReplyDataSize = 0;
  constexpr uint32_t kReplyHandleCount = 0;

  std::atomic<const char*> error = nullptr;
  zx::channel local;
  zx::channel remote;

  ASSERT_OK(zx::channel::create(0, &local, &remote));

  Message request = Message(2, 0);
  Message reply = Message(kReplyDataSize, kReplyHandleCount);
  auto args = MakeArgs(request, &reply);
  {
    AutoJoinThread service_thread(Reply<ReplyFiller<kReplyDataSize, kReplyHandleCount>>, request, 1,
                                  std::move(remote), &error);
    uint32_t bc;
    ASSERT_EQ(local_call(local, args, &bc, nullptr), ZX_ERR_INVALID_ARGS);
  }
  reply.CloseHandles();

  if (error != nullptr) {
    FAIL("Service Thread reported error: %s\n", error.load());
  }
}

TEST(ChannelTest, CallPendingTransactionsUseDifferentIds) {
  constexpr uint32_t kReplyDataSize = 0;
  constexpr uint32_t kReplyHandleCount = 0;
  // The service thread will wait until |kAcummulatedMessages| have been read from the channel
  // before replying in the same order they came through.
  constexpr uint32_t kAccumulatedMessages = 20;

  std::atomic<const char*> error = nullptr;
  std::vector<zx_status_t> call_result(kAccumulatedMessages, ZX_OK);
  zx::channel local;
  zx::channel remote;

  ASSERT_OK(zx::channel::create(0, &local, &remote));

  Message request = Message(2, 0);
  {
    AutoJoinThread service_thread(
        Reply<ReplyFiller<kReplyDataSize, kReplyHandleCount>, kAccumulatedMessages>, request,
        kAccumulatedMessages, std::move(remote), &error);

    std::vector<AutoJoinThread> calling_threads;
    calling_threads.reserve(kAccumulatedMessages);
    for (uint32_t i = 0; i < kAccumulatedMessages; ++i) {
      calling_threads.push_back(AutoJoinThread([i, &call_result, &local, &request]() {
        Message reply = Message(kReplyDataSize, kReplyHandleCount);
        auto args = MakeArgs(request, &reply);
        uint32_t bc, hc;
        call_result[i] = local.call(0, zx::time::infinite(), &args, &bc, &hc);
        if (call_result[i] == ZX_OK) {
          reply.CloseHandles();
        }
      }));
    }
  }

  for (auto call_status : call_result) {
    EXPECT_OK(call_status, "channel::call failed in client thread.");
  }

  if (error != nullptr) {
    FAIL("Service Thread reported error: %s\n", error.load());
  }
}

TEST(ChannelTest, CallDeadlineExceededReturnsTimedOut) {
  constexpr uint32_t kReplyDataSize = 0;
  constexpr uint32_t kReplyHandleCount = 0;
  constexpr uint32_t kAccumulatedMessages = 2;

  std::atomic<const char*> error = nullptr;
  zx::channel local;
  zx::channel remote;

  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  Message request = Message(2, 0);
  Message reply = Message(kReplyDataSize, kReplyHandleCount);
  auto args = MakeArgs(request, &reply);
  {
    AutoJoinThread service_thread(
        ReplyAndWait<ReplyFiller<kReplyDataSize, kReplyHandleCount>, kAccumulatedMessages>, request,
        kAccumulatedMessages - 1, std::move(remote), &error, &event);
    uint32_t bc, hc;
    ASSERT_EQ(local.call(0, zx::time::infinite_past(), &args, &bc, &hc), ZX_ERR_TIMED_OUT);
    event.signal(0, ZX_USER_SIGNAL_0);
  }
  reply.CloseHandles();

  if (error != nullptr) {
    FAIL("Service Thread reported error: %s\n", error.load());
  }
}

TEST(ChannelTest, CallConsumesHandlesOnSuccess) {
  constexpr uint32_t kReplyDataSize = 0;
  constexpr uint32_t kReplyHandleCount = 0;

  std::atomic<const char*> error = nullptr;
  zx::channel local;
  zx::channel remote;

  ASSERT_OK(zx::channel::create(0, &local, &remote));
  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  zx::event event_2;
  ASSERT_OK(zx::event::create(0, &event_2));

  Message request = Message(0, 2);
  request.handles[0] = event.release();
  request.handles[1] = event_2.release();

  Message reply = Message(kReplyDataSize, kReplyHandleCount);

  auto args = MakeArgs(request, &reply);
  {
    AutoJoinThread service_thread(Reply<ReplyFiller<kReplyDataSize, kReplyHandleCount>>, request, 1,
                                  std::move(remote), &error);
    uint32_t hc, bc;
    ASSERT_OK(local.call(0, zx::time::infinite(), &args, &bc, &hc));
  }

  reply.CloseHandles();

  for (uint32_t i = 0; i < request.handle_count; ++i) {
    ASSERT_EQ(ZX_ERR_BAD_HANDLE, zx_handle_close(request.handles[i]));
  }

  if (error != nullptr) {
    FAIL("Service Thread reported error: %s\n", error.load());
  }
}

TEST(ChannelTest, CallConsumesHandlesOnError) {
  constexpr uint32_t kReplyDataSize = 0;
  constexpr uint32_t kReplyHandleCount = 0;

  std::atomic<const char*> error = nullptr;
  zx::channel local;
  zx::channel remote;

  ASSERT_OK(zx::channel::create(0, &local, &remote));
  remote.reset();
  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  zx::event event_2;
  ASSERT_OK(zx::event::create(0, &event_2));

  Message request = Message(0, 2);
  request.handles[0] = event.release();
  request.handles[1] = event_2.release();

  Message reply = Message(kReplyDataSize, kReplyHandleCount);

  auto args = MakeArgs(request, &reply);
  {
    uint32_t hc, bc;
    ASSERT_EQ(ZX_ERR_PEER_CLOSED, local.call(0, zx::time::infinite(), &args, &bc, &hc));
  }

  reply.CloseHandles();

  EXPECT_EQ(2, request.handle_count);
  for (uint32_t i = 0; i < request.handle_count; ++i) {
    ASSERT_EQ(ZX_ERR_BAD_HANDLE, zx_handle_close(request.handles[i]));
  }

  if (error != nullptr) {
    FAIL("Service Thread reported error: %s\n", error.load());
  }
}

TEST(ChannelTest, CallNotifiedOnPeerClosed) {
  zx::channel local;
  zx::channel remote;

  ASSERT_OK(zx::channel::create(0, &local, &remote));

  Message request = Message(0, 0);
  Message reply = Message(0, 0);

  auto args = MakeArgs(request, &reply);
  {
    AutoJoinThread service_thread(
        [](zx::channel svc) {
          // Wait until call message is received.
          svc.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr);
          svc.reset();
        },
        std::move(remote));

    uint32_t bc, hc;
    ASSERT_EQ(ZX_ERR_PEER_CLOSED, local.call(0, zx::time::infinite(), &args, &bc, &hc));
  }
}

// Nest 200 channels, each one in the payload of the previous one. Without
// the SafeDeleter in fbl_recycle() this blows the kernel stack when calling
// the destructors.
TEST(ChannelTest, NestingIsOk) {
  constexpr uint32_t kNestedCount = 200;
  std::vector<zx::channel> local(kNestedCount);
  std::vector<zx::channel> remote(kNestedCount);

  for (uint32_t i = 0; i < kNestedCount; ++i) {
    ASSERT_OK(zx::channel::create(0, &local[i], &remote[i]));
  }

  for (uint32_t i = kNestedCount - 1; i > 0; --i) {
    zx_handle_t handles[2] = {local[i].release(), remote[i].release()};
    ASSERT_OK(local[i - 1].write(0, nullptr, 0, handles, 2));
  }

  // All handles except those at 0, have been transferred to a channel.
  ASSERT_TRUE(local[0].is_valid());
  ASSERT_TRUE(remote[0].is_valid());

  // Close the handles and for destructions.
  local[0].reset();
  remote[0].reset();
}

TEST(ChannelTest, WriteSelfHandleReturnsNotSupported) {
  zx::channel local;
  zx::channel remote;

  ASSERT_OK(zx::channel::create(0, &local, &remote));

  zx::unowned_channel unowned_local(local.get());
  zx_handle_t local_handle = local.release();
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, unowned_local->write(0, nullptr, 0, &local_handle, 1));

  zx_signals_t signals;
  ASSERT_OK(remote.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite_past(), &signals));
  ASSERT_EQ(ZX_CHANNEL_PEER_CLOSED, signals);
}

TEST(ChannelTest, ReadEtcHandleInfoValidation) {
  zx::channel local;
  zx::channel remote;

  ASSERT_OK(zx::channel::create(0, &local, &remote));

  // Handles to send.
  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  zx::event event_with_less_rights;
  ASSERT_OK(event.duplicate(ZX_RIGHTS_BASIC & ~ZX_RIGHT_WAIT, &event_with_less_rights));

  zx::fifo fifo_local, fifo_remote;
  ASSERT_OK(zx::fifo::create(32, 8, 0, &fifo_local, &fifo_remote));

  zx_handle_t handles[4] = {
      fifo_local.release(),
      event.release(),
      event_with_less_rights.release(),
      fifo_remote.release(),
  };

  ASSERT_OK(local.write(0, nullptr, 0, handles, 4));

  zx_handle_info_t read_handles[4] = {};
  uint32_t actual_bytes = 0;
  uint32_t actual_handles = 0;

  ASSERT_OK(remote.read_etc(0, nullptr, read_handles, 0, 4, &actual_bytes, &actual_handles));

  ASSERT_EQ(4, actual_handles);
  ASSERT_EQ(0, actual_bytes);

  EXPECT_EQ(read_handles[0].type, ZX_OBJ_TYPE_FIFO);
  EXPECT_EQ(read_handles[0].rights, ZX_DEFAULT_FIFO_RIGHTS);

  EXPECT_EQ(read_handles[1].type, ZX_OBJ_TYPE_EVENT);
  EXPECT_EQ(read_handles[1].rights, ZX_DEFAULT_EVENT_RIGHTS);

  EXPECT_EQ(read_handles[2].type, ZX_OBJ_TYPE_EVENT);
  EXPECT_EQ(read_handles[2].rights, ZX_RIGHTS_BASIC & ~ZX_RIGHT_WAIT);

  EXPECT_EQ(read_handles[3].type, ZX_OBJ_TYPE_FIFO);
  EXPECT_EQ(read_handles[3].rights, ZX_DEFAULT_FIFO_RIGHTS);
}

TEST(ChannelTest, ReadAndWriteWithMultipleSizes) {
  zx::channel local;
  zx::channel remote;

  ASSERT_OK(zx::channel::create(0, &local, &remote));

  constexpr uint32_t kNumMessages = 1000;
  // Use the seed that was passed as cmd or generated by the library.
  unsigned int seed = zxtest::Runner::GetInstance()->random_seed();
  for (uint32_t i = 0; i < kNumMessages; ++i) {
    uint32_t num_bytes = rand_r(&seed) % ZX_CHANNEL_MAX_MSG_BYTES;
    uint32_t num_handles = rand_r(&seed) % ZX_CHANNEL_MAX_MSG_HANDLES;

    uint8_t data[num_bytes + 1];
    zx_handle_t handles[num_handles + 1];
    memset(data, 0, num_bytes + 1);

    std::vector<zx::event> safe_handles(num_handles + 1);

    for (uint32_t j = 0; j < num_handles; ++j) {
      ASSERT_OK(zx::event::create(0, &safe_handles[j]));
    }

    data[0] = static_cast<uint8_t>(i % std::numeric_limits<uint8_t>::max());

    // Transfer handles
    for (uint32_t j = 0; j < num_handles; ++j) {
      handles[j] = safe_handles[j].release();
    }

    ASSERT_OK(local.write(0, data, num_bytes, handles, num_handles));

    uint8_t read_data[num_bytes + 1];
    zx_handle_t read_handles[num_handles + 1];
    uint32_t actual_bytes = 0;
    uint32_t actual_handles = 0;

    ASSERT_OK(remote.read(0, read_data, read_handles, num_bytes, num_handles, &actual_bytes,
                          &actual_handles));
    // Transfer handles to safe_handles so they are destroyed on destruction.
    for (uint32_t j = 0; j < num_handles; ++j) {
      safe_handles[j].reset(read_handles[j]);
    }
    ASSERT_EQ(num_bytes, actual_bytes);
    ASSERT_EQ(num_handles, actual_handles);
    if (num_bytes > 0) {
      ASSERT_EQ(data[0], read_data[0]);
    }
  }
}

}  // namespace
}  // namespace channel
