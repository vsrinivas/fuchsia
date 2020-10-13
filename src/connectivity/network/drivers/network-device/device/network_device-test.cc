// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/sync/completion.h>
#include <lib/syslog/global.h>

#include <zxtest/cpp/zxtest.h>
#include <zxtest/zxtest.h>

#include "device_interface.h"
#include "log.h"
#include "test_util.h"

// Enable timeouts only to test things locally, committed code should not use timeouts.
#define ENABLE_TIMEOUTS 0

#if ENABLE_TIMEOUTS
#define TEST_DEADLINE zx::deadline_after(zx::msec(5000))
#else
#define TEST_DEADLINE zx::time::infinite()
#endif

namespace network {
namespace testing {

class NetworkDeviceTest : public zxtest::Test {
 public:
  void SetUp() override {
    fx_logger_config_t log_cfg = {
        .min_severity = FX_LOG_TRACE,
        .console_fd = dup(STDOUT_FILENO),
        .log_service_channel = ZX_HANDLE_INVALID,
        .tags = nullptr,
        .num_tags = 0,
    };
    fx_log_reconfigure(&log_cfg);
  }

  void TearDown() override { DiscardDeviceSync(); }

  void DiscardDeviceSync() {
    if (device_) {
      sync_completion_t completer;
      device_->Teardown([&completer, this]() {
        LOG_TRACE("Test: Teardown complete");
        device_ = nullptr;
        sync_completion_signal(&completer);
      });
      ASSERT_OK(sync_completion_wait_deadline(&completer, TEST_DEADLINE.get()));
    }
  }

  zx_status_t WaitEvents(zx_signals_t signals, zx::time deadline) {
    zx_status_t status = impl_.events().wait_one(signals, deadline, nullptr);
    if (status == ZX_OK) {
      impl_.events().signal(signals, 0);
    }
    return status;
  }

  [[nodiscard]] zx_status_t WaitStart(zx::time deadline = TEST_DEADLINE) {
    return WaitEvents(kEventStart, deadline);
  }

  [[nodiscard]] zx_status_t WaitStop(zx::time deadline = TEST_DEADLINE) {
    return WaitEvents(kEventStop, deadline);
  }

  [[nodiscard]] zx_status_t WaitSessionStarted(zx::time deadline = TEST_DEADLINE) {
    return WaitEvents(kEventSessionStarted, deadline);
  }

  [[nodiscard]] zx_status_t WaitTx(zx::time deadline = TEST_DEADLINE) {
    return WaitEvents(kEventTx, deadline);
  }

  [[nodiscard]] zx_status_t WaitRxAvailable(zx::time deadline = TEST_DEADLINE) {
    return WaitEvents(kEventRxAvailable, deadline);
  }

  async_dispatcher_t* dispatcher() {
    if (!loop_) {
      loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNeverAttachToThread);
      EXPECT_OK(loop_->StartThread("messenger-loop", nullptr));
    }
    return loop_->dispatcher();
  }

  zx::channel OpenConnection() {
    zx::channel server_end, client_end;
    EXPECT_OK(zx::channel::create(0, &client_end, &server_end));
    EXPECT_OK(device_->Bind(std::move(server_end)));
    return client_end;
  }

  zx_status_t CreateDevice() {
    if (device_) {
      return ZX_ERR_INTERNAL;
    }
    return impl_.CreateChild(dispatcher(), &device_);
  }

  zx_status_t OpenSession(
      TestSession* session, netdev::SessionFlags flags = netdev::SessionFlags::PRIMARY,
      uint16_t num_descriptors = kDefaultDescriptorCount,
      uint64_t buffer_size = kDefaultBufferLength,
      fidl::VectorView<netdev::FrameType> frame_types = fidl::VectorView<netdev::FrameType>()) {
    // automatically increment to test_session_(a, b, c, etc...)
    char session_name[] = "test_session_a";
    session_name[strlen(session_name) - 1] = static_cast<char>('a' + session_counter_);
    session_counter_++;

    auto connection = OpenConnection();
    return session->Open(zx::unowned(connection), session_name, flags, num_descriptors, buffer_size,
                         std::move(frame_types));
  }

 protected:
  FakeNetworkDeviceImpl impl_;
  std::unique_ptr<async::Loop> loop_;
  int8_t session_counter_ = 0;
  std::unique_ptr<NetworkDeviceInterface> device_;
};

void PrintVec(const std::string& name, const std::vector<uint8_t>& vec) {
  printf("Vec %s: ", name.c_str());
  for (const auto& x : vec) {
    printf("%02X ", x);
  }
  printf("\n");
}

TEST_F(NetworkDeviceTest, CanCreate) { ASSERT_OK(CreateDevice()); }

TEST_F(NetworkDeviceTest, GetInfo) {
  impl_.info().min_rx_buffer_length = 2048;
  impl_.info().min_tx_buffer_length = 60;
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  auto rsp = netdev::Device::Call::GetInfo(zx::unowned(connection));
  ASSERT_OK(rsp.status());
  auto& info = rsp.value().info;
  EXPECT_EQ(info.tx_depth, impl_.info().tx_depth * 2);
  EXPECT_EQ(info.rx_depth, impl_.info().rx_depth * 2);
  EXPECT_EQ(info.min_rx_buffer_length, impl_.info().min_rx_buffer_length);
  EXPECT_EQ(info.min_tx_buffer_length, impl_.info().min_tx_buffer_length);
  EXPECT_EQ(info.max_buffer_length, impl_.info().max_buffer_length);
  EXPECT_EQ(info.min_tx_buffer_tail, impl_.info().tx_tail_length);
  EXPECT_EQ(info.min_tx_buffer_head, impl_.info().tx_head_length);
  EXPECT_EQ(info.descriptor_version, NETWORK_DEVICE_DESCRIPTOR_VERSION);
  EXPECT_EQ(info.buffer_alignment, impl_.info().buffer_alignment);
  static_assert(sizeof(buffer_descriptor_t) % 8 == 0);
  EXPECT_EQ(info.min_descriptor_length, sizeof(buffer_descriptor_t) / sizeof(uint64_t));
  EXPECT_EQ(info.class_, netdev::DeviceClass::ETHERNET);
  EXPECT_EQ(info.tx_accel.count(), impl_.info().tx_accel_count);
  EXPECT_EQ(info.rx_accel.count(), impl_.info().rx_accel_count);
  EXPECT_EQ(info.rx_types.count(), impl_.info().rx_types_count);
  for (size_t i = 0; i < info.rx_types.count(); i++) {
    EXPECT_EQ(static_cast<uint8_t>(info.rx_types.at(i)), impl_.info().rx_types_list[i]);
  }
  EXPECT_EQ(info.tx_types.count(), impl_.info().tx_types_count);
  for (size_t i = 0; i < info.tx_types.count(); i++) {
    EXPECT_EQ(static_cast<uint8_t>(info.tx_types.at(i).type), impl_.info().tx_types_list[i].type);
    EXPECT_EQ(info.tx_types.at(i).features, impl_.info().tx_types_list[i].features);
    EXPECT_EQ(static_cast<uint32_t>(info.tx_types.at(i).supported_flags),
              impl_.info().tx_types_list[i].supported_flags);
  }
}

TEST_F(NetworkDeviceTest, MinReportedBufferAlignment) {
  // Tests that device creation is rejected with an invalid buffer_alignment value.
  impl_.info().buffer_alignment = 0;
  ASSERT_STATUS(CreateDevice(), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(NetworkDeviceTest, InvalidRxThreshold) {
  // Tests that device creation is rejected with an invalid rx_threshold value.
  impl_.info().rx_threshold = impl_.info().rx_depth + 1;
  ASSERT_STATUS(CreateDevice(), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(NetworkDeviceTest, OpenSession) {
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  for (uint16_t i = 0; i < 16; i++) {
    session.ResetDescriptor(i);
    session.SendRx(i);
  }
  session.SetPaused(false);
  ASSERT_OK(WaitStart());
  ASSERT_OK(WaitRxAvailable());
}

TEST_F(NetworkDeviceTest, RxBufferBuild) {
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  session.SetPaused(false);
  ASSERT_OK(WaitStart());
  constexpr size_t kDescTests = 3;
  // send three Rx descriptors:
  // - A simple descriptor with just data length
  // - A descriptor with head and tail removed
  // - A chained descriptor with simple data lengths.
  uint16_t all_descs[kDescTests + 1] = {0, 1, 2};
  session.ResetDescriptor(0);
  auto* desc = session.ResetDescriptor(1);
  desc->head_length = 16;
  desc->tail_length = 32;
  desc->data_length -= desc->head_length + desc->tail_length;
  desc = session.ResetDescriptor(2);
  desc->data_length = 10;
  desc->chain_length = 2;
  desc->nxt = 3;
  desc = session.ResetDescriptor(3);
  desc->data_length = 20;
  desc->chain_length = 1;
  desc->nxt = 4;
  desc = session.ResetDescriptor(4);
  desc->data_length = 30;
  desc->chain_length = 0;
  size_t sent;
  ASSERT_OK(session.SendRx(all_descs, kDescTests, &sent));
  ASSERT_EQ(sent, kDescTests);
  ASSERT_OK(WaitRxAvailable());
  RxReturnTransaction return_session(&impl_);
  // load the buffers from the fake device implementation and check them.
  // We call "pop_back" on the buffer list because network_device feeds Rx buffers in a LIFO order.
  // check first descriptor:
  auto rx = impl_.rx_buffers().pop_back();
  ASSERT_TRUE(rx);
  ASSERT_EQ(rx->buff().data.parts_count, 1);
  ASSERT_EQ(rx->buff().data.parts_list[0].offset, session.descriptor(0)->offset);
  ASSERT_EQ(rx->buff().data.parts_list[0].length, kDefaultBufferLength);
  rx->return_buffer().total_length = 64;
  rx->return_buffer().meta.flags = static_cast<uint32_t>(netdev::RxFlags::RX_ACCEL_0);
  return_session.Enqueue(std::move(rx));
  // check second descriptor:
  rx = impl_.rx_buffers().pop_back();
  ASSERT_TRUE(rx);
  ASSERT_EQ(rx->buff().data.parts_count, 1);
  desc = session.descriptor(1);
  ASSERT_EQ(rx->buff().data.parts_list[0].offset, desc->offset + desc->head_length);
  ASSERT_EQ(rx->buff().data.parts_list[0].length,
            kDefaultBufferLength - desc->head_length - desc->tail_length);
  rx->return_buffer().total_length = 15;
  rx->return_buffer().meta.flags = static_cast<uint32_t>(netdev::RxFlags::RX_ACCEL_1);
  return_session.Enqueue(std::move(rx));
  // check third descriptor:
  rx = impl_.rx_buffers().pop_back();
  ASSERT_TRUE(rx);
  ASSERT_EQ(rx->buff().data.parts_count, 3);
  auto* d0 = session.descriptor(2);
  auto* d1 = session.descriptor(3);
  auto* d2 = session.descriptor(4);
  ASSERT_EQ(rx->buff().data.parts_list[0].offset, d0->offset);
  ASSERT_EQ(rx->buff().data.parts_list[0].length, d0->data_length);
  ASSERT_EQ(rx->buff().data.parts_list[1].offset, d1->offset);
  ASSERT_EQ(rx->buff().data.parts_list[1].length, d1->data_length);
  ASSERT_EQ(rx->buff().data.parts_list[2].offset, d2->offset);
  ASSERT_EQ(rx->buff().data.parts_list[2].length, d2->data_length);
  // set the total length up to a part of the middle buffer:
  rx->return_buffer().total_length = 25;
  rx->return_buffer().meta.flags = static_cast<uint32_t>(netdev::RxFlags::RX_ACCEL_2);
  return_session.Enqueue(std::move(rx));
  // ensure no more rx buffers were actually returned:
  ASSERT_TRUE(impl_.rx_buffers().is_empty());
  // commit the returned buffers
  return_session.Commit();
  // check that all descriptors were returned to the queue:
  size_t read_back;
  ASSERT_OK(session.FetchRx(all_descs, kDescTests + 1, &read_back));
  ASSERT_EQ(read_back, kDescTests);
  EXPECT_EQ(all_descs[0], 0);
  EXPECT_EQ(all_descs[1], 1);
  EXPECT_EQ(all_descs[2], 2);
  // finally check all the stuff that was returned:
  // check returned first descriptor:
  desc = session.descriptor(0);
  EXPECT_EQ(desc->offset, session.canonical_offset(0));
  EXPECT_EQ(desc->chain_length, 0);
  EXPECT_EQ(desc->inbound_flags, static_cast<uint32_t>(netdev::RxFlags::RX_ACCEL_0));
  EXPECT_EQ(desc->head_length, 0);
  EXPECT_EQ(desc->data_length, 64);
  EXPECT_EQ(desc->tail_length, 0);
  // check returned second descriptor:
  desc = session.descriptor(1);
  EXPECT_EQ(desc->offset, session.canonical_offset(1));
  EXPECT_EQ(desc->chain_length, 0);
  EXPECT_EQ(desc->inbound_flags, static_cast<uint32_t>(netdev::RxFlags::RX_ACCEL_1));
  EXPECT_EQ(desc->head_length, 16);
  EXPECT_EQ(desc->data_length, 15);
  EXPECT_EQ(desc->tail_length, 32);
  // check returned third descriptor and the chained ones:
  desc = session.descriptor(2);
  EXPECT_EQ(desc->offset, session.canonical_offset(2));
  EXPECT_EQ(desc->chain_length, 2);
  EXPECT_EQ(desc->nxt, 3);
  EXPECT_EQ(desc->inbound_flags, static_cast<uint32_t>(netdev::RxFlags::RX_ACCEL_2));
  EXPECT_EQ(desc->head_length, 0);
  EXPECT_EQ(desc->data_length, 10);
  EXPECT_EQ(desc->tail_length, 0);
  desc = session.descriptor(3);
  EXPECT_EQ(desc->offset, session.canonical_offset(3));
  EXPECT_EQ(desc->chain_length, 1);
  EXPECT_EQ(desc->nxt, 4);
  EXPECT_EQ(desc->inbound_flags, 0);
  EXPECT_EQ(desc->head_length, 0);
  EXPECT_EQ(desc->data_length, 15);
  EXPECT_EQ(desc->tail_length, 0);
  desc = session.descriptor(4);
  EXPECT_EQ(desc->offset, session.canonical_offset(4));
  EXPECT_EQ(desc->chain_length, 0);
  EXPECT_EQ(desc->inbound_flags, 0);
  EXPECT_EQ(desc->head_length, 0);
  EXPECT_EQ(desc->data_length, 0);
  EXPECT_EQ(desc->tail_length, 0);
}

TEST_F(NetworkDeviceTest, TxBufferBuild) {
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  session.SetPaused(false);
  ASSERT_OK(WaitStart());
  constexpr size_t kDescTests = 3;
  // send three Rx descriptors:
  // - A simple descriptor with just data length
  // - A descriptor with head and tail removed
  // - A chained descriptor with simple data lengths.
  uint16_t all_descs[kDescTests + 1] = {0, 1, 2};
  session.ResetDescriptor(0);
  auto* desc = session.ResetDescriptor(1);
  desc->head_length = 16;
  desc->tail_length = 32;
  desc->data_length -= desc->head_length + desc->tail_length;
  desc = session.ResetDescriptor(2);
  desc->data_length = 10;
  desc->chain_length = 2;
  desc->nxt = 3;
  desc = session.ResetDescriptor(3);
  desc->data_length = 20;
  desc->chain_length = 1;
  desc->nxt = 4;
  desc = session.ResetDescriptor(4);
  desc->data_length = 30;
  desc->chain_length = 0;
  size_t sent;
  ASSERT_OK(session.SendTx(all_descs, kDescTests, &sent));
  ASSERT_EQ(sent, kDescTests);
  ASSERT_OK(WaitTx());
  TxReturnTransaction return_session(&impl_);
  // load the buffers from the fake device implementation and check them.
  auto tx = impl_.tx_buffers().pop_front();
  ASSERT_TRUE(tx);
  ASSERT_EQ(tx->buff().data.parts_count, 1);
  ASSERT_EQ(tx->buff().data.parts_list[0].offset, session.descriptor(0)->offset);
  ASSERT_EQ(tx->buff().data.parts_list[0].length, kDefaultBufferLength);
  return_session.Enqueue(std::move(tx));
  // check second descriptor:
  tx = impl_.tx_buffers().pop_front();
  ASSERT_TRUE(tx);
  ASSERT_EQ(tx->buff().data.parts_count, 1);
  desc = session.descriptor(1);
  ASSERT_EQ(tx->buff().data.parts_list[0].offset, desc->offset + desc->head_length);
  ASSERT_EQ(tx->buff().data.parts_list[0].length,
            kDefaultBufferLength - desc->head_length - desc->tail_length);
  tx->set_status(ZX_ERR_UNAVAILABLE);
  return_session.Enqueue(std::move(tx));
  // check third descriptor:
  tx = impl_.tx_buffers().pop_front();
  ASSERT_TRUE(tx);
  ASSERT_EQ(tx->buff().data.parts_count, 3);
  auto* d0 = session.descriptor(2);
  auto* d1 = session.descriptor(3);
  auto* d2 = session.descriptor(4);
  ASSERT_EQ(tx->buff().data.parts_list[0].offset, d0->offset);
  ASSERT_EQ(tx->buff().data.parts_list[0].length, d0->data_length);
  ASSERT_EQ(tx->buff().data.parts_list[1].offset, d1->offset);
  ASSERT_EQ(tx->buff().data.parts_list[1].length, d1->data_length);
  ASSERT_EQ(tx->buff().data.parts_list[2].offset, d2->offset);
  ASSERT_EQ(tx->buff().data.parts_list[2].length, d2->data_length);
  tx->set_status(ZX_ERR_NOT_SUPPORTED);
  return_session.Enqueue(std::move(tx));
  // ensure no more tx buffers were actually enqueued:
  ASSERT_TRUE(impl_.tx_buffers().is_empty());
  // commit the returned buffers
  return_session.Commit();
  // check that all descriptors were returned to the queue:
  size_t read_back;

  ASSERT_OK(session.FetchTx(all_descs, kDescTests + 1, &read_back));
  ASSERT_EQ(read_back, kDescTests);
  EXPECT_EQ(all_descs[0], 0);
  EXPECT_EQ(all_descs[1], 1);
  EXPECT_EQ(all_descs[2], 2);
  // check the status of the returned descriptors
  desc = session.descriptor(0);
  EXPECT_EQ(desc->return_flags, 0);
  desc = session.descriptor(1);
  EXPECT_EQ(desc->return_flags, static_cast<uint32_t>(netdev::TxReturnFlags::TX_RET_ERROR |
                                                      netdev::TxReturnFlags::TX_RET_NOT_AVAILABLE));
  desc = session.descriptor(2);
  EXPECT_EQ(desc->return_flags, static_cast<uint32_t>(netdev::TxReturnFlags::TX_RET_ERROR |
                                                      netdev::TxReturnFlags::TX_RET_NOT_SUPPORTED));
}

TEST_F(NetworkDeviceTest, SessionEpitaph) {
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  ASSERT_OK(session.Close());
  // closing the session should cause a stop:
  ASSERT_OK(WaitStop());
  // wait for epitaph to show up in channel
  ASSERT_OK(session.channel().wait_one(ZX_CHANNEL_READABLE, TEST_DEADLINE, nullptr));
  fidl_epitaph_t epitaph;
  uint32_t actual_bytes;
  ASSERT_OK(
      session.channel().read(0, &epitaph, nullptr, sizeof(epitaph), 0, &actual_bytes, nullptr));
  ASSERT_EQ(actual_bytes, sizeof(epitaph));
  ASSERT_EQ(epitaph.error, ZX_ERR_CANCELED);
  // also the channel must be closed after:
  ASSERT_OK(session.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, TEST_DEADLINE, nullptr));
}

TEST_F(NetworkDeviceTest, SessionPauseUnpause) {
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session;
  // pausing and unpausing the session makes the device start and stop:
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  ASSERT_OK(session.SetPaused(true));
  ASSERT_OK(WaitStop());
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  ASSERT_OK(session.SetPaused(true));
  ASSERT_OK(WaitStop());
}

TEST_F(NetworkDeviceTest, TwoSessionsTx) {
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  TestSession session_b;
  ASSERT_OK(OpenSession(&session_b));
  session_a.SetPaused(false);
  ASSERT_OK(WaitSessionStarted());
  session_b.SetPaused(false);
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(WaitStart());
  // send something from each session, both should succeed:
  std::vector<uint8_t> sent_buff_a({1, 2, 3, 4});
  std::vector<uint8_t> sent_buff_b({5, 6});
  session_a.SendTxData(0, sent_buff_a);
  ASSERT_OK(WaitTx());
  session_b.SendTxData(1, sent_buff_b);
  ASSERT_OK(WaitTx());
  // wait until we have two frames waiting:
  auto buff_a = impl_.tx_buffers().pop_front();
  auto buff_b = impl_.tx_buffers().pop_front();
  std::vector<uint8_t> data_a;
  std::vector<uint8_t> data_b;
  auto vmo_provider = impl_.VmoGetter();
  ASSERT_OK(buff_a->GetData(&data_a, vmo_provider));
  ASSERT_OK(buff_b->GetData(&data_b, vmo_provider));
  // can't rely on ordering here:
  if (data_a.size() != sent_buff_a.size()) {
    std::swap(buff_a, buff_b);
    std::swap(data_a, data_b);
  }
  PrintVec("data_a", data_a);
  PrintVec("data_b", data_b);
  ASSERT_EQ(data_a, sent_buff_a);
  ASSERT_EQ(data_b, sent_buff_b);
  // return both buffers and ensure they get to the correct sessions:
  buff_a->set_status(ZX_OK);
  buff_b->set_status(ZX_ERR_UNAVAILABLE);
  TxReturnTransaction tx_ret(&impl_);
  tx_ret.Enqueue(std::move(buff_a));
  tx_ret.Enqueue(std::move(buff_b));
  tx_ret.Commit();

  uint16_t rd;
  ASSERT_OK(session_a.FetchTx(&rd));
  ASSERT_EQ(rd, 0);
  ASSERT_OK(session_b.FetchTx(&rd));
  ASSERT_EQ(rd, 1);
  ASSERT_EQ(session_a.descriptor(0)->return_flags, 0);
  ASSERT_EQ(session_b.descriptor(1)->return_flags,
            static_cast<uint32_t>(netdev::TxReturnFlags::TX_RET_ERROR |
                                  netdev::TxReturnFlags::TX_RET_NOT_AVAILABLE));
}

TEST_F(NetworkDeviceTest, TwoSessionsRx) {
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  TestSession session_b;
  ASSERT_OK(OpenSession(&session_b));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(session_b.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(WaitStart());
  constexpr uint16_t kBufferCount = 5;
  constexpr size_t kDataLen = 15;
  uint16_t desc_buff[kBufferCount];
  for (uint16_t i = 0; i < kBufferCount; i++) {
    session_a.ResetDescriptor(i);
    session_b.ResetDescriptor(i);
    desc_buff[i] = i;
  }
  ASSERT_OK(session_a.SendRx(desc_buff, kBufferCount, nullptr));
  ASSERT_OK(session_b.SendRx(desc_buff, kBufferCount, nullptr));

  ASSERT_OK(WaitRxAvailable());
  auto vmo_provider = impl_.VmoGetter();
  RxReturnTransaction return_session(&impl_);
  for (uint16_t i = 0; i < kBufferCount; i++) {
    auto buff = impl_.rx_buffers().pop_front();
    std::vector<uint8_t> data(kDataLen, static_cast<uint8_t>(i));
    ASSERT_OK(buff->WriteData(data, vmo_provider));
    return_session.Enqueue(std::move(buff));
  }
  return_session.Commit();

  auto checker = [kBufferCount, kDataLen](TestSession* session) {
    uint16_t descriptors[kBufferCount];
    size_t rd;
    ASSERT_OK(session->FetchRx(descriptors, kBufferCount, &rd));
    ASSERT_EQ(rd, kBufferCount);
    for (uint32_t i = 0; i < kBufferCount; i++) {
      auto* desc = session->descriptor(descriptors[i]);
      ASSERT_EQ(desc->data_length, kDataLen);
      auto* data = session->buffer(desc->offset);
      for (uint32_t j = 0; j < kDataLen; j++) {
        ASSERT_EQ(*data, static_cast<uint8_t>(i));
        data++;
      }
    }
  };
  checker(&session_a);
  checker(&session_b);
}

TEST_F(NetworkDeviceTest, ListenSession) {
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  TestSession session_b;
  ASSERT_OK(OpenSession(&session_b, netdev::SessionFlags::LISTEN_TX));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(session_b.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(WaitStart());
  // Get an Rx descriptor ready on session b:
  session_b.ResetDescriptor(0);
  ASSERT_OK(session_b.SendRx(0));

  // send data from session a:
  std::vector<uint8_t> send_buff({1, 2, 3, 4});
  session_a.SendTxData(0, send_buff);
  ASSERT_OK(WaitTx());

  uint16_t desc_idx;
  ASSERT_OK(session_b.FetchRx(&desc_idx));
  ASSERT_EQ(desc_idx, 0);
  auto* desc = session_b.descriptor(0);
  ASSERT_EQ(desc->data_length, send_buff.size());
  auto* data = session_b.buffer(desc->offset);
  ASSERT_BYTES_EQ(data, &send_buff.at(0), send_buff.size());
}

TEST_F(NetworkDeviceTest, ClosingPrimarySession) {
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  TestSession session_b;
  ASSERT_OK(OpenSession(&session_b));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(session_b.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  // send one buffer on each session
  auto* d = session_a.ResetDescriptor(0);
  d->data_length = kDefaultBufferLength / 2;
  session_b.ResetDescriptor(1);
  ASSERT_OK(session_a.SendRx(0));
  ASSERT_OK(session_b.SendRx(1));
  ASSERT_OK(WaitRxAvailable());
  // impl_ now owns session_a's RxBuffer
  auto rx_buff = impl_.rx_buffers().pop_front();
  ASSERT_EQ(rx_buff->buff().data.parts_list[0].length, kDefaultBufferLength / 2);
  // let's close session_a, it should not be closed until we return the buffers
  ASSERT_OK(session_a.Close());
  ASSERT_EQ(session_a.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(zx::msec(20)),
                                         nullptr),
            ZX_ERR_TIMED_OUT);
  // and now return data.
  rx_buff->return_buffer().total_length = 5;
  RxReturnTransaction rx_transaction(&impl_);
  rx_transaction.Enqueue(std::move(rx_buff));
  rx_transaction.Commit();

  // Session a should be closed...
  ASSERT_OK(session_a.WaitClosed(TEST_DEADLINE));
  /// ...and Session b should still receive the data.
  uint16_t desc;
  ASSERT_OK(session_b.FetchRx(&desc));
  ASSERT_EQ(desc, 1);
  ASSERT_EQ(session_b.descriptor(1)->data_length, 5);
}

TEST_F(NetworkDeviceTest, DelayedStart) {
  ASSERT_OK(CreateDevice());
  impl_.set_auto_start(false);
  auto connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  // we're dealing starting the device, so the start signal must've been triggered.
  ASSERT_OK(WaitStart());
  // But we haven't actually called the callback.
  // We should be able to pause and unpause session_a while we're still holding the device.
  // we can send Tx data and it won't reach the device until TriggerStart is called.
  session_a.ResetDescriptor(0);
  ASSERT_OK(session_a.SendTx(0));
  ASSERT_OK(session_a.SetPaused(true));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_TRUE(impl_.tx_buffers().is_empty());
  ASSERT_TRUE(impl_.TriggerStart());
  ASSERT_OK(WaitTx());
  ASSERT_FALSE(impl_.tx_buffers().is_empty());
  impl_.ReturnAllTx();

  // pause the session again and wait for stop.
  ASSERT_OK(session_a.SetPaused(true));
  ASSERT_OK(WaitStop());
  // Then unpause and re-pause the session:
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(WaitStart());
  // Pause the session once again, we haven't called TriggerStart yet.
  ASSERT_OK(session_a.SetPaused(true));

  // As soon as we call TriggerStart, stop must be called, but not before
  ASSERT_STATUS(WaitStop(zx::deadline_after(zx::msec(20))), ZX_ERR_TIMED_OUT);
  ASSERT_TRUE(impl_.TriggerStart());
  ASSERT_OK(WaitStop());
}

TEST_F(NetworkDeviceTest, DelayedStop) {
  ASSERT_OK(CreateDevice());
  impl_.set_auto_stop(false);
  auto connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(WaitStart());

  ASSERT_OK(session_a.SetPaused(true));
  ASSERT_OK(WaitStop());
  // Unpause the session again, we haven't called TriggerStop yet
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  // As soon as we call TriggerStop, start must be called, but not before
  ASSERT_STATUS(WaitStart(zx::deadline_after(zx::msec(20))), ZX_ERR_TIMED_OUT);
  ASSERT_TRUE(impl_.TriggerStop());
  ASSERT_OK(WaitStart());

  // With the session running, send down a tx frame and then close the session.
  // The session should NOT be closed until we actually call TriggerStop.
  session_a.ResetDescriptor(0);
  ASSERT_OK(session_a.SendTx(0));
  ASSERT_OK(session_a.Close());
  ASSERT_OK(WaitStop());
  // Session must not have been closed yet:
  ASSERT_EQ(session_a.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(zx::msec(20)),
                                         nullptr),
            ZX_ERR_TIMED_OUT);
  ASSERT_TRUE(impl_.TriggerStop());
  ASSERT_OK(session_a.WaitClosed(TEST_DEADLINE));
}

TEST_F(NetworkDeviceTest, ReclaimBuffers) {
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitStart());
  session_a.ResetDescriptor(0);
  session_a.ResetDescriptor(1);
  ASSERT_OK(session_a.SendRx(0));
  ASSERT_OK(session_a.SendTx(1));
  ASSERT_OK(WaitTx());
  ASSERT_OK(WaitRxAvailable());
  ASSERT_EQ(impl_.tx_buffers().size_slow(), 1);
  ASSERT_EQ(impl_.rx_buffers().size_slow(), 1);
  ASSERT_OK(session_a.SetPaused(true));
  ASSERT_OK(WaitStop());
  impl_.tx_buffers().clear();
  impl_.rx_buffers().clear();

  // check that the tx buffer was reclaimed
  uint16_t desc;
  ASSERT_OK(session_a.FetchTx(&desc));
  ASSERT_EQ(desc, 1);
  // check that the return flags reflect the error
  ASSERT_EQ(session_a.descriptor(1)->return_flags,
            static_cast<uint32_t>(netdev::TxReturnFlags::TX_RET_ERROR |
                                  netdev::TxReturnFlags::TX_RET_NOT_AVAILABLE));

  // Unpause the session again and fetch rx buffers to confirm that the Rx buffer was reclaimed
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitStart());
  ASSERT_OK(WaitRxAvailable());
  ASSERT_EQ(impl_.rx_buffers().size_slow(), 1);
}

TEST_F(NetworkDeviceTest, Teardown) {
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  TestSession session_b;
  ASSERT_OK(OpenSession(&session_b));
  ASSERT_OK(session_b.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  TestSession session_c;
  ASSERT_OK(OpenSession(&session_c));

  DiscardDeviceSync();
  session_a.WaitClosed(TEST_DEADLINE);
  session_b.WaitClosed(TEST_DEADLINE);
  session_c.WaitClosed(TEST_DEADLINE);
}

TEST_F(NetworkDeviceTest, TeardownWithReclaim) {
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitStart());
  session_a.ResetDescriptor(0);
  session_a.ResetDescriptor(1);
  ASSERT_OK(session_a.SendRx(0));
  ASSERT_OK(session_a.SendTx(1));
  ASSERT_OK(WaitTx());
  ASSERT_OK(WaitRxAvailable());
  ASSERT_EQ(impl_.tx_buffers().size_slow(), 1);
  ASSERT_EQ(impl_.rx_buffers().size_slow(), 1);

  DiscardDeviceSync();
  session_a.WaitClosed(TEST_DEADLINE);
}

TEST_F(NetworkDeviceTest, TxHeadLength) {
  constexpr uint16_t kHeadLength = 16;
  impl_.info().tx_head_length = kHeadLength;
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  session.ZeroVmo();
  auto* desc = session.ResetDescriptor(0);
  desc->head_length = kHeadLength;
  desc->data_length = 1;
  *session.buffer(desc->offset + desc->head_length) = 0xAA;
  desc = session.ResetDescriptor(1);
  desc->head_length = kHeadLength * 2;
  desc->data_length = 1;
  *session.buffer(desc->offset + desc->head_length) = 0xBB;
  uint16_t descs[] = {0, 1};
  size_t sent;
  ASSERT_OK(session.SendTx(descs, 2, &sent));
  ASSERT_EQ(sent, 2);
  ASSERT_OK(WaitTx());
  auto buffs = impl_.tx_buffers().begin();
  std::vector<uint8_t> data;

  auto vmo_provider = impl_.VmoGetter();
  // check first buffer
  ASSERT_EQ(buffs->buff().head_length, kHeadLength);
  ASSERT_OK(buffs->GetData(&data, vmo_provider));
  ASSERT_EQ(data.size(), kHeadLength + 1);
  ASSERT_EQ(data[kHeadLength], 0xAA);
  buffs++;
  // check second buffer
  ASSERT_EQ(buffs->buff().head_length, kHeadLength);
  ASSERT_OK(buffs->GetData(&data, vmo_provider));
  ASSERT_EQ(data.size(), kHeadLength + 1);
  ASSERT_EQ(data[kHeadLength], 0xBB);
  buffs++;
  ASSERT_EQ(buffs, impl_.tx_buffers().end());
}

TEST_F(NetworkDeviceTest, InvalidTxFrameType) {
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  auto* desc = session.ResetDescriptor(0);
  desc->frame_type = static_cast<uint8_t>(netdev::FrameType::IPV4);
  ASSERT_OK(session.SendTx(0));
  // Session should be killed because of contract breach:
  ASSERT_OK(session.WaitClosed(TEST_DEADLINE));
  // We should NOT have received that frame:
  ASSERT_TRUE(impl_.tx_buffers().is_empty());
}

TEST_F(NetworkDeviceTest, RxFrameTypeFilter) {
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  session.ResetDescriptor(0);
  ASSERT_OK(session.SendRx(0));
  ASSERT_OK(WaitRxAvailable());
  auto buff = impl_.rx_buffers().pop_front();
  buff->return_buffer().meta.frame_type = static_cast<uint8_t>(netdev::FrameType::IPV4);
  buff->return_buffer().total_length = 10;
  RxReturnTransaction rx_transaction(&impl_);
  rx_transaction.Enqueue(std::move(buff));
  rx_transaction.Commit();

  uint16_t ret_desc;
  ASSERT_EQ(session.FetchRx(&ret_desc), ZX_ERR_SHOULD_WAIT);
}

TEST_F(NetworkDeviceTest, ObserveStatus) {
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  zx::channel watcher, watcher_req;
  ASSERT_OK(zx::channel::create(0, &watcher, &watcher_req));
  ASSERT_TRUE(
      netdev::Device::Call::GetStatusWatcher(zx::unowned(connection), std::move(watcher_req), 3)
          .ok());
  {
    auto result = netdev::StatusWatcher::Call::WatchStatus(zx::unowned(watcher));
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.value().device_status.mtu(), impl_.status().mtu);
    ASSERT_TRUE(result.value().device_status.flags() & netdev::StatusFlags::ONLINE);
  }
  // Set offline, then set online (watcher is buffered, we should be able to observe both).
  impl_.SetOnline(false);
  impl_.SetOnline(true);
  {
    auto result = netdev::StatusWatcher::Call::WatchStatus(zx::unowned(watcher));
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.value().device_status.mtu(), impl_.status().mtu);
    ASSERT_FALSE(result.value().device_status.flags() & netdev::StatusFlags::ONLINE);
  }
  {
    auto result = netdev::StatusWatcher::Call::WatchStatus(zx::unowned(watcher));
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.value().device_status.mtu(), impl_.status().mtu);
    ASSERT_TRUE(result.value().device_status.flags() & netdev::StatusFlags::ONLINE);
  }

  DiscardDeviceSync();

  // Watcher must be closed on teardown.
  ASSERT_OK(watcher.wait_one(ZX_CHANNEL_PEER_CLOSED, TEST_DEADLINE, nullptr));
}

// Test that returning tx buffers in the body of QueueTx is allowd and works.
TEST_F(NetworkDeviceTest, ReturnTxInline) {
  impl_.set_auto_return_tx(true);
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  session.ResetDescriptor(0x02);
  ASSERT_OK(session.SendTx(0x02));
  ASSERT_OK(WaitTx());
  uint16_t desc;
  ASSERT_OK(session.FetchTx(&desc));
  EXPECT_EQ(desc, 0x02);
}

// Test that opening a session with unknown Rx types will fail.
TEST_F(NetworkDeviceTest, RejectsInvalidRxTypes) {
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session;
  auto frame_type = netdev::FrameType::IPV4;
  ASSERT_STATUS(
      OpenSession(&session, netdev::SessionFlags::PRIMARY, kDefaultDescriptorCount,
                  kDefaultBufferLength, fidl::VectorView(fidl::unowned_ptr(&frame_type), 1)),
      ZX_ERR_INVALID_ARGS);
}

// Regression test for session name not respecting fidl::StringView lack of null termination
// character.
TEST_F(NetworkDeviceTest, SessionNameRespectsStringView) {
  ASSERT_OK(CreateDevice());
  // Cast to internal implementation to access methods directly.
  auto* dev = static_cast<internal::DeviceInterface*>(device_.get());

  netdev::SessionInfo info;
  TestSession test_session;
  ASSERT_OK(test_session.Init(kDefaultDescriptorCount, kDefaultBufferLength));
  ASSERT_OK(test_session.GetInfo(&info));

  const char* name_str = "hello world";
  // String view only contains "hello".
  fidl::StringView name(fidl::unowned_ptr(name_str), 5u);

  zx::channel req, ch;
  ASSERT_OK(zx::channel::create(0, &req, &ch));

  netdev::Device_OpenSession_Response rsp;
  ASSERT_OK(dev->OpenSession(std::move(name), std::move(info), &rsp));

  const auto& session = dev->sessions_unsafe().front();

  ASSERT_STR_EQ("hello", session.name());
}

TEST_F(NetworkDeviceTest, RejectsSmallRxBuffers) {
  constexpr uint32_t kMinRxLength = 60;
  impl_.info().min_rx_buffer_length = kMinRxLength;
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  auto* desc = session.ResetDescriptor(0);
  desc->data_length = kMinRxLength - 1;
  ASSERT_OK(session.SendRx(0));
  // Session should be killed because of contract breach:
  ASSERT_OK(session.WaitClosed(TEST_DEADLINE));
  // We should NOT have received that frame:
  ASSERT_TRUE(impl_.rx_buffers().is_empty());
}

TEST_F(NetworkDeviceTest, RejectsSmallTxBuffers) {
  constexpr uint32_t kMinTxLength = 60;
  impl_.info().min_tx_buffer_length = kMinTxLength;
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  auto* desc = session.ResetDescriptor(0);
  desc->data_length = kMinTxLength - 1;
  ASSERT_OK(session.SendTx(0));
  // Session should be killed because of contract breach:
  ASSERT_OK(session.WaitClosed(TEST_DEADLINE));
  // We should NOT have received that frame:
  ASSERT_TRUE(impl_.tx_buffers().is_empty());
}

TEST_F(NetworkDeviceTest, RespectsRxThreshold) {
  constexpr uint64_t kReturnBufferSize = 1;
  ASSERT_OK(CreateDevice());
  auto connection = OpenConnection();
  TestSession session;
  uint16_t descriptor_count = impl_.info().rx_depth * 2;
  ASSERT_OK(OpenSession(&session, netdev::SessionFlags::PRIMARY, descriptor_count));

  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());

  std::vector<uint16_t> descriptors;
  descriptors.reserve(descriptor_count);
  for (uint16_t i = 0; i < descriptor_count; i++) {
    session.ResetDescriptor(i);
    descriptors.push_back(i);
  }

  // Fill up to half depth one buffer at a time, waiting for each one to be observed by the device
  // driver implementation. The slow dripping of buffers will force the Rx queue to enter
  // steady-state so we're not racing the return buffer signals with the session started and device
  // started ones.
  uint16_t half_depth = impl_.info().rx_depth / 2;
  for (uint16_t i = 0; i < half_depth; i++) {
    ASSERT_OK(session.SendRx(descriptors[i]));
    ASSERT_OK(WaitRxAvailable());
    ASSERT_EQ(impl_.rx_buffers().size_slow(), i + 1);
  }
  // Send the rest of the buffers.
  size_t actual;
  ASSERT_OK(
      session.SendRx(descriptors.data() + half_depth, descriptors.size() - half_depth, &actual));
  ASSERT_EQ(actual, descriptors.size() - half_depth);
  ASSERT_OK(WaitRxAvailable());
  ASSERT_EQ(impl_.rx_buffers().size_slow(), impl_.info().rx_depth);

  // Return the maximum number of buffers that we can return without hitting the threshold.
  for (uint16_t i = impl_.info().rx_depth - impl_.info().rx_threshold - 1; i != 0; i--) {
    RxReturnTransaction return_session(&impl_);
    return_session.EnqueueWithSize(impl_.rx_buffers().pop_front(), kReturnBufferSize);
    return_session.Commit();
    // Check that no more buffers are enqueued.
    ASSERT_STATUS(WaitRxAvailable(zx::time::infinite_past()), ZX_ERR_TIMED_OUT, "remaining=%d", i);
  }
  // Check again with some time slack for the last buffer.
  ASSERT_STATUS(WaitRxAvailable(zx::deadline_after(zx::msec(10))), ZX_ERR_TIMED_OUT);

  // Return one more buffer to cross the threshold.
  RxReturnTransaction return_session(&impl_);
  return_session.EnqueueWithSize(impl_.rx_buffers().pop_front(), kReturnBufferSize);
  return_session.Commit();
  ASSERT_OK(WaitRxAvailable());
  ASSERT_EQ(impl_.rx_buffers().size_slow(), impl_.info().rx_depth);
}

}  // namespace testing
}  // namespace network
