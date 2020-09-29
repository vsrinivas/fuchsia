// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "serial-ppp.h"

#include <lib/fidl-async/cpp/bind.h>
#include <lib/zx/time.h>
#include <zircon/hw/usb.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddktl/fidl.h>
#include <fbl/auto_call.h>

#include "lib/common/ppp.h"
#include "lib/fit/result.h"
#include "lib/hdlc/frame.h"

namespace ppp {

namespace netdev = ::llcpp::fuchsia::hardware::network;

static constexpr zx_driver_ops_t driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = SerialPpp::Create,
};

constexpr uint64_t kTriggerTxKey = 1;
constexpr uint64_t kTriggerRxKey = 2;
constexpr uint64_t kExitKey = 3;
constexpr uint64_t kWaitSocketReadableKey = 4;
constexpr uint64_t kWaitSocketWritableKey = 5;

static constexpr uint8_t kRxFrameTypes[] = {static_cast<uint8_t>(netdev::FrameType::IPV4),
                                            static_cast<uint8_t>(netdev::FrameType::IPV6)};
static constexpr tx_support_t kTxFrameSupport[] = {
    {.type = static_cast<uint8_t>(netdev::FrameType::IPV4),
     .features = netdev::FRAME_FEATURES_RAW,
     .supported_flags = 0},
    {.type = static_cast<uint8_t>(netdev::FrameType::IPV4),
     .features = netdev::FRAME_FEATURES_RAW,
     .supported_flags = 0}};

SerialPpp::SerialPpp(zx_device_t* parent) : SerialPpp(parent, parent) {}

SerialPpp::SerialPpp(zx_device_t* parent, ddk::SerialProtocolClient serial)
    : DeviceType(parent),
      serial_protocol_(serial),
      vmos_(vmo_store::Options{
          .map = vmo_store::MapOptions{.vm_option = ZX_VM_PERM_WRITE | ZX_VM_PERM_READ}}) {}

zx_status_t SerialPpp::Create(void* /*ctx*/, zx_device_t* parent) {
  auto dev = std::make_unique<SerialPpp>(parent);

  dev->DdkAdd("ppp");

  // Release because devmgr is now in charge of the device.
  static_cast<void>(dev.release());
  return ZX_OK;
}

void SerialPpp::DdkRelease() { delete this; }

void SerialPpp::DdkUnbind(ddk::UnbindTxn txn) {
  Shutdown();
  txn.Reply();
}

void SerialPpp::Shutdown() {
  fbl::AutoLock lock(&state_lock_);
  if (!port_.is_valid()) {
    return;
  }
  zx_port_packet_t packet = {.key = kExitKey, .type = ZX_PKT_TYPE_USER, .status = ZX_OK};
  zx_status_t status = port_.queue(&packet);
  ZX_ASSERT_MSG(status == ZX_OK, "failed to enqueue exit packet: %s", zx_status_get_string(status));
  thread_.join();
  serial_.reset();
  port_.reset();
}

void SerialPpp::WorkerLoop() {
  rx_frame_buffer_offset_ = rx_frame_buffer_.begin();
  *rx_frame_buffer_offset_++ = kFlagSequence;

  bool waiting_readable = false;
  zx::time wait_deadline = zx::time::infinite();
  auto wait_readable = [&waiting_readable, &wait_deadline, this]() {
    if (waiting_readable) {
      return;
    }
    zx_status_t status = serial_.wait_async(port_, kWaitSocketReadableKey, ZX_SOCKET_READABLE, 0);
    if (status != ZX_OK) {
      zxlogf(WARNING, "failed to wait for readable on serial socket: %s",
             zx_status_get_string(status));
      return;
    }
    waiting_readable = true;
    // If we have data in our buffer, establish a deadline for more data to come in over serial.
    // Silence over serial should be considered a dropped message, i.e. the deadline encodes the
    // serial line's "character distance".
    if (enable_rx_timeout_ && rx_frame_buffer_offset_ > rx_frame_buffer_.begin() + 1) {
      wait_deadline = zx::deadline_after(kSerialTimeout);
    } else {
      wait_deadline = zx::time::infinite();
    }
  };

  bool waiting_writable = false;
  auto wait_writable = [&waiting_writable, this]() {
    if (waiting_writable) {
      return;
    }
    zx_status_t status = serial_.wait_async(port_, kWaitSocketWritableKey, ZX_SOCKET_WRITABLE, 0);
    if (status != ZX_OK) {
      zxlogf(WARNING, "failed to wait for writable on serial socket: %s",
             zx_status_get_string(status));
      return;
    }
    waiting_writable = true;
  };

  for (;;) {
    zx_port_packet_t packet;
    zx_status_t status = port_.wait(wait_deadline, &packet);
    switch (status) {
      case ZX_OK:
        break;
      case ZX_ERR_TIMED_OUT:
        // Clear rx buffers if we hit the rx wait deadline. Silence over serial implies dropped
        // message.
        zxlogf(DEBUG, "rx wait timed out");
        wait_deadline = zx::time::infinite();
        rx_frame_buffer_offset_ = rx_frame_buffer_.begin();
        continue;
      default:
        zxlogf(ERROR, "failed to wait on port: %s", zx_status_get_string(status));
        return;
    }
    switch (packet.key) {
      case kTriggerRxKey: {
        // Consume from serial buffer when buffers become available.
        if (ConsumeSerial(false)) {
          wait_readable();
        }
      } break;
      case kTriggerTxKey: {
        wait_writable();
      } break;
      case kWaitSocketReadableKey: {
        waiting_readable = false;
        // Consume serial fetching data from socket.
        if (ConsumeSerial(true)) {
          wait_readable();
        }
      } break;
      case kWaitSocketWritableKey: {
        waiting_writable = false;
        PendingBuffer buffer;
        bool has_more_tx;
        {
          fbl::AutoLock lock(&tx_lock_);
          if (pending_tx_.empty()) {
            // No data to send, continue without waiting for writable.
            continue;
          }
          buffer = pending_tx_.front();
          pending_tx_.pop();
          has_more_tx = !pending_tx_.empty();
        }
        Protocol protocol;
        switch (netdev::FrameType(buffer.type)) {
          case llcpp::fuchsia::hardware::network::FrameType::IPV4:
            protocol = Protocol::Ipv4;
            break;
          case llcpp::fuchsia::hardware::network::FrameType::IPV6:
            protocol = Protocol::Ipv6;
            break;
          default:
            ZX_PANIC("bad tx frame type %d", buffer.type);
        }
        // WriteFramed will block until the entire frame is written on the serial socket.
        // TODO(fxbug.dev/60230) consider exposing the state machine so we can keep operating on the
        // rx side.
        zx_status_t status = WriteFramed(FrameView(protocol, buffer.data));
        if (status != ZX_OK) {
          zxlogf(ERROR, "failed to write tx frame: %s", zx_status_get_string(status));
        }
        tx_result_t result = {
            .id = buffer.id,
            .status = status,
        };
        netdevice_protocol_.CompleteTx(&result, 1);
        if (has_more_tx) {
          wait_writable();
        }
      } break;
      case kExitKey:
        return;
      default:
        zxlogf(ERROR, "received invalid port packet: %ld", packet.key);
        return;
    }
  }
}

bool SerialPpp::ConsumeSerial(bool fetch_from_socket) {
  // Invariant: rx_frame_buffer_ always contains a flag in its first position. |DeserializeFrame|
  // requires that the span it receives has a flag in the first and last positions.
  ZX_DEBUG_ASSERT(rx_frame_buffer_[0] == kFlagSequence);
  bool has_more_rx_space;
  {
    fbl::AutoLock lock(&rx_lock_);
    // Nowhere to put data, continue without waiting for the socket to become readable.
    if (rx_space_.empty()) {
      return false;
    }
    has_more_rx_space = true;
  }

  size_t actual = 0;
  if (fetch_from_socket) {
    zx_status_t status = serial_.read(0, rx_frame_buffer_offset_,
                                      rx_frame_buffer_.end() - rx_frame_buffer_offset_, &actual);
    if (status != ZX_OK) {
      zxlogf(ERROR, "failed to read from serial: %s", zx_status_get_string(status));
      return true;
    }
  }

  auto limit = rx_frame_buffer_offset_ + actual;
  auto frame_begin = rx_frame_buffer_.begin();
  for (auto i = rx_frame_buffer_.begin() + 1; i < limit; i++) {
    if (*i != kFlagSequence) {
      continue;
    }
    if (*(i - 1) == kFlagSequence) {
      frame_begin = i;
      continue;
    }

    // We have a full frame.
    auto maybe_frame = DeserializeFrame(fbl::Span(frame_begin, i + 1));
    // Data has been copied to target buffer if successful, either way discard from rx buffer.
    limit = std::copy(i, limit, rx_frame_buffer_.begin());
    ZX_DEBUG_ASSERT(rx_frame_buffer_[0] == kFlagSequence);
    frame_begin = rx_frame_buffer_.begin();
    i = frame_begin;

    if (maybe_frame.is_error()) {
      zxlogf(DEBUG, "failed to deserialize frame: %d", maybe_frame.error());
      continue;
    }
    auto frame = maybe_frame.take_value();
    netdev::FrameType frame_type;
    switch (frame.protocol) {
      case Protocol::Ipv4:
        frame_type = netdev::FrameType::IPV4;
        break;
      case Protocol::Ipv6:
        frame_type = netdev::FrameType::IPV6;
        break;
      default: {
        // Ignore all other protocols.
        zxlogf(DEBUG, "ignoring frame for protocol 0x%04X", frame.protocol);
        continue;
      }
    }
    rx_buffer_t complete;
    {
      fbl::AutoLock lock(&rx_lock_);
      auto& buffer = rx_space_.front();
      auto copied =
          std::copy(frame.information.begin(), frame.information.end(), buffer.data.begin());
      complete = {.id = buffer.id,
                  .total_length = static_cast<uint64_t>(copied - buffer.data.begin()),
                  .meta = buffer_metadata{
                      .info = frame_info_t{},
                      .info_type = 0,
                      .flags = 0,
                      .frame_type = static_cast<uint8_t>(frame_type),
                  }};
      rx_space_.pop();
      has_more_rx_space = !rx_space_.empty();
    }
    netdevice_protocol_.CompleteRx(&complete, 1);
    if (!has_more_rx_space) {
      // If we run out of rx space, stop consuming the buffer and save for the next read.
      break;
    }
  }
  // Get rid of empty sequences if any.
  // Data has been copied to target buffer if successful, either way discard from rx buffer.
  if (frame_begin != rx_frame_buffer_.begin()) {
    limit = std::copy(frame_begin, limit, rx_frame_buffer_.begin());
  }
  rx_frame_buffer_offset_ = limit;
  if (rx_frame_buffer_offset_ >= rx_frame_buffer_.end()) {
    // Discard data if the buffer is full and we weren't able to find frame boundaries.
    rx_frame_buffer_offset_ = rx_frame_buffer_.begin() + 1;
  }
  return has_more_rx_space;
}

zx_status_t SerialPpp::WriteFramed(FrameView frame) {
  if (frame.information.size() >= kDefaultMtu) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const auto serialized = SerializeFrame(frame);
  auto data = serialized.data();
  auto to_write = serialized.size();

  while (to_write != 0) {
    size_t actual = 0;
    const auto status = serial_.write(0, data, to_write, &actual);
    if (status == ZX_ERR_SHOULD_WAIT) {
      const auto wait_status =
          serial_.wait_one(ZX_SOCKET_WRITABLE, zx::deadline_after(kSerialTimeout), nullptr);
      if (wait_status != ZX_OK) {
        return wait_status;
      }
    } else if (status != ZX_OK) {
      return status;
    } else {
      data += actual;
      to_write -= actual;
    }
  }

  return ZX_OK;
}

zx_status_t SerialPpp::NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface) {
  zx_status_t status = vmos_.Reserve(MAX_VMOS);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to allocate VMO storage: %s", zx_status_get_string(status));
    return status;
  }
  if (netdevice_protocol_.is_valid()) {
    return ZX_ERR_BAD_STATE;
  }
  netdevice_protocol_ = ddk::NetworkDeviceIfcProtocolClient(iface);
  return ZX_OK;
}

void SerialPpp::NetworkDeviceImplStart(network_device_impl_start_callback callback, void* cookie) {
  fbl::AutoCall complete([callback, cookie]() { callback(cookie); });
  zx::socket serial;
  zx::port port;
  zx_status_t status = serial_protocol_.OpenSocket(&serial);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to open serial connection: %s", zx_status_get_string(status));
    return;
  }
  status = zx::port::create(0, &port);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to create signalling port: %s", zx_status_get_string(status));
    return;
  }
  {
    fbl::AutoLock lock(&state_lock_);
    serial_ = std::move(serial);
    port_ = std::move(port);
    thread_ = std::thread([&] { WorkerLoop(); });
  }
  status_t new_status = {.mtu = kDefaultMtu,
                         .flags = static_cast<uint32_t>(netdev::StatusFlags::ONLINE)};
  netdevice_protocol_.StatusChanged(&new_status);
}

void SerialPpp::NetworkDeviceImplStop(network_device_impl_stop_callback callback, void* cookie) {
  fbl::AutoCall complete([callback, cookie]() { callback(cookie); });
  Shutdown();

  status_t new_status = {.mtu = kDefaultMtu, .flags = 0};
  netdevice_protocol_.StatusChanged(&new_status);

  // Clear pending buffers.
  {
    fbl::AutoLock lock(&rx_lock_);
    rx_space_ = {};
  }
  {
    fbl::AutoLock lock(&tx_lock_);
    pending_tx_ = {};
  }
  callback(cookie);
}

void SerialPpp::NetworkDeviceImplGetInfo(device_info_t* out_info) {
  *out_info = device_info_t{
      .tx_depth = kFifoDepth,
      .rx_depth = kFifoDepth,
      .device_class = static_cast<uint8_t>(llcpp::fuchsia::hardware::network::DeviceClass::PPP),
      .rx_types_list = kRxFrameTypes,
      .rx_types_count = sizeof(kRxFrameTypes) / sizeof(uint8_t),
      .tx_types_list = kTxFrameSupport,
      .tx_types_count = sizeof(kTxFrameSupport) / sizeof(tx_support_t),
      .max_buffer_length = kMaxBufferSize,
      .buffer_alignment = 1,
      .min_rx_buffer_length = kDefaultMtu,
  };
}

void SerialPpp::NetworkDeviceImplGetStatus(status_t* out_status) {
  *out_status = status_t{
      .mtu = kDefaultMtu,
      .flags = serial_.is_valid() ? static_cast<uint32_t>(netdev::StatusFlags::ONLINE) : 0};
}

void SerialPpp::NetworkDeviceImplQueueTx(const tx_buffer_t* buf_list, size_t buf_count) {
  std::array<tx_result_t, kFifoDepth> inline_return;
  uint32_t inline_return_count = 0;

  {
    fbl::AutoLock lock(&tx_lock_);
    for (auto buffer : fbl::Span(buf_list, buf_count)) {
      zx_status_t status = ZX_OK;
      if (buffer.data.parts_count == 1) {
        auto& data = *buffer.data.parts_list;
        auto* stored_vmo = vmos_.GetVmo(buffer.data.vmo_id);
        if (stored_vmo) {
          pending_tx_.push(
              PendingBuffer{.id = buffer.id,
                            .data = stored_vmo->data().subspan(data.offset, data.length),
                            .type = buffer.meta.frame_type});
        } else {
          status = ZX_ERR_INVALID_ARGS;
        }
      } else {
        status = ZX_ERR_NOT_SUPPORTED;
      }

      if (status != ZX_OK) {
        auto& r = inline_return[inline_return_count++];
        r.id = buffer.id;
        r.status = status;
      }
    }
  }

  if (inline_return_count != 0) {
    // Immediately return failed tx buffers.
    netdevice_protocol_.CompleteTx(inline_return.data(), inline_return_count);
  }

  if (inline_return_count != buf_count) {
    zx_port_packet_t packet = {.key = kTriggerTxKey, .type = ZX_PKT_TYPE_USER, .status = ZX_OK};
    zx_status_t status = port_.queue(&packet);
    if (status != ZX_OK) {
      zxlogf(ERROR, "failed to trigger tx on port: %s", zx_status_get_string(status));
    }
  }
}

void SerialPpp::NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buf_list, size_t buf_count) {
  {
    fbl::AutoLock lock(&rx_lock_);
    for (auto buffer : fbl::Span(buf_list, buf_count)) {
      if (buffer.data.parts_count != 1) {
        zxlogf(WARNING, "ignoring scatter-gather rx space buffer (%ld parts)",
               buffer.data.parts_count);
        continue;
      }
      auto& data = *buffer.data.parts_list;
      if (data.length < kDefaultMtu) {
        zxlogf(WARNING, "ignoring small rx buffer with size %ld", data.length);
        continue;
      }

      auto* vmo = vmos_.GetVmo(buffer.data.vmo_id);
      if (!vmo) {
        zxlogf(WARNING, "ignoring rx buffer with invalid VMO id: %d", buffer.data.vmo_id);
        continue;
      }

      rx_space_.push(
          PendingBuffer{.id = buffer.id, .data = vmo->data().subspan(data.offset, data.length)});
    }
  }

  zx_port_packet_t packet = {.key = kTriggerRxKey, .type = ZX_PKT_TYPE_USER, .status = ZX_OK};
  zx_status_t status = port_.queue(&packet);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to trigger rx on port: %s", zx_status_get_string(status));
  }
}

void SerialPpp::NetworkDeviceImplPrepareVmo(uint8_t vmo_id, zx::vmo vmo) {
  zx_status_t status = vmos_.RegisterWithKey(vmo_id, std::move(vmo));
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to register VMO %d: %s", vmo_id, zx_status_get_string(status));
  }
}

void SerialPpp::NetworkDeviceImplReleaseVmo(uint8_t vmo_id) {
  zx_status_t status = vmos_.Unregister(vmo_id);
  if (status != ZX_OK) {
    zxlogf(WARNING, "failed to unregister VMO %d: %s", vmo_id, zx_status_get_string(status));
  }
}

void SerialPpp::NetworkDeviceImplSetSnoop(bool snoop) {
  // ignored, we're using auto-snoop
}

}  // namespace ppp

// clang-format off
ZIRCON_DRIVER_BEGIN(serial-ppp, ppp::driver_ops, "zircon", "0.1", 1)
  BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SERIAL),
ZIRCON_DRIVER_END(serial-ppp)
