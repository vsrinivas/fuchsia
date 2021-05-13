// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/defer.h>
#include <lib/sync/completion.h>
#include <lib/syslog/global.h>

#include <gtest/gtest.h>

#include "device_interface.h"
#include "log.h"
#include "session.h"
#include "src/lib/testing/predicates/status.h"
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

using netdev::wire::RxFlags;

class NetworkDeviceTest : public ::testing::Test {
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

  static zx_status_t WaitEvents(const zx::event& events, zx_signals_t signals, zx::time deadline) {
    zx_status_t status = events.wait_one(signals, deadline, nullptr);
    if (status == ZX_OK) {
      events.signal(signals, 0);
    }
    return status;
  }

  [[nodiscard]] zx_status_t WaitStart(zx::time deadline = TEST_DEADLINE) {
    return WaitEvents(impl_.events(), kEventStart, deadline);
  }

  [[nodiscard]] zx_status_t WaitStop(zx::time deadline = TEST_DEADLINE) {
    return WaitEvents(impl_.events(), kEventStop, deadline);
  }

  [[nodiscard]] zx_status_t WaitSessionStarted(zx::time deadline = TEST_DEADLINE) {
    return WaitEvents(impl_.events(), kEventSessionStarted, deadline);
  }

  [[nodiscard]] zx_status_t WaitTx(zx::time deadline = TEST_DEADLINE) {
    return WaitEvents(impl_.events(), kEventTx, deadline);
  }

  [[nodiscard]] zx_status_t WaitRxAvailable(zx::time deadline = TEST_DEADLINE) {
    return WaitEvents(impl_.events(), kEventRxAvailable, deadline);
  }

  [[nodiscard]] zx_status_t WaitPortActiveChanged(const FakeNetworkPortImpl& port,
                                                  zx::time deadline = TEST_DEADLINE) {
    return WaitEvents(port.events(), kEventPortActiveChanged, deadline);
  }

  async_dispatcher_t* dispatcher() {
    if (!loop_) {
      loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNeverAttachToThread);
      EXPECT_OK(loop_->StartThread("messenger-loop", nullptr));
    }
    return loop_->dispatcher();
  }

  fidl::WireSyncClient<netdev::Device> OpenConnection() {
    zx::status endpoints = fidl::CreateEndpoints<netdev::Device>();
    EXPECT_OK(endpoints.status_value());
    auto [client_end, server_end] = std::move(*endpoints);
    EXPECT_OK(device_->Bind(std::move(server_end)));
    return fidl::BindSyncClient(std::move(client_end));
  }

  zx_status_t CreateDevice() {
    if (device_) {
      return ZX_ERR_INTERNAL;
    }
    zx::status device = impl_.CreateChild(dispatcher());
    if (device.is_ok()) {
      device_ = std::move(device.value());
    }
    return device.status_value();
  }

  zx_status_t OpenSession(TestSession* session,
                          netdev::wire::SessionFlags flags = netdev::wire::SessionFlags::kPrimary,
                          uint16_t num_descriptors = kDefaultDescriptorCount,
                          uint64_t buffer_size = kDefaultBufferLength,
                          fidl::VectorView<netdev::wire::FrameType> frame_types =
                              fidl::VectorView<netdev::wire::FrameType>()) {
    // automatically increment to test_session_(a, b, c, etc...)
    char session_name[] = "test_session_a";
    session_name[strlen(session_name) - 1] =
        static_cast<char>('a' + (session_counter_ % ('z' - 'a')));
    session_counter_++;

    fidl::WireSyncClient connection = OpenConnection();
    return session->Open(connection, session_name, flags, num_descriptors, buffer_size,
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

enum class RxTxSwitch {
  Rx,
  Tx,
};

const char* rxTxSwitchToString(RxTxSwitch rxtx) {
  switch (rxtx) {
    case RxTxSwitch::Tx:
      return "Tx";
    case RxTxSwitch::Rx:
      return "Rx";
  }
}

RxTxSwitch flipRxTxSwitch(RxTxSwitch rxtx) {
  switch (rxtx) {
    case RxTxSwitch::Tx:
      return RxTxSwitch::Rx;
    case RxTxSwitch::Rx:
      return RxTxSwitch::Tx;
  }
}

const std::string rxTxParamTestToString(const ::testing::TestParamInfo<RxTxSwitch>& info) {
  return rxTxSwitchToString(info.param);
}

// Helper class to instantiate test suites that have an Rx and Tx variant.
class RxTxParamTest : public NetworkDeviceTest, public ::testing::WithParamInterface<RxTxSwitch> {};

TEST_F(NetworkDeviceTest, CanCreate) { ASSERT_OK(CreateDevice()); }

TEST_F(NetworkDeviceTest, GetInfo) {
  impl_.info().min_rx_buffer_length = 2048;
  impl_.info().min_tx_buffer_length = 60;
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  fidl::WireResult rsp = connection.GetInfo();
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
  EXPECT_EQ(info.min_descriptor_length, sizeof(buffer_descriptor_t) / sizeof(uint64_t));
  EXPECT_EQ(info.class_, netdev::wire::DeviceClass::kEthernet);
  EXPECT_EQ(info.tx_accel.count(), impl_.info().tx_accel_count);
  EXPECT_EQ(info.rx_accel.count(), impl_.info().rx_accel_count);

  const auto& port_info = impl_.port0().port_info();
  EXPECT_EQ(info.rx_types.count(), port_info.rx_types_count);
  for (size_t i = 0; i < info.rx_types.count(); i++) {
    EXPECT_EQ(static_cast<uint8_t>(info.rx_types.at(i)), port_info.rx_types_list[i]);
  }
  EXPECT_EQ(info.tx_types.count(), port_info.tx_types_count);
  for (size_t i = 0; i < info.tx_types.count(); i++) {
    EXPECT_EQ(static_cast<uint8_t>(info.tx_types.at(i).type), port_info.tx_types_list[i].type);
    EXPECT_EQ(info.tx_types.at(i).features, port_info.tx_types_list[i].features);
    EXPECT_EQ(static_cast<uint32_t>(info.tx_types.at(i).supported_flags),
              port_info.tx_types_list[i].supported_flags);
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
  fidl::WireSyncClient connection = OpenConnection();
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
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  session.SetPaused(false);
  ASSERT_OK(WaitStart());

  constexpr uint16_t kDescriptor0 = 0;
  constexpr uint16_t kDescriptor1 = 1;
  constexpr uint16_t kDescriptor2 = 2;

  constexpr struct {
    uint16_t space_head;
    uint16_t space_tail;
    uint16_t descriptor;
    uint32_t offset;
    uint32_t length;
    bool chain;
    std::optional<RxFlags> flags;
  } kDescriptorSetup[] = {{
                              .descriptor = kDescriptor0,
                              .length = 64,
                              .chain = false,
                              .flags = RxFlags::kRxAccel0,
                          },
                          {
                              .space_head = 16,
                              .descriptor = kDescriptor1,
                              .length = 15,
                              .chain = true,
                              .flags = RxFlags::kRxAccel1,
                          },
                          {
                              .space_tail = 32,
                              .descriptor = kDescriptor2,
                              .offset = 64,
                              .length = 8,
                              .chain = true,
                          }};
  for (const auto& setup : kDescriptorSetup) {
    buffer_descriptor_t* desc = session.ResetDescriptor(setup.descriptor);
    desc->head_length = setup.space_head;
    desc->tail_length = setup.space_tail;
    desc->data_length -= setup.space_head + setup.space_tail;
  }

  uint16_t all_descs[std::size(kDescriptorSetup)] = {kDescriptor0, kDescriptor1, kDescriptor2};
  size_t sent;
  ASSERT_OK(session.SendRx(all_descs, std::size(all_descs), &sent));
  ASSERT_EQ(sent, std::size(kDescriptorSetup));
  ASSERT_OK(WaitRxAvailable());

  // Get the expected VMO ID for all buffers.
  std::optional first_vmo = impl_.first_vmo_id();
  ASSERT_TRUE(first_vmo.has_value());
  uint8_t want_vmo = first_vmo.value();

  RxReturnTransaction return_session(&impl_);

  // Prepare a chained return.
  auto chained_return = std::make_unique<RxReturn>();
  fbl::DoublyLinkedList buffers = impl_.TakeRxBuffers();
  for (const auto& descriptor_setup : kDescriptorSetup) {
    SCOPED_TRACE(descriptor_setup.descriptor);
    // Load the buffers from the fake device implementation and check them.
    // We call "pop_back" on the buffer list because network_device feeds Rx buffers in a LIFO
    // order.
    std::unique_ptr rx = buffers.pop_back();
    ASSERT_TRUE(rx);
    const rx_space_buffer_t& space = rx->space();
    ASSERT_EQ(space.vmo, want_vmo);
    buffer_descriptor_t* descriptor = session.descriptor(descriptor_setup.descriptor);
    ASSERT_EQ(space.region.offset, descriptor->offset + descriptor->head_length);
    ASSERT_EQ(space.region.length, descriptor->data_length + descriptor->tail_length);

    rx->return_part().offset = descriptor_setup.offset;
    rx->return_part().length = descriptor_setup.length;
    if (descriptor_setup.chain) {
      if (descriptor_setup.flags.has_value()) {
        chained_return->buffer().meta.flags = static_cast<uint32_t>(*descriptor_setup.flags);
      }
      chained_return->PushPart(std::move(rx));
    } else {
      std::unique_ptr ret = std::make_unique<RxReturn>(std::move(rx));
      if (descriptor_setup.flags.has_value()) {
        ret->buffer().meta.flags = static_cast<uint32_t>(*descriptor_setup.flags);
      }
      return_session.Enqueue(std::move(ret));
    }
  }
  chained_return->buffer().meta.flags = static_cast<uint32_t>(RxFlags::kRxAccel1);
  return_session.Enqueue(std::move(chained_return));
  // Ensure no more rx buffers were actually returned:
  ASSERT_TRUE(buffers.is_empty());
  // Commit the returned buffers.
  return_session.Commit();
  // Check that all descriptors were returned to the queue:
  size_t read_back;
  ASSERT_OK(session.FetchRx(all_descs, std::size(all_descs), &read_back));
  // We chained descriptors 2 descriptors together, so we should observe one less than the number of
  // descriptors returned.
  ASSERT_EQ(read_back, std::size(kDescriptorSetup) - 1);
  EXPECT_EQ(all_descs[0], kDescriptor0);
  EXPECT_EQ(all_descs[1], kDescriptor1);
  // Finally check all the stuff that was returned.
  for (const auto& setup : kDescriptorSetup) {
    SCOPED_TRACE(setup.descriptor);
    buffer_descriptor_t* desc = session.descriptor(setup.descriptor);
    EXPECT_EQ(desc->port_id, 0);
    EXPECT_EQ(desc->offset, session.canonical_offset(setup.descriptor));
    if (setup.descriptor == kDescriptor1) {
      // This descriptor should have a chain.
      EXPECT_EQ(desc->chain_length, 1u);
      EXPECT_EQ(desc->nxt, kDescriptor2);
    } else {
      EXPECT_EQ(desc->chain_length, 0u);
    }
    if (setup.flags.has_value()) {
      EXPECT_EQ(desc->inbound_flags, static_cast<uint32_t>(*setup.flags));
    }
    EXPECT_EQ(desc->head_length, setup.offset);
    EXPECT_EQ(desc->data_length, setup.length);
    EXPECT_EQ(desc->tail_length, kDefaultBufferLength - setup.length - setup.offset);
  }
}

TEST_F(NetworkDeviceTest, TxBufferBuild) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
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
  auto tx = impl_.PopTxBuffer();
  ASSERT_TRUE(tx);
  ASSERT_EQ(tx->buffer().data_count, 1u);
  ASSERT_EQ(tx->buffer().data_list[0].offset, session.descriptor(0)->offset);
  ASSERT_EQ(tx->buffer().data_list[0].length, kDefaultBufferLength);
  return_session.Enqueue(std::move(tx));
  // check second descriptor:
  tx = impl_.PopTxBuffer();
  ASSERT_TRUE(tx);
  ASSERT_EQ(tx->buffer().data_count, 1u);
  desc = session.descriptor(1);
  ASSERT_EQ(tx->buffer().data_list[0].offset, desc->offset + desc->head_length);
  ASSERT_EQ(tx->buffer().data_list[0].length,
            kDefaultBufferLength - desc->head_length - desc->tail_length);
  tx->set_status(ZX_ERR_UNAVAILABLE);
  return_session.Enqueue(std::move(tx));
  // check third descriptor:
  tx = impl_.PopTxBuffer();
  ASSERT_TRUE(tx);
  ASSERT_EQ(tx->buffer().data_count, 3u);
  {
    uint16_t descriptor = 2;
    for (const buffer_region_t& region :
         fbl::Span(tx->buffer().data_list, tx->buffer().data_count)) {
      SCOPED_TRACE(descriptor);
      buffer_descriptor_t* d = session.descriptor(descriptor++);
      ASSERT_EQ(region.offset, d->offset);
      ASSERT_EQ(region.length, d->data_length);
    }
  }
  tx->set_status(ZX_ERR_NOT_SUPPORTED);
  return_session.Enqueue(std::move(tx));
  // ensure no more tx buffers were actually enqueued:
  ASSERT_FALSE(impl_.PopTxBuffer());
  // commit the returned buffers
  return_session.Commit();
  // check that all descriptors were returned to the queue:
  size_t read_back;

  ASSERT_OK(session.FetchTx(all_descs, kDescTests + 1, &read_back));
  ASSERT_EQ(read_back, kDescTests);
  EXPECT_EQ(all_descs[0], 0u);
  EXPECT_EQ(all_descs[1], 1u);
  EXPECT_EQ(all_descs[2], 2u);
  // check the status of the returned descriptors
  desc = session.descriptor(0);
  EXPECT_EQ(desc->return_flags, 0u);
  desc = session.descriptor(1);
  EXPECT_EQ(desc->return_flags,
            static_cast<uint32_t>(netdev::wire::TxReturnFlags::kTxRetError |
                                  netdev::wire::TxReturnFlags::kTxRetNotAvailable));
  desc = session.descriptor(2);
  EXPECT_EQ(desc->return_flags,
            static_cast<uint32_t>(netdev::wire::TxReturnFlags::kTxRetError |
                                  netdev::wire::TxReturnFlags::kTxRetNotSupported));
}

TEST_F(NetworkDeviceTest, SessionEpitaph) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  ASSERT_OK(session.Close());
  // closing the session should cause a stop:
  ASSERT_OK(WaitStop());
  // wait for epitaph to show up in channel
  ASSERT_OK(session.session().channel().wait_one(ZX_CHANNEL_READABLE, TEST_DEADLINE, nullptr));
  fidl_epitaph_t epitaph;
  uint32_t actual_bytes;
  ASSERT_OK(session.session().channel().read(0, &epitaph, nullptr, sizeof(epitaph), 0,
                                             &actual_bytes, nullptr));
  ASSERT_EQ(actual_bytes, sizeof(epitaph));
  ASSERT_EQ(epitaph.error, ZX_ERR_CANCELED);
  // also the channel must be closed after:
  ASSERT_OK(session.session().channel().wait_one(ZX_CHANNEL_PEER_CLOSED, TEST_DEADLINE, nullptr));
}

TEST_F(NetworkDeviceTest, SessionPauseUnpause) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
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
  fidl::WireSyncClient connection = OpenConnection();
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
  std::unique_ptr buff_a = impl_.PopTxBuffer();
  std::unique_ptr buff_b = impl_.PopTxBuffer();
  VmoProvider vmo_provider = impl_.VmoGetter();
  zx::status data_status_a = buff_a->GetData(vmo_provider);
  ASSERT_OK(data_status_a.status_value());
  std::vector data_a = std::move(data_status_a.value());

  zx::status data_status_b = buff_b->GetData(vmo_provider);
  ASSERT_OK(data_status_b.status_value());
  std::vector data_b = std::move(data_status_b.value());
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
  ASSERT_EQ(rd, 0u);
  ASSERT_OK(session_b.FetchTx(&rd));
  ASSERT_EQ(rd, 1u);
  ASSERT_EQ(session_a.descriptor(0)->return_flags, 0u);
  ASSERT_EQ(session_b.descriptor(1)->return_flags,
            static_cast<uint32_t>(netdev::wire::TxReturnFlags::kTxRetError |
                                  netdev::wire::TxReturnFlags::kTxRetNotAvailable));
}

TEST_F(NetworkDeviceTest, TwoSessionsRx) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
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
    auto buff = impl_.PopRxBuffer();
    std::vector<uint8_t> data(kDataLen, static_cast<uint8_t>(i));
    ASSERT_OK(buff->WriteData(data, vmo_provider));
    return_session.Enqueue(std::move(buff));
  }
  return_session.Commit();

  auto checker = [kBufferCount, kDataLen](TestSession& session) {
    uint16_t descriptors[kBufferCount];
    size_t rd;
    ASSERT_OK(session.FetchRx(descriptors, kBufferCount, &rd));
    ASSERT_EQ(rd, kBufferCount);
    for (uint32_t i = 0; i < kBufferCount; i++) {
      auto* desc = session.descriptor(descriptors[i]);
      ASSERT_EQ(desc->data_length, kDataLen);
      auto* data = session.buffer(desc->offset);
      for (uint32_t j = 0; j < kDataLen; j++) {
        ASSERT_EQ(*data, static_cast<uint8_t>(i));
        data++;
      }
    }
  };
  {
    SCOPED_TRACE("session_a");
    checker(session_a);
  }
  {
    SCOPED_TRACE("session_b");
    checker(session_b);
  }
}

TEST_F(NetworkDeviceTest, ListenSession) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  TestSession session_b;
  ASSERT_OK(OpenSession(&session_b, netdev::wire::SessionFlags::kListenTx));
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
  ASSERT_EQ(desc_idx, 0u);
  auto* desc = session_b.descriptor(0);
  ASSERT_EQ(desc->data_length, send_buff.size());
  auto* data = session_b.buffer(desc->offset);
  ASSERT_EQ(std::basic_string_view(data, send_buff.size()),
            std::basic_string_view(send_buff.data(), send_buff.size()));
}

TEST_F(NetworkDeviceTest, ClosingPrimarySession) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  TestSession session_b;
  ASSERT_OK(OpenSession(&session_b));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(session_b.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  buffer_descriptor_t* d = session_a.ResetDescriptor(0);
  d->data_length = kDefaultBufferLength / 2;
  session_b.ResetDescriptor(1);
  ASSERT_OK(session_a.SendRx(0));
  ASSERT_OK(WaitRxAvailable());
  // Implementation now owns session a's RxBuffer.
  std::unique_ptr rx_buff = impl_.PopRxBuffer();
  ASSERT_EQ(rx_buff->space().region.length, kDefaultBufferLength / 2);
  // Let's close session_a, it should not be closed until we return the buffers.
  ASSERT_OK(session_a.Close());
  ASSERT_EQ(session_a.session().channel().wait_one(ZX_CHANNEL_PEER_CLOSED,
                                                   zx::deadline_after(zx::msec(20)), nullptr),
            ZX_ERR_TIMED_OUT);
  // Session B should've now become primary. Provide enough buffers to fill the device queues.
  uint16_t target_descriptor = 0;
  while (impl_.rx_buffer_count() < impl_.info().rx_depth - 1) {
    session_b.ResetDescriptor(target_descriptor);
    ASSERT_OK(session_b.SendRx(target_descriptor++));
    ASSERT_OK(WaitRxAvailable());
  }
  // Send one more descriptor that will receive the copied data form the old buffer in Session A.
  session_b.ResetDescriptor(target_descriptor);
  ASSERT_OK(session_b.SendRx(target_descriptor));

  // And now return data.
  constexpr uint32_t kReturnLength = 5;
  rx_buff->SetReturnLength(kReturnLength);
  RxReturnTransaction rx_transaction(&impl_);
  rx_transaction.Enqueue(std::move(rx_buff));
  rx_transaction.Commit();

  // Session a should be closed...
  ASSERT_OK(session_a.WaitClosed(TEST_DEADLINE));
  /// ...and Session b should still receive the data.
  uint16_t desc;
  ASSERT_OK(session_b.FetchRx(&desc));
  ASSERT_EQ(desc, target_descriptor);
  ASSERT_EQ(session_b.descriptor(desc)->data_length, kReturnLength);
}

TEST_F(NetworkDeviceTest, DelayedStart) {
  ASSERT_OK(CreateDevice());
  impl_.set_auto_start(false);
  fidl::WireSyncClient connection = OpenConnection();
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
  ASSERT_FALSE(impl_.PopRxBuffer());
  ASSERT_TRUE(impl_.TriggerStart());
  ASSERT_OK(WaitTx());
  std::unique_ptr tx_buffer = impl_.PopTxBuffer();
  ASSERT_TRUE(tx_buffer);
  TxReturnTransaction transaction(&impl_);
  transaction.Enqueue(std::move(tx_buffer));
  transaction.Commit();

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
  fidl::WireSyncClient connection = OpenConnection();
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

  // With the session running, send down a tx frame and then close the session. The session should
  // NOT be closed until we actually both call TriggerStop and return the outstanding buffer.
  session_a.ResetDescriptor(0);
  ASSERT_OK(session_a.SendTx(0));
  ASSERT_OK(WaitTx());
  ASSERT_OK(session_a.Close());
  ASSERT_OK(WaitStop());
  // Session must not have been closed yet.
  ASSERT_EQ(session_a.session().channel().wait_one(ZX_CHANNEL_PEER_CLOSED,
                                                   zx::deadline_after(zx::msec(20)), nullptr),
            ZX_ERR_TIMED_OUT);
  ASSERT_TRUE(impl_.TriggerStop());

  // Session must not have been closed yet.
  ASSERT_EQ(session_a.session().channel().wait_one(ZX_CHANNEL_PEER_CLOSED,
                                                   zx::deadline_after(zx::msec(20)), nullptr),
            ZX_ERR_TIMED_OUT);

  // Return the outstanding buffer.
  std::unique_ptr buffer = impl_.PopTxBuffer();
  TxReturnTransaction transaction(&impl_);
  transaction.Enqueue(std::move(buffer));
  transaction.Commit();
  // Now session should close.
  ASSERT_OK(session_a.WaitClosed(TEST_DEADLINE));
}

TEST_P(RxTxParamTest, WaitsForAllBuffersReturned) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  session.ResetDescriptor(0);
  session.ResetDescriptor(1);
  ASSERT_OK(session.SendRx(0));
  ASSERT_OK(session.SendTx(1));
  ASSERT_OK(WaitTx());
  ASSERT_OK(WaitRxAvailable());

  fbl::DoublyLinkedList rx_buffers = impl_.TakeRxBuffers();
  ASSERT_EQ(rx_buffers.size_slow(), 1u);
  fbl::DoublyLinkedList tx_buffers = impl_.TakeTxBuffers();
  ASSERT_EQ(tx_buffers.size_slow(), 1u);

  ASSERT_OK(session.Close());
  ASSERT_OK(WaitStop());

  // Session will not close until we return the buffers we're holding.
  ASSERT_STATUS(session.WaitClosed(zx::deadline_after(zx::msec(10))), ZX_ERR_TIMED_OUT);

  // Test parameter controls which buffers we'll return first.
  auto return_buffer = [this, &tx_buffers, &rx_buffers](RxTxSwitch which) {
    switch (which) {
      case RxTxSwitch::Tx: {
        TxReturnTransaction transaction(&impl_);
        std::unique_ptr buffer = tx_buffers.pop_front();
        buffer->set_status(ZX_ERR_UNAVAILABLE);
        transaction.Enqueue(std::move(buffer));
        transaction.Commit();
      } break;
      case RxTxSwitch::Rx: {
        RxReturnTransaction transaction(&impl_);
        std::unique_ptr buffer = rx_buffers.pop_front();
        buffer->return_part().length = 0;
        transaction.Enqueue(std::move(buffer));
        transaction.Commit();
      } break;
    }
  };

  return_buffer(GetParam());
  ASSERT_STATUS(session.WaitClosed(zx::deadline_after(zx::msec(10))), ZX_ERR_TIMED_OUT);
  return_buffer(flipRxTxSwitch(GetParam()));
  ASSERT_OK(session.WaitClosed(TEST_DEADLINE));
}

TEST_F(NetworkDeviceTest, Teardown) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
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
  fidl::WireSyncClient connection = OpenConnection();
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
  ASSERT_EQ(impl_.rx_buffer_count(), 1u);
  ASSERT_EQ(impl_.tx_buffer_count(), 1u);

  DiscardDeviceSync();
  session_a.WaitClosed(TEST_DEADLINE);
}

TEST_F(NetworkDeviceTest, TxHeadLength) {
  constexpr uint16_t kHeadLength = 16;
  impl_.info().tx_head_length = kHeadLength;
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
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
  ASSERT_EQ(sent, 2u);
  ASSERT_OK(WaitTx());

  auto vmo_provider = impl_.VmoGetter();
  TxReturnTransaction transaction(&impl_);
  constexpr struct {
    uint8_t expect;
    const char* name;
  } kCheckTable[] = {
      {
          .expect = 0xAA,
          .name = "first buffer",
      },
      {
          .expect = 0xBB,
          .name = "second buffer",
      },
  };
  for (const auto& check : kCheckTable) {
    SCOPED_TRACE(check.name);
    std::unique_ptr buffer = impl_.PopTxBuffer();
    ASSERT_TRUE(buffer);
    ASSERT_EQ(buffer->buffer().head_length, kHeadLength);
    zx::status status = buffer->GetData(vmo_provider);
    ASSERT_OK(status.status_value());
    std::vector<uint8_t>& data = status.value();
    ASSERT_EQ(data.size(), kHeadLength + 1u);
    ASSERT_EQ(data[kHeadLength], check.expect);
    transaction.Enqueue(std::move(buffer));
  }
  transaction.Commit();
}

TEST_F(NetworkDeviceTest, InvalidTxFrameType) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  auto* desc = session.ResetDescriptor(0);
  desc->frame_type = static_cast<uint8_t>(netdev::wire::FrameType::kIpv4);
  ASSERT_OK(session.SendTx(0));
  // Session should be killed because of contract breach:
  ASSERT_OK(session.WaitClosed(TEST_DEADLINE));
  // We should NOT have received that frame:
  ASSERT_FALSE(impl_.PopTxBuffer());
}

TEST_F(NetworkDeviceTest, RxFrameTypeFilter) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  session.ResetDescriptor(0);
  ASSERT_OK(session.SendRx(0));
  ASSERT_OK(WaitRxAvailable());
  std::unique_ptr buff = impl_.PopRxBuffer();
  buff->SetReturnLength(10);
  std::unique_ptr ret = std::make_unique<RxReturn>(std::move(buff));
  ret->buffer().meta.frame_type = static_cast<uint8_t>(netdev::wire::FrameType::kIpv4);
  RxReturnTransaction rx_transaction(&impl_);
  rx_transaction.Enqueue(std::move(ret));
  rx_transaction.Commit();

  uint16_t ret_desc;
  ASSERT_EQ(session.FetchRx(&ret_desc), ZX_ERR_SHOULD_WAIT);
}

TEST_F(NetworkDeviceTest, ObserveStatus) {
  using netdev::wire::StatusFlags;
  ASSERT_OK(CreateDevice());
  zx::status endpoints = fidl::CreateEndpoints<netdev::StatusWatcher>();
  ASSERT_OK(endpoints.status_value());
  auto [client_end, server_end] = std::move(*endpoints);
  fidl::WireSyncClient watcher = fidl::BindSyncClient(std::move(client_end));
  ASSERT_OK(OpenConnection().GetStatusWatcher(std::move(server_end), 3).status());
  {
    fidl::WireResult result = watcher.WatchStatus();
    ASSERT_OK(result.status());
    ASSERT_EQ(result.value().device_status.mtu(), impl_.port0().status().mtu);
    ASSERT_TRUE(result.value().device_status.flags() & StatusFlags::kOnline);
  }
  // Set offline, then set online (watcher is buffered, we should be able to observe both).
  impl_.SetOnline(false);
  impl_.SetOnline(true);
  {
    fidl::WireResult result = watcher.WatchStatus();
    ASSERT_OK(result.status());
    ASSERT_EQ(result.value().device_status.mtu(), impl_.port0().status().mtu);
    ASSERT_FALSE(result.value().device_status.flags() & StatusFlags::kOnline);
  }
  {
    fidl::WireResult result = watcher.WatchStatus();
    ASSERT_OK(result.status());
    ASSERT_EQ(result.value().device_status.mtu(), impl_.port0().status().mtu);
    ASSERT_TRUE(result.value().device_status.flags() & StatusFlags::kOnline);
  }

  DiscardDeviceSync();

  // Watcher must be closed on teardown.
  ASSERT_OK(watcher.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, TEST_DEADLINE, nullptr));
}

// Test that returning tx buffers in the body of QueueTx is allowed and works.
TEST_F(NetworkDeviceTest, ReturnTxInline) {
  impl_.set_immediate_return_tx(true);
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  session.ResetDescriptor(0x02);
  ASSERT_OK(session.SendTx(0x02));
  ASSERT_OK(session.tx_fifo().wait_one(ZX_FIFO_READABLE, TEST_DEADLINE, nullptr));
  uint16_t desc;
  ASSERT_OK(session.FetchTx(&desc));
  EXPECT_EQ(desc, 0x02);
}

// Test that opening a session with unknown Rx types will fail.
TEST_F(NetworkDeviceTest, RejectsInvalidRxTypes) {
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  auto frame_type = netdev::wire::FrameType::kIpv4;
  ASSERT_STATUS(
      OpenSession(&session, netdev::wire::SessionFlags::kPrimary, kDefaultDescriptorCount,
                  kDefaultBufferLength,
                  fidl::VectorView<netdev::wire::FrameType>::FromExternal(&frame_type, 1)),
      ZX_ERR_INVALID_ARGS);
}

// Regression test for session name not respecting fidl::StringView lack of null termination
// character.
TEST_F(NetworkDeviceTest, SessionNameRespectsStringView) {
  ASSERT_OK(CreateDevice());
  // Cast to internal implementation to access methods directly.
  auto* dev = static_cast<internal::DeviceInterface*>(device_.get());

  netdev::wire::SessionInfo info;
  TestSession test_session;
  ASSERT_OK(test_session.Init(kDefaultDescriptorCount, kDefaultBufferLength));
  ASSERT_OK(test_session.GetInfo(&info));

  const char* name_str = "hello world";
  // String view only contains "hello".
  fidl::StringView name = fidl::StringView::FromExternal(name_str, 5u);

  zx::status response = dev->OpenSession(std::move(name), std::move(info));
  ASSERT_OK(response.status_value());

  const auto& session = dev->sessions_unsafe().front();

  ASSERT_STREQ("hello", session.name());
}

TEST_F(NetworkDeviceTest, RejectsSmallRxBuffers) {
  constexpr uint32_t kMinRxLength = 60;
  impl_.info().min_rx_buffer_length = kMinRxLength;
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
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
  ASSERT_FALSE(impl_.PopRxBuffer());
}

TEST_F(NetworkDeviceTest, RejectsSmallTxBuffers) {
  constexpr uint32_t kMinTxLength = 60;
  impl_.info().min_tx_buffer_length = kMinTxLength;
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
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
  ASSERT_FALSE(impl_.PopTxBuffer());
}

TEST_F(NetworkDeviceTest, RespectsRxThreshold) {
  constexpr uint64_t kReturnBufferSize = 1;
  ASSERT_OK(CreateDevice());
  fidl::WireSyncClient connection = OpenConnection();
  TestSession session;
  uint16_t descriptor_count = impl_.info().rx_depth * 2;
  ASSERT_OK(OpenSession(&session, netdev::wire::SessionFlags::kPrimary, descriptor_count));

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
  // steady-state so we're not racing the return buffer signals with the session started and
  // device started ones.
  uint16_t half_depth = impl_.info().rx_depth / 2;
  for (uint16_t i = 0; i < half_depth; i++) {
    ASSERT_OK(session.SendRx(descriptors[i]));
    ASSERT_OK(WaitRxAvailable());
    ASSERT_EQ(impl_.rx_buffer_count(), i + 1u);
  }
  // Send the rest of the buffers.
  size_t actual;
  ASSERT_OK(
      session.SendRx(descriptors.data() + half_depth, descriptors.size() - half_depth, &actual));
  ASSERT_EQ(actual, descriptors.size() - half_depth);
  ASSERT_OK(WaitRxAvailable());
  ASSERT_EQ(impl_.rx_buffer_count(), impl_.info().rx_depth);

  // Return the maximum number of buffers that we can return without hitting the threshold.
  for (uint16_t i = impl_.info().rx_depth - impl_.info().rx_threshold - 1; i != 0; i--) {
    RxReturnTransaction return_session(&impl_);
    std::unique_ptr buff = impl_.PopRxBuffer();
    buff->SetReturnLength(kReturnBufferSize);
    return_session.Enqueue(std::move(buff));
    return_session.Commit();
    // Check that no more buffers are enqueued.
    ASSERT_STATUS(WaitRxAvailable(zx::time::infinite_past()), ZX_ERR_TIMED_OUT)
        << "remaining=" << i;
  }
  // Check again with some time slack for the last buffer.
  ASSERT_STATUS(WaitRxAvailable(zx::deadline_after(zx::msec(10))), ZX_ERR_TIMED_OUT);

  // Return one more buffer to cross the threshold.
  RxReturnTransaction return_session(&impl_);
  std::unique_ptr buff = impl_.PopRxBuffer();
  buff->SetReturnLength(kReturnBufferSize);
  return_session.Enqueue(std::move(buff));
  return_session.Commit();
  ASSERT_OK(WaitRxAvailable());
  ASSERT_EQ(impl_.rx_buffer_count(), impl_.info().rx_depth);
}

TEST_F(NetworkDeviceTest, RxQueueIdlesOnPausedSession) {
  ASSERT_OK(CreateDevice());

  struct {
    fbl::Mutex lock;
    std::optional<uint64_t> key __TA_GUARDED(lock);
  } observed_key;

  sync_completion_t completion;

  auto get_next_key = [&observed_key, &completion](zx::duration timeout) -> zx::status<uint64_t> {
    zx_status_t status = sync_completion_wait(&completion, timeout.get());
    fbl::AutoLock l(&observed_key.lock);
    std::optional k = observed_key.key;
    if (status != ZX_OK) {
      // Whenever wait fails, key must not have a value.
      EXPECT_EQ(k, std::nullopt);
      return zx::error(status);
    }
    sync_completion_reset(&completion);
    if (!k.has_value()) {
      return zx::error(ZX_ERR_BAD_STATE);
    }
    uint64_t key = *k;
    observed_key.key.reset();
    return zx::ok(key);
  };

  auto* dev_iface = static_cast<internal::DeviceInterface*>(device_.get());
  dev_iface->evt_rx_queue_packet = [&observed_key, &completion](uint64_t key) {
    fbl::AutoLock l(&observed_key.lock);
    std::optional k = observed_key.key;
    EXPECT_EQ(k, std::nullopt);
    observed_key.key = key;
    sync_completion_signal(&completion);
  };
  auto undo = fit::defer([dev_iface]() {
    // Clear event handler so we don't see any of the teardown.
    dev_iface->evt_rx_queue_packet = nullptr;
  });

  TestSession session;
  ASSERT_OK(OpenSession(&session));

  {
    zx::status key = get_next_key(zx::duration::infinite());
    ASSERT_OK(key.status_value());
    ASSERT_EQ(key.value(), internal::RxQueue::kSessionSwitchKey);
  }

  session.ResetDescriptor(0);
  // Make the FIFO readable.
  ASSERT_OK(session.SendRx(0));
  // It should not trigger any RxQueue events.
  {
    zx::status key = get_next_key(zx::msec(50));
    ASSERT_TRUE(key.is_error()) << "unexpected key value " << key.value();
    ASSERT_STATUS(key.status_value(), ZX_ERR_TIMED_OUT);
  }

  // Kill the session and check that we see a session switch again.
  ASSERT_OK(session.Close());
  {
    zx::status key = get_next_key(zx::duration::infinite());
    ASSERT_OK(key.status_value());
    ASSERT_EQ(key.value(), internal::RxQueue::kSessionSwitchKey);
  }
}

TEST_F(NetworkDeviceTest, RemovingPortCausesSessionToPause) {
  ASSERT_OK(CreateDevice());
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());

  // Removing the port causes the session to pause, which should cause the data plane to stop.
  impl_.client().RemovePort(FakeNetworkDeviceImpl::kPort0);
  ASSERT_OK(WaitStop());
}

TEST_F(NetworkDeviceTest, OnlyReceiveOnSubscribedPorts) {
  ASSERT_OK(CreateDevice());
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  std::array<uint16_t, 2> descriptors = {0, 1};

  for (auto desc : descriptors) {
    auto* descriptor = session.ResetDescriptor(desc);
    // Garble descriptor port.
    descriptor->port_id = MAX_PORTS - 1;
  }
  size_t actual;
  ASSERT_OK(session.SendRx(descriptors.data(), descriptors.size(), &actual));
  ASSERT_EQ(actual, descriptors.size());
  ASSERT_OK(WaitRxAvailable());
  ASSERT_EQ(impl_.rx_buffer_count(), descriptors.size());
  RxReturnTransaction return_session(&impl_);
  for (size_t i = 0; i < descriptors.size(); i++) {
    std::unique_ptr rx_space = impl_.PopRxBuffer();
    uint8_t port_id = static_cast<uint8_t>(i);
    // Write some data so the buffer makes it into the session.
    ASSERT_OK(rx_space->WriteData(fbl::Span(&port_id, sizeof(port_id)), impl_.VmoGetter()));
    std::unique_ptr ret = std::make_unique<RxReturn>(std::move(rx_space));
    // Set the port ID to the index, we should expect the session to only see port 0.
    ret->buffer().meta.port = port_id;
    return_session.Enqueue(std::move(ret));
  }
  return_session.Commit();
  ASSERT_OK(session.FetchRx(descriptors.data(), descriptors.size(), &actual));
  // Only one of the descriptors makes it back into the session.
  ASSERT_EQ(actual, 1u);
  uint16_t returned = descriptors[0];
  ASSERT_EQ(session.descriptor(returned)->port_id, FakeNetworkDeviceImpl::kPort0);

  // The unused descriptor comes right back to us.
  ASSERT_OK(WaitRxAvailable());
  ASSERT_EQ(impl_.rx_buffer_count(), 1u);
}

TEST_F(NetworkDeviceTest, SessionsAttachToPort) {
  ASSERT_OK(CreateDevice());
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  auto& port0 = impl_.port0();
  // Just opening a session doesn't attach to port 0.
  ASSERT_STATUS(WaitPortActiveChanged(port0, zx::deadline_after(zx::msec(20))), ZX_ERR_TIMED_OUT);
  ASSERT_FALSE(port0.active());

  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitPortActiveChanged(port0));
  ASSERT_TRUE(port0.active());

  ASSERT_OK(session.SetPaused(true));
  ASSERT_OK(WaitPortActiveChanged(port0));
  ASSERT_FALSE(port0.active());

  // Unpause the session once again, then observe that session detaches on destruction.
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitPortActiveChanged(port0));
  ASSERT_TRUE(port0.active());

  ASSERT_OK(session.Close());
  ASSERT_OK(WaitPortActiveChanged(port0));
  ASSERT_FALSE(port0.active());
}

TEST_F(NetworkDeviceTest, RejectsInvalidPortIds) {
  ASSERT_OK(CreateDevice());
  {
    // Add a port with an invalid ID.
    FakeNetworkPortImpl fake_port;
    network_port_protocol_t proto = fake_port.protocol();
    impl_.client().AddPort(MAX_PORTS, proto.ctx, proto.ops);
    ASSERT_TRUE(fake_port.removed());
  }

  {
    // Add a port with a duplicate ID.
    FakeNetworkPortImpl fake_port;
    network_port_protocol_t proto = fake_port.protocol();
    impl_.client().AddPort(FakeNetworkDeviceImpl::kPort0, proto.ctx, proto.ops);
    ASSERT_TRUE(fake_port.removed());
  }
}

TEST_F(NetworkDeviceTest, TxOnUnattachedPort) {
  // Test that transmitting a frame to a port we're not attached to returns the buffer with an
  // error.
  ASSERT_OK(CreateDevice());
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  constexpr uint16_t kDesc = 0;
  buffer_descriptor_t* desc = session.ResetDescriptor(kDesc);
  desc->port_id = MAX_PORTS - 1;
  ASSERT_OK(session.SendTx(kDesc));
  // Should be returned with an error.
  zx_signals_t observed;
  ASSERT_OK(session.tx_fifo().wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED, zx::time::infinite(),
                                       &observed));
  ASSERT_EQ(observed & (ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED), ZX_FIFO_READABLE);
  uint16_t read_desc = 0xFFFF;
  ASSERT_OK(session.FetchTx(&read_desc));
  ASSERT_EQ(read_desc, kDesc);
  ASSERT_EQ(desc->return_flags,
            static_cast<uint32_t>(netdev::wire::TxReturnFlags::kTxRetError |
                                  netdev::wire::TxReturnFlags::kTxRetNotAvailable));
}

TEST_F(NetworkDeviceTest, RxCrossSessionChaining) {
  // Test that attempting to chain Rx buffers that originated from different sessions will cause
  // the frame to be dropped and that no descriptors will be swallowed.
  ASSERT_OK(CreateDevice());
  TestSession session_a;
  ASSERT_OK(OpenSession(&session_a));
  ASSERT_OK(session_a.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  ASSERT_OK(WaitStart());
  // Send a single descriptor to the device and wait for it to be available.
  session_a.ResetDescriptor(0);
  ASSERT_OK(session_a.SendRx(0));
  ASSERT_OK(WaitRxAvailable());
  std::unique_ptr buffer_a = impl_.PopRxBuffer();
  ASSERT_TRUE(buffer_a);
  // Start a second session.
  TestSession session_b;
  ASSERT_OK(OpenSession(&session_b));
  ASSERT_OK(session_b.SetPaused(false));
  ASSERT_OK(WaitSessionStarted());
  session_b.ResetDescriptor(0);
  ASSERT_OK(session_b.SendRx(0));

  // Close session A, it should no longer be primary. Then we should receive the rx buffer from
  // session B.
  ASSERT_OK(session_a.Close());
  ASSERT_OK(WaitRxAvailable());
  // We still hold buffer from Session A, it can't be fully closed yet.
  ASSERT_STATUS(session_a.WaitClosed(zx::time::infinite_past()), ZX_ERR_TIMED_OUT);

  std::unique_ptr buffer_b = impl_.PopRxBuffer();
  ASSERT_TRUE(buffer_b);
  rx_space_buffer_t space_b = buffer_b->space();

  // Space from each buffer must've come from different VMOs.
  ASSERT_NE(buffer_a->space().vmo, buffer_b->space().vmo);
  // Return both buffers as a single chained rx frame.
  buffer_a->return_part().length = 0xdead;
  buffer_b->return_part().length = 0xbeef;
  auto ret = std::make_unique<RxReturn>();
  ret->PushPart(std::move(buffer_a));
  ret->PushPart(std::move(buffer_b));
  {
    RxReturnTransaction transaction(&impl_);
    transaction.Enqueue(std::move(ret));
    transaction.Commit();
  }

  // By committing the transaction, the expectation is:
  // - Session A must've stopped because all its buffers have been returned.
  // - Session B must not have received any buffers through the FIFO because the frame must be
  // discarded.
  // - Buffer B must come back to the available buffer queue because it Session B is still valid and
  // the frame was discarded.
  ASSERT_OK(session_a.WaitClosed(zx::time::infinite()));
  {
    uint16_t descriptor = 0xFFFF;
    ASSERT_STATUS(session_b.FetchRx(&descriptor), ZX_ERR_SHOULD_WAIT)
        << "descriptor=" << descriptor;
  }
  ASSERT_OK(WaitRxAvailable());
  std::unique_ptr buffer_b_again = impl_.PopRxBuffer();
  ASSERT_TRUE(buffer_b_again);
  const rx_space_buffer_t& space = buffer_b_again->space();
  EXPECT_EQ(space.vmo, space_b.vmo);
  EXPECT_EQ(space.region.offset, space_b.region.offset);
  EXPECT_EQ(space.region.length, space_b.region.length);
  {
    RxReturnTransaction transaction(&impl_);
    transaction.Enqueue(std::move(buffer_b_again));
    transaction.Commit();
  }
}

TEST_F(NetworkDeviceTest, SessionRejectsChainedRxSpace) {
  // Tests that sessions do not accept chained descriptors on the Rx FIFO.
  ASSERT_OK(CreateDevice());
  TestSession session;
  ASSERT_OK(OpenSession(&session));
  ASSERT_OK(session.SetPaused(false));
  ASSERT_OK(WaitStart());
  session.ResetDescriptor(1);
  {
    buffer_descriptor_t* desc = session.ResetDescriptor(0);
    desc->chain_length = 1;
    desc->nxt = 1;
  }
  ASSERT_OK(session.SendRx(0));
  // Session will be closed because of bad descriptor.
  ASSERT_OK(session.WaitClosed(zx::time::infinite()));
}

enum class BufferReturnMethod {
  NoReturn,
  ManualReturn,
  ImmediateReturn,
};

using RxTxBufferReturnParameters = std::tuple<RxTxSwitch, BufferReturnMethod, bool>;

const std::string rxTxBufferReturnTestToString(
    const ::testing::TestParamInfo<RxTxBufferReturnParameters>& info) {
  std::stringstream ss;
  auto [rxtx, return_method, auto_stop] = info.param;
  ss << rxTxSwitchToString(rxtx);
  switch (return_method) {
    case BufferReturnMethod::NoReturn:
      ss << "NoReturn";
      break;
    case BufferReturnMethod::ManualReturn:
      ss << "ManualReturn";
      break;
    case BufferReturnMethod::ImmediateReturn:
      ss << "ImmediateReturn";
      break;
  }
  if (auto_stop) {
    ss << "AutoStop";
  } else {
    ss << "NoAutoStop";
  }
  return ss.str();
}
class RxTxBufferReturnTest : public NetworkDeviceTest,
                             public ::testing::WithParamInterface<RxTxBufferReturnParameters> {};

TEST_P(RxTxBufferReturnTest, TestRaceFramesWithDeviceStop) {
  // Test that racing a closing session with data on the Tx FIFO will do the right thing:
  // - No buffers referencing old VMO IDs remain.
  // - The device is stopped appropriately.
  // - VMOs are cleaned up.
  //
  // Some correctness assertions exercised here are part of the test fixtures and enforce correct
  // contract:
  // - NetworkDeviceImplStart and NetworkDeviceImplStop can't be called when device is already in
  // that state.
  ASSERT_OK(CreateDevice());

  auto [rxtx, return_method, auto_stop] = GetParam();
  impl_.set_auto_stop(auto_stop);

  // Run the test multiple times to increase chance of reproducing race in a single run.
  constexpr uint16_t kIterations = 10;
  for (uint16_t i = 0; i < kIterations; i++) {
    TestSession session;
    ASSERT_OK(OpenSession(&session));
    ASSERT_OK(session.SetPaused(false));
    ASSERT_OK(WaitStart());
    session.ResetDescriptor(i);
    fit::function<void()> manual_return;
    switch (rxtx) {
      case RxTxSwitch::Rx:
        impl_.set_immediate_return_rx(return_method == BufferReturnMethod::ImmediateReturn);
        ASSERT_OK(session.SendRx(i));
        if (return_method == BufferReturnMethod::ManualReturn) {
          ASSERT_OK(WaitRxAvailable());
          std::unique_ptr buffer = impl_.PopRxBuffer();
          buffer->return_part().length = kDefaultBufferLength;
          ASSERT_FALSE(impl_.PopRxBuffer());
          manual_return = [this, buffer = std::move(buffer)]() mutable {
            RxReturnTransaction transact(&impl_);
            transact.Enqueue(std::move(buffer));
            transact.Commit();
          };
        }
        break;
      case RxTxSwitch::Tx:
        impl_.set_immediate_return_tx(return_method == BufferReturnMethod::ImmediateReturn);
        ASSERT_OK(session.SendTx(i));
        if (return_method == BufferReturnMethod::ManualReturn) {
          ASSERT_OK(WaitTx());
          std::unique_ptr buffer = impl_.PopTxBuffer();
          buffer->set_status(ZX_OK);
          ASSERT_FALSE(impl_.PopTxBuffer());
          manual_return = [this, buffer = std::move(buffer)]() mutable {
            TxReturnTransaction transact(&impl_);
            transact.Enqueue(std::move(buffer));
            transact.Commit();
          };
        }
        break;
    }
    session.Close();
    if (manual_return) {
      manual_return();
    }
    ASSERT_OK(WaitStop());
    if (!auto_stop) {
      ASSERT_TRUE(impl_.TriggerStop());
    }

    for (;;) {
      zx_wait_item_t items[] = {
          {
              .handle = session.channel().get(),
              .waitfor = ZX_CHANNEL_PEER_CLOSED,
          },
          {
              .handle = impl_.events().get(),
              .waitfor = kEventTx | kEventRxAvailable,
          },
      };
      auto& [session_wait, events_wait] = items;
      ASSERT_OK(zx_object_wait_many(items, std::size(items), TEST_DEADLINE.get()));
      // Here's where we observe and assert on our races. We're waiting for the session to close,
      // but we're racing with rx buffers becoming available again and the session teardown itself.
      if (events_wait.pending & kEventRxAvailable) {
        ASSERT_OK(impl_.events().signal(kEventRxAvailable, 0));
        // If new rx buffers came back to us, the session must not have been closed.
        ASSERT_FALSE(session_wait.pending & ZX_CHANNEL_PEER_CLOSED);
        RxReturnTransaction return_rx(&impl_);
        for (std::unique_ptr buffer = impl_.PopRxBuffer(); buffer; buffer = impl_.PopRxBuffer()) {
          buffer->return_part().length = 0;
          return_rx.Enqueue(std::move(buffer));
        }
        return_rx.Commit();
      }

      // When no returns and no auto stopping we may have the pending tx frame that hasn't been
      // returned yet.
      if (return_method == BufferReturnMethod::NoReturn && !auto_stop) {
        if (events_wait.pending & kEventTx) {
          ASSERT_OK(impl_.events().signal(kEventTx, 0));
          // If we still have pending tx buffers then the session must not have been closed.
          ASSERT_FALSE(session_wait.pending & ZX_CHANNEL_PEER_CLOSED);
          TxReturnTransaction return_tx(&impl_);
          for (std::unique_ptr buffer = impl_.PopTxBuffer(); buffer; buffer = impl_.PopTxBuffer()) {
            buffer->set_status(ZX_ERR_UNAVAILABLE);
            return_tx.Enqueue(std::move(buffer));
          }
          return_tx.Commit();
        }
      } else {
        ASSERT_FALSE(events_wait.pending & kEventTx);
      }

      if (session_wait.pending & ZX_CHANNEL_PEER_CLOSED) {
        ASSERT_FALSE(events_wait.pending & kEventTx);
        ASSERT_FALSE(events_wait.pending & kEventRxAvailable);
        break;
      }
    }

    fbl::Span vmos = impl_.vmos();
    for (auto vmo = vmos.begin(); vmo != vmos.end(); vmo++) {
      ASSERT_FALSE(vmo->is_valid())
          << "unreleased VMO found at " << std::distance(vmo, vmos.begin());
    }
  }
}

INSTANTIATE_TEST_SUITE_P(NetworkDeviceTest, RxTxBufferReturnTest,
                         ::testing::Combine(::testing::Values(RxTxSwitch::Rx, RxTxSwitch::Tx),
                                            ::testing::Values(BufferReturnMethod::NoReturn,
                                                              BufferReturnMethod::ManualReturn,
                                                              BufferReturnMethod::ImmediateReturn),
                                            ::testing::Bool()),
                         rxTxBufferReturnTestToString);

INSTANTIATE_TEST_SUITE_P(NetworkDeviceTest, RxTxParamTest,
                         ::testing::Values(RxTxSwitch::Rx, RxTxSwitch::Tx), rxTxParamTestToString);

}  // namespace testing
}  // namespace network
