// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "serial-ppp.h"

#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/event.h>
#include <lib/zx/socket.h>
#include <zircon/errors.h>

#include <iomanip>
#include <sstream>

#include <fbl/span.h>
#include <zxtest/zxtest.h>

#include "lib/common/ppp.h"

namespace {

namespace netdev = llcpp::fuchsia::hardware::network;

class SerialPppHarness : public zxtest::Test {
 public:
  static constexpr uint32_t kEventStatusChanged = ZX_USER_SIGNAL_0;
  static constexpr uint32_t kEventRxCompleted = ZX_USER_SIGNAL_1;
  static constexpr uint32_t kEventTxCompleted = ZX_USER_SIGNAL_2;
  static constexpr uint8_t kVmoId = 0;
  static constexpr uint64_t kDefaultBufferReservation = 2048;

  static constexpr uint64_t kVmoSize = ppp::SerialPpp::kFifoDepth * 2 * ZX_PAGE_SIZE;
  void SetUp() override {
    zx::vmo device_vmo;
    ASSERT_OK(zx::vmo::create(kVmoSize, 0, &device_vmo));
    ASSERT_OK(vmo_.Map(device_vmo, 0, kVmoSize, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE));
    ASSERT_OK(zx::event::create(0, &event_));

    serial_proto_ops_ = serial_protocol_ops_t{
        .get_info = [](void* ctx, serial_port_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; },
        .config = [](void* ctx, uint32_t baud_rate,
                     uint32_t flags) { return ZX_ERR_NOT_SUPPORTED; },
        .open_socket =
            [](void* ctx, zx_handle_t* out_handle) {
              return static_cast<SerialPppHarness*>(ctx)->OpenSocket(out_handle);
            }};

    serial_protocol_t proto = {
        .ops = &serial_proto_ops_,
        .ctx = this,
    };

    device_ = std::make_unique<ppp::SerialPpp>(nullptr, ddk::SerialProtocolClient(&proto));
    // Disable rx timeouts by default to prevent flakes.
    device_->set_enable_rx_timeout(false);

    netdevice_proto_ops_ = network_device_ifc_protocol_ops_t{
        .status_changed =
            [](void* ctx, const status_t* new_status) {
              auto self = static_cast<SerialPppHarness*>(ctx);
              fbl::AutoLock lock(&self->lock_);
              self->status_ = *new_status;
              EXPECT_OK(self->event_.signal(0, kEventStatusChanged));
            },
        .complete_rx =
            [](void* ctx, const rx_buffer_t* rx_list, size_t rx_count) {
              auto self = static_cast<SerialPppHarness*>(ctx);
              fbl::AutoLock lock(&self->lock_);
              for (auto buffer : fbl::Span(rx_list, rx_count)) {
                self->rx_completed_.push(buffer);
              }
              EXPECT_OK(self->event_.signal(0, kEventRxCompleted));
            },
        .complete_tx =
            [](void* ctx, const tx_result_t* tx_list, size_t tx_count) {
              auto self = static_cast<SerialPppHarness*>(ctx);
              fbl::AutoLock lock(&self->lock_);
              for (auto buffer : fbl::Span(tx_list, tx_count)) {
                self->tx_completed_.push(buffer);
              }
              EXPECT_OK(self->event_.signal(0, kEventTxCompleted));
            },
        .snoop = nullptr};
    network_device_ifc_protocol_t netdev_proto = {.ops = &netdevice_proto_ops_, .ctx = this};
    ASSERT_OK(device_->NetworkDeviceImplInit(&netdev_proto));

    device_->NetworkDeviceImplPrepareVmo(kVmoId, std::move(device_vmo));
    // NB: We're assuming starting is synchronous here.
    device_->NetworkDeviceImplStart([](void* cookie) {}, nullptr);
  }

  zx_status_t OpenSocket(zx_handle_t* out) {
    zx::socket driver_socket;
    zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &driver_socket, &socket_);
    EXPECT_OK(status);
    if (status == ZX_OK) {
      *out = driver_socket.release();
    }
    return status;
  }

  void Tx(uint32_t id, uint64_t offset, netdev::FrameType type, fbl::Span<const uint8_t> data) {
    std::copy(data.begin(), data.end(), vmo_data().subspan(offset).begin());
    buffer_region_t part = {
        .offset = offset,
        .length = data.size(),
    };
    tx_buffer_t buffer = {
        .id = id,
        .data =
            {
                .vmo_id = kVmoId,
                .parts_list = &part,
                .parts_count = 1,
            },
        .meta =
            {
                .info = frame_info_t{},
                .info_type = 0,
                .flags = 0,
                .frame_type = static_cast<uint8_t>(type),
            },
        .head_length = 0,
        .tail_length = 0,
    };
    device_->NetworkDeviceImplQueueTx(&buffer, 1);
  }

  void PushRxSpace(uint32_t id, uint64_t offset, uint64_t length = kDefaultBufferReservation) {
    buffer_region_t part = {.offset = offset, .length = length};
    rx_space_buffer_t space = {
        .id = id,
        .data = {.vmo_id = kVmoId, .parts_list = &part, .parts_count = 1},
    };
    device_->NetworkDeviceImplQueueRxSpace(&space, 1);
  }

  fbl::Span<uint8_t> vmo_data() {
    return fbl::Span<uint8_t>(static_cast<uint8_t*>(vmo_.start()), kVmoSize);
  }

  void TearDown() override { device_->Shutdown(); }

  zx_status_t Wait(zx_signals_t signals, zx::time deadline = zx::time::infinite()) {
    zx_status_t status = event_.wait_one(signals, deadline, nullptr);
    if (status == ZX_OK) {
      status = event_.signal(signals, 0);
    }
    return status;
  }

  ppp::SerialPpp& Device() { return *device_; }
  zx::event& Event() { return event_; }
  zx::socket& Socket() { return socket_; }

  fit::optional<rx_buffer_t> PopRx() {
    fbl::AutoLock lock(&lock_);
    if (rx_completed_.empty()) {
      return fit::nullopt;
    }
    rx_buffer_t ret = rx_completed_.front();
    rx_completed_.pop();
    return ret;
  }

  fit::result<rx_buffer_t, zx_status_t> WaitRx() {
    for (;;) {
      auto buffer = PopRx();
      if (buffer.has_value()) {
        return fit::ok(*buffer);
      }
      zx_status_t status = Wait(kEventRxCompleted);
      if (status != ZX_OK) {
        return fit::error(status);
      }
    }
  }

  fit::optional<tx_result_t> PopTx() {
    fbl::AutoLock lock(&lock_);
    if (tx_completed_.empty()) {
      return fit::nullopt;
    }
    tx_result_t ret = tx_completed_.front();
    tx_completed_.pop();
    return ret;
  }

  std::queue<rx_buffer_t> PopAllRxAndClearEvent() {
    fbl::AutoLock lock(&lock_);
    auto ret = std::move(rx_completed_);
    rx_completed_ = {};
    EXPECT_OK(event_.signal(kEventRxCompleted, 0));
    return ret;
  }

  std::queue<tx_result_t> PopAllTxAndClearEvent() {
    fbl::AutoLock lock(&lock_);
    auto ret = std::move(tx_completed_);
    tx_completed_ = {};
    EXPECT_OK(event_.signal(kEventTxCompleted, 0));
    return ret;
  }

  fit::optional<status_t> TakeStatus() {
    fbl::AutoLock lock(&lock_);
    auto ret = status_;
    status_.reset();
    return ret;
  }

 private:
  std::unique_ptr<ppp::SerialPpp> device_;
  zx::socket socket_;
  serial_protocol_ops_t serial_proto_ops_;
  network_device_ifc_protocol_ops_t netdevice_proto_ops_;
  zx::event event_;

  fbl::Mutex lock_;
  fit::optional<status_t> status_ __TA_GUARDED(lock_);
  std::queue<rx_buffer_t> rx_completed_ __TA_GUARDED(lock_);
  std::queue<tx_result_t> tx_completed_ __TA_GUARDED(lock_);
  fzl::VmoMapper vmo_;
};

TEST_F(SerialPppHarness, DriverTx) {
  std::string information = "Hello\x7eworld!";
  fbl::Span<const uint8_t> span(reinterpret_cast<const uint8_t*>(information.data()),
                                information.size());
  Tx(0, 0, netdev::FrameType::IPV4, span);
  ASSERT_OK(Wait(kEventTxCompleted));
  const std::vector<uint8_t> expect = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x21, 'H', 'e',  'l',  'l',  'o',
      0x7d, 0x5e, 'w',  'o',  'r',  'l',  'd',  '!', 0x28, 0x67, 0x7e,
  };
  std::vector<uint8_t> buffer(expect.size() * 2);
  size_t actual = 0;
  ASSERT_OK(Socket().read(0, buffer.data(), buffer.size(), &actual));
  ASSERT_EQ(actual, expect.size());
  ASSERT_BYTES_EQ(buffer.data(), expect.data(), expect.size());
}

TEST_F(SerialPppHarness, DriverMultipleTx) {
  std::string information = "Hello\x7eworld!";
  fbl::Span<const uint8_t> span(reinterpret_cast<const uint8_t*>(information.data()),
                                information.size());
  constexpr uint32_t kFrameCount = 5;
  for (uint32_t i = 0; i < kFrameCount; i++) {
    Tx(i, i * kDefaultBufferReservation, netdev::FrameType::IPV4, span);
  }

  const std::vector<uint8_t> expect = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x21, 'H', 'e',  'l',  'l',  'o',
      0x7d, 0x5e, 'w',  'o',  'r',  'l',  'd',  '!', 0x28, 0x67, 0x7e,
  };
  std::vector<uint8_t> buffer(expect.size() * kFrameCount);
  size_t read = 0;
  while (read < buffer.size()) {
    ASSERT_OK(Socket().wait_one(ZX_SOCKET_READABLE, zx::time::infinite(), nullptr));
    size_t actual;
    ASSERT_OK(Socket().read(0, &buffer[read], buffer.size() - read, &actual));
    read += actual;
  }
  for (uint32_t i = 0; i < kFrameCount; i++) {
    ASSERT_BYTES_EQ(&buffer[i * expect.size()], expect.data(), expect.size(), "frame %d", i);
  }
}

TEST_F(SerialPppHarness, DriverTxEscapedFcs) {
  // I stumbled upon a string that has a flag sequence in its FCS. Thus this
  // test is able to exist.
  std::string information = "\x29Hello\x7eworld!";
  fbl::Span<const uint8_t> span(reinterpret_cast<const uint8_t*>(information.data()),
                                information.size());
  Tx(0, 0, netdev::FrameType::IPV4, span);
  ASSERT_OK(Wait(kEventTxCompleted));
  auto tx_result = PopTx();
  ASSERT_TRUE(tx_result.has_value());
  ASSERT_OK(tx_result->status);
  ASSERT_EQ(tx_result->id, 0);
  const std::vector<uint8_t> expect = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x21, 0x29, 'H',  'e',  'l',  'l',  'o',
      0x7d, 0x5e, 'w',  'o',  'r',  'l',  'd',  '!',  0x7d, 0x3c, 0x83, 0x7e,
  };
  std::vector<uint8_t> buffer(expect.size() * 2);
  size_t actual = 0;
  ASSERT_OK(Socket().read(0, buffer.data(), buffer.size(), &actual));
  ASSERT_EQ(actual, expect.size());
  ASSERT_BYTES_EQ(buffer.data(), expect.data(), expect.size());
}

TEST_F(SerialPppHarness, DriverTxEmpty) {
  std::string information;
  fbl::Span<const uint8_t> span(reinterpret_cast<const uint8_t*>(information.data()),
                                information.size());
  Tx(0, 0, netdev::FrameType::IPV6, span);
  ASSERT_OK(Wait(kEventTxCompleted));
  auto tx_result = PopTx();
  ASSERT_TRUE(tx_result.has_value());
  ASSERT_OK(tx_result->status);
  ASSERT_EQ(tx_result->id, 0);
  const std::vector<uint8_t> expect = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x57, 0x52, 0xf0, 0x7e,
  };
  std::vector<uint8_t> buffer(expect.size() * 2);
  size_t actual = 0;
  ASSERT_OK(Socket().read(0, buffer.data(), buffer.size(), &actual));
  ASSERT_EQ(actual, expect.size());
  ASSERT_BYTES_EQ(buffer.data(), expect.data(), expect.size());
}

TEST_F(SerialPppHarness, DriverRxIgnoresUnknownProtocols) {
  PushRxSpace(0, 0, kDefaultBufferReservation);
  std::string information = "\x80\x21Hello\x7eworld!";
  const std::vector<uint8_t> serial_data = {
      0x7e, 0xff, 0x7d, 0x23, 0x80, 0x21, 'H', 'e', 'l',  'l',  'o',
      0x7d, 0x5e, 'w',  'o',  'r',  'l',  'd', '!', 0xf6, 0xe1, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());
  ASSERT_STATUS(Wait(kEventRxCompleted, zx::deadline_after(zx::msec(20))), ZX_ERR_TIMED_OUT);
}

TEST_F(SerialPppHarness, DriverRxSingleFrame) {
  PushRxSpace(0, 0);
  std::string information = "Some Ipv4";
  const std::vector<uint8_t> serial_data = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x21, 'S',  'o',  'm',
      'e',  ' ',  'I',  'p',  'v',  '4',  0xae, 0xdf, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());
  ASSERT_OK(Wait(kEventRxCompleted));
  auto rx_buffer = PopRx();
  ASSERT_TRUE(rx_buffer.has_value());
  ASSERT_EQ(rx_buffer->meta.frame_type, static_cast<uint8_t>(netdev::FrameType::IPV4));
  auto rx_data = vmo_data().subspan(0, rx_buffer->total_length);
  ASSERT_EQ(rx_data.size(), information.length());
  ASSERT_BYTES_EQ(rx_data.data(), information.data(), rx_data.size());
}

TEST_F(SerialPppHarness, DriverRxSingleFrameFiller) {
  PushRxSpace(0, 0);
  std::string information = "Some Ipv4";
  const std::vector<uint8_t> serial_data = {
      0x7e, 0x7e, 0x7e, 0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x21, 'S',  'o',  'm',
      'e',  ' ',  'I',  'p',  'v',  '4',  0xae, 0xdf, 0x7e, 0x7e, 0x7e, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());
  ASSERT_OK(Wait(kEventRxCompleted));
  auto rx_buffer = PopRx();
  ASSERT_TRUE(rx_buffer.has_value());
  ASSERT_EQ(rx_buffer->meta.frame_type, static_cast<uint8_t>(netdev::FrameType::IPV4));
  auto rx_data = vmo_data().subspan(0, rx_buffer->total_length);
  ASSERT_EQ(rx_data.size(), information.length());
  ASSERT_BYTES_EQ(rx_data.data(), information.data(), rx_data.size());
}

TEST_F(SerialPppHarness, DriverRxTwoJoinedFrames) {
  PushRxSpace(0, 0);
  PushRxSpace(1, kDefaultBufferReservation);
  std::string information0 = "Some Ipv4";
  std::string information1 = "Hello\x7eworld!";
  const std::vector<uint8_t> serial_data = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x21, 'S',  'o',  'm',  'e',  ' ',  'I',  'p',
      'v',  '4',  0xae, 0xdf, 0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x21, 'H',  'e',  'l',
      'l',  'o',  0x7d, 0x5e, 'w',  'o',  'r',  'l',  'd',  '!',  0x28, 0x67, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());

  auto validate_rx = [this](const rx_buffer_t& result, const std::string& expect) {
    ASSERT_EQ(result.meta.frame_type, static_cast<uint8_t>(netdev::FrameType::IPV4), "buffer %d",
              result.id);
    auto data = vmo_data().subspan(result.id * kDefaultBufferReservation, result.total_length);
    ASSERT_EQ(data.size(), expect.size(), "buffer %d", result.id);
    ASSERT_BYTES_EQ(data.data(), expect.data(), data.size(), "buffer %d", result.id);
  };
  auto rx = WaitRx();
  ASSERT_TRUE(rx.is_ok(), "rx failed: %s", zx_status_get_string(rx.error()));
  ASSERT_NO_FATAL_FAILURES(validate_rx(rx.take_value(), information0));
  rx = WaitRx();
  ASSERT_TRUE(rx.is_ok(), "rx failed: %s", zx_status_get_string(rx.error()));
  ASSERT_NO_FATAL_FAILURES(validate_rx(rx.take_value(), information1));
}

TEST_F(SerialPppHarness, DriverRxEmpty) {
  PushRxSpace(0, 0);
  const std::vector<uint8_t> serial_data = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x57, 0x52, 0xf0, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());
  ASSERT_OK(Wait(kEventRxCompleted));
  auto rx_buffer = PopRx();
  ASSERT_TRUE(rx_buffer.has_value());
  ASSERT_EQ(rx_buffer->meta.frame_type, static_cast<uint8_t>(netdev::FrameType::IPV6));
  ASSERT_EQ(rx_buffer->total_length, 0);
}

TEST_F(SerialPppHarness, DriverRxBadProtocol) {
  PushRxSpace(0, 0);
  PushRxSpace(1, kDefaultBufferReservation);
  const std::vector<uint8_t> serial_data = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x58, 0x52, 0xf0, 0x7e,
      0xff, 0x7d, 0x23, 0x7d, 0x20, 0x57, 0x52, 0xf0, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());
  ASSERT_OK(Wait(kEventRxCompleted));
  auto rx_buffer = PopRx();
  ASSERT_TRUE(rx_buffer.has_value());
  ASSERT_EQ(rx_buffer->meta.frame_type, static_cast<uint8_t>(netdev::FrameType::IPV6));
  ASSERT_EQ(rx_buffer->total_length, 0);
}

TEST_F(SerialPppHarness, DriverRxBadHeader) {
  PushRxSpace(0, 0);
  PushRxSpace(1, kDefaultBufferReservation);
  const std::vector<uint8_t> serial_data = {
      0x7e, 0x7d, 0x20, 0x7d, 0x20, 0x7d, 0x20, 0x57, 0x52, 0xf0,
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x57, 0x52, 0xf0, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());
  ASSERT_OK(Wait(kEventRxCompleted));
  auto rx_buffer = PopRx();
  ASSERT_TRUE(rx_buffer.has_value());
  ASSERT_EQ(rx_buffer->meta.frame_type, static_cast<uint8_t>(netdev::FrameType::IPV6));
  ASSERT_EQ(rx_buffer->total_length, 0);
}

TEST_F(SerialPppHarness, DriverRxTooShort) {
  PushRxSpace(0, 0);
  PushRxSpace(1, kDefaultBufferReservation);
  const std::vector<uint8_t> serial_data = {
      0x7e, 0x7d, 0x20, 0x7d, 0x20, 0x57, 0x52, 0xf0, 0x7e,
      0xff, 0x7d, 0x23, 0x7d, 0x20, 0x57, 0x52, 0xf0, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());
  ASSERT_OK(Wait(kEventRxCompleted));
  auto rx_buffer = PopRx();
  ASSERT_TRUE(rx_buffer.has_value());
  ASSERT_EQ(rx_buffer->meta.frame_type, static_cast<uint8_t>(netdev::FrameType::IPV6));
  ASSERT_EQ(rx_buffer->total_length, 0);
}

TEST_F(SerialPppHarness, DriverRxTooLong) {
  PushRxSpace(0, 0);
  PushRxSpace(1, kDefaultBufferReservation);
  const std::vector<uint8_t> serial_data_1(1500 * 2 + 9);
  const std::vector<uint8_t> serial_data_2 = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x57, 0x52, 0xf0, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data_1.data(), serial_data_1.size(), &actual));
  ASSERT_EQ(actual, serial_data_1.size());

  actual = 0;
  ASSERT_OK(Socket().write(0, serial_data_2.data(), serial_data_2.size(), &actual));
  ASSERT_EQ(actual, serial_data_2.size());

  ASSERT_OK(Wait(kEventRxCompleted));
  auto rx_buffer = PopRx();
  ASSERT_TRUE(rx_buffer.has_value());
  ASSERT_EQ(rx_buffer->meta.frame_type, static_cast<uint8_t>(netdev::FrameType::IPV6));
  ASSERT_EQ(rx_buffer->total_length, 0);
}

TEST_F(SerialPppHarness, DriverRxBadFrameCheckSequence) {
  PushRxSpace(0, 0);
  PushRxSpace(1, kDefaultBufferReservation);
  const std::vector<uint8_t> serial_data = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x57, 0x7d, 0x20, 0x7d, 0x20,
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x57, 0x52, 0xf0, 0x7e,
  };
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data.data(), serial_data.size(), &actual));
  ASSERT_EQ(actual, serial_data.size());

  ASSERT_OK(Wait(kEventRxCompleted));
  auto rx_buffer = PopRx();
  ASSERT_TRUE(rx_buffer.has_value());
  ASSERT_EQ(rx_buffer->meta.frame_type, static_cast<uint8_t>(netdev::FrameType::IPV6));
  ASSERT_EQ(rx_buffer->total_length, 0);
}

TEST_F(SerialPppHarness, DriverRxTimeout) {
  Device().set_enable_rx_timeout(true);
  PushRxSpace(0, 0);
  std::string expect = "Hello\x7eworld!";
  const std::vector<uint8_t> serial_data0 = {
      0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x21, 'S',  'o',
      'm',  'e',  ' ',  'I',  'p',  'v',  '4',  0xae, 0xdf,
  };
  const std::vector<uint8_t> serial_data1 = {0x7e, 0xff, 0x7d, 0x23, 0x7d, 0x20, 0x21, 'H',
                                             'e',  'l',  'l',  'o',  0x7d, 0x5e, 'w',  'o',
                                             'r',  'l',  'd',  '!',  0x28, 0x67, 0x7e};
  size_t actual = 0;
  ASSERT_OK(Socket().write(0, serial_data0.data(), serial_data0.size(), &actual));
  ASSERT_EQ(actual, serial_data0.size());
  // Give the device plenty of time to timeout the first buffer.
  zx::nanosleep(zx::deadline_after(ppp::SerialPpp::kSerialTimeout * 10));

  ASSERT_OK(Socket().write(0, serial_data1.data(), serial_data1.size(), &actual));
  ASSERT_EQ(actual, serial_data1.size());

  // Only the second frame must have been received.
  ASSERT_OK(Wait(kEventRxCompleted));
  auto rx_buffer = PopRx();
  ASSERT_TRUE(rx_buffer.has_value());
  ASSERT_EQ(rx_buffer->meta.frame_type, static_cast<uint8_t>(netdev::FrameType::IPV4));
  auto data = vmo_data().subspan(0, rx_buffer->total_length);
  ASSERT_EQ(data.size(), expect.size());
  ASSERT_BYTES_EQ(data.data(), expect.data(), data.size());
}

TEST_F(SerialPppHarness, CycleTraffic) {
  for (uint16_t i = 0; i < ppp::SerialPpp::kFifoDepth; i++) {
    PushRxSpace(i, i * kDefaultBufferReservation);
  }
  constexpr size_t kTotalFrames = ppp::SerialPpp::kFifoDepth * 16;

  // Counts the number of rx and tx observed at driver level.
  size_t rx_driver_count = 0;
  size_t tx_driver_count = 0;
  size_t tx_returned_driver_count = 0;

  // Counts the number of rx and tx observed at serial socket.
  size_t rx_serial_count = 0;
  size_t tx_serial_count = 0;

  fit::optional<std::pair<std::vector<uint8_t>, size_t>> pending_write;
  std::array<uint8_t, kDefaultBufferReservation> socket_read_buffer;
  auto socket_read_offset = socket_read_buffer.begin();

  auto make_span = [](size_t* counter) -> fbl::Span<uint8_t> {
    return fbl::Span(reinterpret_cast<uint8_t*>(counter), sizeof(size_t));
  };

  auto hex_buffer = [](const fbl::Span<uint8_t>& span) -> std::string {
    std::stringstream ss;
    for (auto b : span) {
      ss << std::setw(2) << std::hex << static_cast<int>(b) << ",";
    }
    return ss.str();
  };

  std::vector<uint8_t> serial_expect =
      ppp::SerializeFrame(ppp::FrameView(ppp::Protocol::Ipv4, make_span(&tx_serial_count)));

  // Enqueue FIFO-depth tx frames.
  for (uint32_t i = 0; i < ppp::SerialPpp::kFifoDepth; i++) {
    uint32_t id = i + ppp::SerialPpp::kFifoDepth;
    Tx(id, id * kDefaultBufferReservation, netdev::FrameType::IPV4,
       fbl::Span(reinterpret_cast<uint8_t*>(&tx_driver_count), sizeof(tx_driver_count)));
    tx_driver_count++;
  }

  while (rx_driver_count < kTotalFrames || tx_driver_count < kTotalFrames ||
         tx_serial_count < kTotalFrames || rx_serial_count < kTotalFrames ||
         tx_returned_driver_count < kTotalFrames) {
    zx_wait_item_t wait[] = {{
                                 .handle = Socket().get(),
                                 .waitfor = ZX_SOCKET_READABLE,
                                 .pending = 0,
                             },
                             {
                                 .handle = Event().get(),
                                 .waitfor = kEventRxCompleted | kEventTxCompleted,
                                 .pending = 0,
                             }};
    if (rx_serial_count < kTotalFrames || pending_write.has_value()) {
      wait[0].waitfor |= ZX_SOCKET_WRITABLE;
    }
    ASSERT_OK(zx_object_wait_many(wait, countof(wait), zx::time::infinite().get()));
    if (wait[0].pending & wait[0].waitfor & ZX_SOCKET_WRITABLE) {
      std::vector<uint8_t> frame;
      size_t offset;
      if (pending_write.has_value()) {
        frame = std::move(pending_write->first);
        offset = pending_write->second;
        pending_write.reset();
      } else {
        frame =
            ppp::SerializeFrame(ppp::FrameView(ppp::Protocol::Ipv4, make_span(&rx_serial_count)));
        rx_serial_count++;
        offset = 0;
      }
      size_t actual;
      ASSERT_OK(Socket().write(0, &frame[offset], frame.size() - offset, &actual));

      if (actual != frame.size() - offset) {
        pending_write = std::make_pair(std::move(frame), offset + actual);
      }
    }
    if (wait[0].pending & ZX_SOCKET_READABLE) {
      size_t actual;
      ASSERT_OK(Socket().read(0, socket_read_offset, socket_read_buffer.end() - socket_read_offset,
                              &actual));
      socket_read_offset += actual;

      while (static_cast<size_t>(socket_read_offset - socket_read_buffer.begin()) >=
             serial_expect.size()) {
        fbl::Span<uint8_t> serial_frame(socket_read_buffer.data(), serial_expect.size());

        auto maybe_frame = ppp::DeserializeFrame(serial_frame);
        ASSERT_TRUE(maybe_frame.is_ok(), "failed to deserialize: %d, buffer=[%s], expect=[%s]",
                    maybe_frame.error(), hex_buffer(serial_frame).c_str(),
                    hex_buffer(fbl::Span(serial_expect.data(), serial_expect.size())).c_str());
        auto& frame = maybe_frame.value();
        ASSERT_EQ(frame.protocol, ppp::Protocol::Ipv4);
        ASSERT_EQ(frame.information.size(), sizeof(tx_serial_count));
        ASSERT_BYTES_EQ(frame.information.data(), reinterpret_cast<uint8_t*>(&tx_serial_count),
                        sizeof(tx_serial_count));
        socket_read_offset = std::copy(socket_read_buffer.begin() + serial_frame.size(),
                                       socket_read_offset, socket_read_buffer.begin());
        // Update serial expectation.
        tx_serial_count++;
        serial_expect =
            ppp::SerializeFrame(ppp::FrameView(ppp::Protocol::Ipv4, make_span(&tx_serial_count)));
      }
    }
    if (wait[1].pending & kEventRxCompleted) {
      auto received = PopAllRxAndClearEvent();
      while (!received.empty()) {
        rx_buffer_t buffer = received.front();
        received.pop();
        ASSERT_EQ(buffer.id, rx_driver_count % ppp::SerialPpp::kFifoDepth);
        uint64_t offset = buffer.id * kDefaultBufferReservation;
        ASSERT_EQ(buffer.meta.frame_type, static_cast<uint8_t>(netdev::FrameType::IPV4));
        ASSERT_EQ(buffer.total_length, sizeof(rx_driver_count));
        ASSERT_BYTES_EQ(vmo_data().begin() + offset, reinterpret_cast<uint8_t*>(&rx_driver_count),
                        sizeof(rx_driver_count));
        rx_driver_count++;
        PushRxSpace(buffer.id, offset);
      }
    }
    if (wait[1].pending & kEventTxCompleted) {
      auto tx_completed = PopAllTxAndClearEvent();
      while (!tx_completed.empty()) {
        tx_result_t result = tx_completed.front();
        tx_completed.pop();
        ASSERT_OK(result.status);
        ASSERT_EQ(result.id, (tx_returned_driver_count % ppp::SerialPpp::kFifoDepth) +
                                 ppp::SerialPpp::kFifoDepth);
        tx_returned_driver_count++;
        if (tx_driver_count < kTotalFrames) {
          Tx(result.id, result.id * kDefaultBufferReservation, netdev::FrameType::IPV4,
             make_span(&tx_driver_count));
          tx_driver_count++;
        }
      }
    }
  }
  EXPECT_EQ(tx_driver_count, kTotalFrames);
  EXPECT_EQ(rx_driver_count, kTotalFrames);
  EXPECT_EQ(rx_serial_count, kTotalFrames);
  EXPECT_EQ(tx_serial_count, kTotalFrames);
  EXPECT_EQ(tx_returned_driver_count, kTotalFrames);
}

TEST_F(SerialPppHarness, TestStatus) {
  // NB: Device is brought online on startup by the harness so we should see that event from the get
  // go.
  bool online = true;
  for (int i = 0; i < 5; i++) {
    ASSERT_OK(Wait(kEventStatusChanged));
    auto status = TakeStatus();
    ASSERT_TRUE(status.has_value());
    status_t read;
    Device().NetworkDeviceImplGetStatus(&read);
    if (online) {
      ASSERT_EQ(status->flags, static_cast<uint32_t>(netdev::StatusFlags::ONLINE));
      ASSERT_EQ(read.flags, static_cast<uint32_t>(netdev::StatusFlags::ONLINE));
      Device().NetworkDeviceImplStop([](void* cookie) {}, nullptr);
    } else {
      ASSERT_EQ(status->flags, 0);
      ASSERT_EQ(read.flags, 0);
      Device().NetworkDeviceImplStart([](void* cookie) {}, nullptr);
    }
    online = !online;
  }
}

}  // namespace
