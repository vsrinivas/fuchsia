// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_channel.h"

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/fdio/unsafe.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/event.h>
#include <lib/zx/time.h>
#include <lib/zx/timer.h>
#include <zircon/status.h>

#include <iostream>

#include "bt_intel.h"
#include "src/connectivity/bluetooth/core/bt-host/hci/slab_allocators.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace {

zx::channel GetCommandChannel(int fd) {
  fdio_t* hci_io = fdio_unsafe_fd_to_io(fd);
  assert(hci_io);

  zx::channel ours, theirs;
  zx_status_t status = zx::channel::create(0, &ours, &theirs);
  if (status != ZX_OK) {
    std::cerr << "CommandChannel: Failed to create channel: %s\n"
              << zx_status_get_string(status) << std::endl;
    return ours;
  }

  status = fuchsia_hardware_bluetooth_HciOpenCommandChannel(fdio_unsafe_borrow_channel(hci_io),
                                                            theirs.release());
  fdio_unsafe_release(hci_io);

  if (status != ZX_OK) {
    std::cerr << "hci: Failed to obtain command channel handle: " << zx_status_get_string(status)
              << std::endl;
    assert(!ours.is_valid());
  }
  return ours;
}

zx::channel GetAclChannel(int fd) {
  fdio_t* hci_io = fdio_unsafe_fd_to_io(fd);
  assert(hci_io);

  zx::channel ours, theirs;
  zx_status_t status = zx::channel::create(0, &ours, &theirs);
  if (status != ZX_OK) {
    std::cerr << "CommandChannel: Failed to create channel: %s\n"
              << zx_status_get_string(status) << std::endl;
    return ours;
  }

  status = fuchsia_hardware_bluetooth_HciOpenAclDataChannel(fdio_unsafe_borrow_channel(hci_io),
                                                            theirs.release());
  fdio_unsafe_release(hci_io);

  if (status != ZX_OK) {
    std::cerr << "hci: Failed to obtain ACL channel handle: " << zx_status_get_string(status)
              << std::endl;
    assert(!ours.is_valid());
  }
  return ours;
}

}  // namespace

CommandChannel::CommandChannel(std::string hcidev_path) : valid_(false), event_callback_(nullptr) {
  hci_fd_.reset(open(hcidev_path.c_str(), O_RDWR));
  if (!bool(hci_fd_)) {
    return;
  }
  cmd_channel_ = GetCommandChannel(hci_fd_.get());
  cmd_channel_wait_.set_object(cmd_channel_.get());
  cmd_channel_wait_.set_trigger(ZX_CHANNEL_READABLE);
  zx_status_t status = cmd_channel_wait_.Begin(async_get_default_dispatcher());
  if (status != ZX_OK) {
    std::cerr << "CommandChannel: problem setting up command channel: "
              << zx_status_get_string(status) << std::endl;
    return;
  }

  acl_channel_ = GetAclChannel(hci_fd_.get());
  acl_channel_wait_.set_object(acl_channel_.get());
  acl_channel_wait_.set_trigger(ZX_CHANNEL_READABLE);
  status = acl_channel_wait_.Begin(async_get_default_dispatcher());
  if (status != ZX_OK) {
    std::cerr << "CommandChannel: problem setting up ACL channel: " << zx_status_get_string(status)
              << std::endl;
    return;
  }

  valid_ = true;
}

CommandChannel::~CommandChannel() {
  SetEventCallback(nullptr);
  cmd_channel_wait_.Cancel();
  acl_channel_wait_.Cancel();
}

void CommandChannel::SetEventCallback(EventCallback callback) {
  event_callback_ = std::move(callback);
}

void CommandChannel::SendCommand(const ::bt::PacketView<::bt::hci::CommandHeader>& command) {
  zx::channel* channel = &cmd_channel_;
  // Bootloader Secure Send commands are sent and responded to via the bulk
  // endpoint (ACL channel)
  if (command.header().opcode == bt_intel::kSecureSend) {
    channel = &acl_channel_;
  }

  zx_status_t status = channel->write(0, command.data().data(), command.size(), nullptr, 0);
  if (status < 0) {
    std::cerr << "CommandChannel: Failed to send command: " << zx_status_get_string(status)
              << std::endl;
  }
}

void CommandChannel::SendCommandSync(const ::bt::PacketView<::bt::hci::CommandHeader>& command,
                                     EventCallback callback) {
  bool received = false;
  auto previous_cb = std::move(event_callback_);

  auto cb = [&received, callback = std::move(callback)](const auto& event_packet) {
    if (callback) {
      callback(event_packet);
    }
    received = true;
  };

  SetEventCallback(std::move(cb));
  SendCommand(command);

  zx_status_t status = ZX_OK;
  zx::timer timeout;
  zx::timer::create(ZX_TIMER_SLACK_LATE, ZX_CLOCK_MONOTONIC, &timeout);
  // Wait up to 500ms for a response.
  timeout.set(zx::deadline_after(zx::msec(500)), zx::msec(50));
  for (;;) {
    // TODO(fxbug.dev/1077): Don't use the message loop modally.
    async_loop_run(async_loop_from_dispatcher(async_get_default_dispatcher()),
                   zx_deadline_after(ZX_MSEC(10)), true);
    if (received) {
      status = ZX_OK;
      break;
    }

    status = timeout.wait_one(ZX_TIMER_SIGNALED, zx::time(), nullptr);
    if (status != ZX_ERR_TIMED_OUT) {
      if (status == ZX_OK) {
        status = ZX_ERR_TIMED_OUT;
      }
      break;
    }
  }

  SetEventCallback(std::move(previous_cb));

  if (status != ZX_OK) {
    std::cerr << "CommandChannel: error waiting for event " << zx_status_get_string(status)
              << std::endl;
  }
}

void CommandChannel::HandleChannelReady(const zx::channel& channel, async_dispatcher_t* dispatcher,
                                        async::WaitBase* wait, zx_status_t status,
                                        const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    std::cerr << "CommandChannel: channel error: " << zx_status_get_string(status) << std::endl;
    return;
  }

  FX_DCHECK(signal->observed & ZX_CHANNEL_READABLE);
  for (size_t count = 0; count < signal->count; count++) {
    uint32_t read_size;
    // Allocate a buffer for the event. Since we don't know the size
    // beforehand we allocate the largest possible buffer.
    auto packet = ::bt::hci::EventPacket::New(::bt::hci::slab_allocators::kLargeControlPayloadSize);
    if (!packet) {
      std::cerr << "CommandChannel: Failed to allocate event packet!" << std::endl;
      return;
    }
    auto packet_bytes = packet->mutable_view()->mutable_data();
    zx_status_t read_status = channel.read(0u, packet_bytes.mutable_data(), nullptr,
                                           packet_bytes.size(), 0, &read_size, nullptr);
    if (read_status < 0) {
      std::cerr << "CommandChannel: Failed to read event bytes: "
                << zx_status_get_string(read_status) << std::endl;
      // Clear the handler so that we stop receiving events from it.
      return;
    }

    if (read_size < sizeof(::bt::hci::EventHeader)) {
      std::cerr << "CommandChannel: Malformed event packet - "
                << "expected at least " << sizeof(::bt::hci::EventHeader) << " bytes, got "
                << read_size << std::endl;
      continue;
    }

    // Compare the received payload size to what is in the header.
    const size_t rx_payload_size = read_size - sizeof(::bt::hci::EventHeader);
    const size_t size_from_header = packet->view().header().parameter_total_size;
    if (size_from_header != rx_payload_size) {
      std::cerr << "CommandChannel: Malformed event packet - "
                << "payload size from header (" << size_from_header << ")"
                << " does not match received payload size: " << rx_payload_size << std::endl;
      continue;
    }

    packet->InitializeFromBuffer();

    if (event_callback_) {
      event_callback_(*packet);
    } else {
      std::cerr << fxl::StringPrintf("CommandChannel: Event received with no handler: 0x%02x",
                                     packet->event_code())
                << std::endl;
    }
  }

  status = wait->Begin(dispatcher);
  if (status != ZX_OK) {
    std::cerr << "CommandChannel: resume wait error: " << zx_status_get_string(status) << std::endl;
  }
}

void CommandChannel::OnCmdChannelReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                       zx_status_t status, const zx_packet_signal_t* signal) {
  HandleChannelReady(cmd_channel_, dispatcher, wait, status, signal);
}

void CommandChannel::OnAclChannelReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                       zx_status_t status, const zx_packet_signal_t* signal) {
  // A Command packet response from a Secure Send command.
  HandleChannelReady(acl_channel_, dispatcher, wait, status, signal);
}
