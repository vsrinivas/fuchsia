// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "serial-ppp.h"

#include <lib/fidl-async/cpp/bind.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/hw/usb.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddktl/fidl.h>

#include "lib/common/ppp.h"
#include "lib/fit/result.h"
#include "lib/hdlc/frame.h"

namespace ppp {

namespace fppp = llcpp::fuchsia::net::ppp;

static constexpr zx_driver_ops_t driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = SerialPpp::Create,
};

static constexpr uint16_t kDefaultMru = 1500;
static constexpr size_t kMaxBufferSize = 2 * kDefaultMru + 8;

static constexpr size_t kMaxQueueSize = 32;
static constexpr zx::duration kSerialTimeout = zx::msec(10);

static constexpr fppp::Info kDefaultInfo = {
    .mtu = kDefaultMru,
};

SerialPpp::SerialPpp() : DeviceType(nullptr) {}

SerialPpp::SerialPpp(zx_device_t* parent) : DeviceType(parent), serial_protocol_(parent) {}

zx_status_t SerialPpp::Create(void* /*ctx*/, zx_device_t* parent) {
  auto dev = std::make_unique<SerialPpp>(parent);

  auto status = dev->Init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Init failed", __func__);
    return status;
  }

  dev->DdkAdd("ppp");

  // Release because devmgr is now in charge of the device.
  static_cast<void>(dev.release());
  return ZX_OK;
}

zx_status_t SerialPpp::Init() { return loop_.StartThread(); }

void SerialPpp::WaitCallbacks() {
  while (true) {
    {
      std::lock_guard<std::mutex> guard(ipv4_mutex_);
      if (!ipv4_callback_) {
        break;
      }
    }
  }
  while (true) {
    {
      std::lock_guard<std::mutex> guard(ipv6_mutex_);
      if (!ipv6_callback_) {
        break;
      }
    }
  }
  while (true) {
    {
      std::lock_guard<std::mutex> guard(control_mutex_);
      if (!control_callback_) {
        break;
      }
    }
  }
}

void SerialPpp::DdkRelease() {
  loop_.Shutdown();
  Enable(false);
  {
    std::lock_guard<std::mutex> guard(ipv4_mutex_);
    if (ipv4_callback_) {
      ipv4_callback_(fit::error(ZX_ERR_CANCELED));
    }
  }
  {
    std::lock_guard<std::mutex> guard(ipv6_mutex_);
    if (ipv6_callback_) {
      ipv6_callback_(fit::error(ZX_ERR_CANCELED));
    }
  }
  {
    std::lock_guard<std::mutex> guard(control_mutex_);
    if (control_callback_) {
      control_callback_(fit::error(ZX_ERR_CANCELED));
    }
  }
  delete this;
}

void SerialPpp::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

zx_status_t SerialPpp::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fppp::DeviceBootstrap::Dispatch(&bootstrap_server_, msg, &transaction);
  return transaction.Status();
}

void SerialPpp::Rx(fppp::ProtocolType protocol,
                   fit::callback<void(fit::result<Frame, zx_status_t>)> callback) {
  switch (protocol) {
    case fppp::ProtocolType::IPV4: {
      std::lock_guard<std::mutex> guard(ipv4_mutex_);
      if (ipv4_callback_) {
        callback(fit::error(ZX_ERR_SHOULD_WAIT));
        return;
      }
      if (!ipv4_up_) {
        callback(fit::error(ZX_ERR_NOT_CONNECTED));
        return;
      }
      ipv4_callback_ = std::move(callback);
      return;
    }
    case fppp::ProtocolType::IPV6: {
      std::lock_guard<std::mutex> guard(ipv6_mutex_);
      if (ipv6_callback_) {
        callback(fit::error(ZX_ERR_SHOULD_WAIT));
        return;
      }
      if (!ipv6_up_) {
        callback(fit::error(ZX_ERR_NOT_CONNECTED));
        return;
      }
      ipv6_callback_ = std::move(callback);
      return;
    }
    case fppp::ProtocolType::CONTROL: {
      std::lock_guard<std::mutex> guard(control_mutex_);
      if (control_callback_) {
        callback(fit::error(ZX_ERR_SHOULD_WAIT));
        return;
      }
      control_callback_ = std::move(callback);
      return;
    }
    default:
      callback(fit::error(ZX_ERR_INVALID_ARGS));
      return;
  }
}

zx_status_t SerialPpp::Tx(fppp::ProtocolType protocol, fbl::Span<const uint8_t> data) {
  switch (protocol) {
    case fppp::ProtocolType::IPV4: {
      if (!ipv4_up_) {
        return ZX_ERR_NOT_CONNECTED;
      }
      return WriteFramed(FrameView{Protocol::Ipv4, data});
    }
    case fppp::ProtocolType::IPV6: {
      if (!ipv6_up_) {
        return ZX_ERR_NOT_CONNECTED;
      }
      return WriteFramed(FrameView{Protocol::Ipv6, data});
    }
    case fppp::ProtocolType::CONTROL:
      if (data.size() >= 2) {
        // For ProtocolType::Control, the first two bytes of data encode (in network
        // byte order) the PPP control protocol.
        const auto protocol = static_cast<Protocol>(data[0] << 8 | data[1]);
        return WriteFramed(FrameView(protocol, data.subspan(2)));
      } else {
        return ZX_ERR_OUT_OF_RANGE;
      }
    default:
      return ZX_ERR_INVALID_ARGS;
  }
}

fppp::Info SerialPpp::GetInfo() { return kDefaultInfo; }

bool SerialPpp::GetStatus(fppp::ProtocolType protocol) {
  switch (protocol) {
    case fppp::ProtocolType::CONTROL:
      return true;
    case fppp::ProtocolType::IPV4:
      return ipv4_up_;
    case fppp::ProtocolType::IPV6:
      return ipv6_up_;
    default:
      return false;
  }
}

void SerialPpp::SetStatus(fppp::ProtocolType protocol, bool up) {
  switch (protocol) {
    case fppp::ProtocolType::IPV4: {
      ipv4_up_ = up;
      if (!up) {
        std::lock_guard<std::mutex> guard(ipv4_mutex_);
        // std::queue doesn't have .clear().
        ipv4_frames_ = {};
      }
      break;
    }
    case fppp::ProtocolType::IPV6: {
      ipv6_up_ = up;
      if (!up) {
        std::lock_guard<std::mutex> guard(ipv6_mutex_);
        ipv6_frames_ = {};
      }
      break;
    }
    default:
      break;
  }
}

zx_status_t SerialPpp::Enable(bool up) {
  if (enabled_ && !up) {
    // down
    serial_.reset();
    enabled_ = false;
    rx_thread_.join();
  } else if (!enabled_ && up) {
    // up
    auto status = serial_protocol_.OpenSocket(&serial_);
    if (status != ZX_OK) {
      return status;
    }
    enabled_ = true;
    rx_thread_ = std::thread([&] { RxLoop(); });
  }
  return ZX_OK;
}

zx_status_t SerialPpp::Enable(bool up, zx::socket socket) {
  if (enabled_ && !up) {
    // down
    serial_.reset();
    enabled_ = false;
    rx_thread_.join();
  } else if (!enabled_ && up) {
    // up
    serial_ = std::move(socket);
    enabled_ = true;
    rx_thread_ = std::thread([&] { RxLoop(); });
  }
  return ZX_OK;
}

zx::channel SerialPpp::GetInstance() {
  zx::channel local;
  zx::channel remote;
  zx::channel::create(0, &local, &remote);
  auto status = fidl::BindSingleInFlightOnly(loop_.dispatcher(), std::move(local), &server_);
  if (status != ZX_OK) {
    return zx::channel();
  }
  return remote;
}

SerialPpp::FrameDeviceServer::FrameDeviceServer(SerialPpp* dev) : dev_(dev) {}

void SerialPpp::FrameDeviceServer::Rx(fppp::ProtocolType protocol, RxCompleter::Sync completer) {
  auto callback = [completer = completer.ToAsync()](auto frame) mutable {
    fppp::Device_Rx_Result result;

    if (frame.is_ok()) {
      fppp::Device_Rx_Response response;
      auto [protocol, data] = frame.take_value();
      response.data = fidl::VectorView<uint8_t>(fidl::unowned_ptr(data.data()), data.size());
      result.set_response(fidl::unowned_ptr(&response));
      completer.Reply(std::move(result));
    } else if (frame.is_error()) {
      zx_status_t status = frame.take_error();
      result.set_err(fidl::unowned_ptr(&status));
      completer.Reply(std::move(result));
    } else {
      zx_status_t status = ZX_ERR_INTERNAL;
      result.set_err(fidl::unowned_ptr(&status));
      completer.Reply(std::move(result));
    }
  };
  dev_->Rx(protocol, std::move(callback));
}

void SerialPpp::FrameDeviceServer::Tx(fppp::ProtocolType protocol, fidl::VectorView<uint8_t> data,
                                      TxCompleter::Sync completer) {
  auto status = dev_->Tx(protocol, fbl::Span(data.data(), data.count()));
  fppp::Device_Tx_Result result;

  fidl::aligned<fppp::Device_Tx_Response> response;
  if (status == ZX_OK) {
    result.set_response(fidl::unowned_ptr(&response));
  } else {
    result.set_err(fidl::unowned_ptr(&status));
  }

  completer.Reply(std::move(result));
}

void SerialPpp::FrameDeviceServer::GetInfo(GetInfoCompleter::Sync completer) {
  completer.Reply(dev_->GetInfo());
}

void SerialPpp::FrameDeviceServer::GetStatus(fppp::ProtocolType protocol,
                                             GetStatusCompleter::Sync completer) {
  completer.Reply(dev_->GetStatus(protocol));
}

void SerialPpp::FrameDeviceServer::SetStatus(fppp::ProtocolType protocol, bool up,
                                             SetStatusCompleter::Sync /*completer*/) {
  dev_->SetStatus(protocol, up);
}

void SerialPpp::FrameDeviceServer::Enable(bool up, EnableCompleter::Sync completer) {
  auto status = dev_->Enable(up);
  fppp::Device_Enable_Result result;

  fidl::aligned<fppp::Device_Enable_Response> response;
  if (status == ZX_OK) {
    result.set_response(fidl::unowned_ptr(&response));
  } else {
    result.set_err(fidl::unowned_ptr(&status));
  }

  completer.Reply(std::move(result));
}

SerialPpp::FrameDeviceBootstrapServer::FrameDeviceBootstrapServer(SerialPpp* dev) : dev_(dev) {}

void SerialPpp::FrameDeviceBootstrapServer::GetInstance(GetInstanceCompleter::Sync completer) {
  completer.Reply(dev_->GetInstance());
}

void SerialPpp::RxLoop() {
  std::vector<uint8_t> raw_frame;
  raw_frame.reserve(kMaxBufferSize);
  while (enabled_) {
    {
      std::lock_guard<std::mutex> guard(ipv4_mutex_);
      if (ipv4_callback_ && !ipv4_frames_.empty()) {
        auto frame = std::move(ipv4_frames_.front());
        ipv4_frames_.pop();
        ipv4_callback_(fit::ok(std::move(frame)));
      }
    }
    {
      std::lock_guard<std::mutex> guard(ipv6_mutex_);
      if (ipv6_callback_ && !ipv6_frames_.empty()) {
        auto frame = std::move(ipv6_frames_.front());
        ipv6_frames_.pop();
        ipv6_callback_(fit::ok(std::move(frame)));
      }
    }
    {
      std::lock_guard<std::mutex> guard(control_mutex_);
      if (control_callback_ && !control_frames_.empty()) {
        auto frame = std::move(control_frames_.front());
        control_frames_.pop();
        control_callback_(fit::ok(std::move(frame)));
      }
    }

    auto maybe_frame = ReadFramed(&raw_frame);
    if (!maybe_frame.is_ok()) {
      continue;
    }
    auto frame = maybe_frame.take_value();
    switch (frame.protocol) {
      case Protocol::Ipv4: {
        if (!ipv4_up_) {
          break;
        }
        {
          std::lock_guard<std::mutex> guard(ipv4_mutex_);
          ipv4_frames_.push(std::move(frame));
          if (ipv4_frames_.size() > kMaxQueueSize) {
            ipv4_frames_.pop();
          }
        }
        break;
      }
      case Protocol::Ipv6: {
        if (!ipv6_up_) {
          break;
        }
        {
          std::lock_guard<std::mutex> guard(ipv6_mutex_);
          ipv6_frames_.push(std::move(frame));
          if (ipv6_frames_.size() > kMaxQueueSize) {
            ipv6_frames_.pop();
          }
        }
        break;
      }
      default: {
        const auto protocol_upper =
            static_cast<uint8_t>(static_cast<uint16_t>(frame.protocol) >> 8);
        const auto protocol_lower = static_cast<uint8_t>(frame.protocol);
        frame.information.insert(frame.information.begin(), {protocol_upper, protocol_lower});
        {
          std::lock_guard<std::mutex> guard(control_mutex_);
          control_frames_.push(std::move(frame));
          if (control_frames_.size() > kMaxQueueSize) {
            control_frames_.pop();
          }
        }
        break;
      }
    }
  }
}

fit::result<Frame, zx_status_t> SerialPpp::ReadFramed(std::vector<uint8_t>* raw_frame) {
  raw_frame->clear();
  raw_frame->push_back(kFlagSequence);
  while (raw_frame->size() != kMaxBufferSize) {
    uint8_t b = kFlagSequence;
    size_t actual = 0;
    const auto status = serial_.read(0, &b, 1, &actual);
    if (status == ZX_ERR_SHOULD_WAIT) {
      const auto wait_status =
          serial_.wait_one(ZX_SOCKET_READABLE, zx::deadline_after(kSerialTimeout), nullptr);
      if (wait_status != ZX_OK) {
        return fit::error(wait_status);
      }
    } else if (status != ZX_OK) {
      return fit::error(status);
    }

    if (b == kFlagSequence) {
      if (raw_frame->back() != kFlagSequence) {
        raw_frame->push_back(b);
        break;
      }
    } else {
      raw_frame->push_back(b);
    }
  }

  auto result = DeserializeFrame(*raw_frame);

  if (result.is_ok()) {
    return result.take_ok_result();
  }

  return fit::error(ZX_ERR_IO);
}

zx_status_t SerialPpp::WriteFramed(FrameView frame) {
  std::lock_guard<std::mutex> lock_guard(write_mutex_);
  if (frame.information.size() >= kDefaultMru) {
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

}  // namespace ppp

// clang-format off
ZIRCON_DRIVER_BEGIN(serial-ppp, ppp::driver_ops, "zircon", "0.1", 1)
  BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SERIAL),
ZIRCON_DRIVER_END(serial-ppp)
