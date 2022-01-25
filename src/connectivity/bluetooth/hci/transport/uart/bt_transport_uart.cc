// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bt_transport_uart.h"

#include <assert.h>
#include <fuchsia/hardware/bt/hci/c/banjo.h>
#include <fuchsia/hardware/serial/c/fidl.h>
#include <fuchsia/hardware/serialimpl/async/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/device/bt-hci.h>
#include <zircon/status.h>

#include "src/connectivity/bluetooth/hci/transport/uart/bt_transport_uart_bind.h"

namespace bt_transport_uart {

BtTransportUart::BtTransportUart(zx_device_t* parent, async_dispatcher_t* dispatcher)
    : BtTransportUartType(parent), dispatcher_(dispatcher) {}

zx_status_t BtTransportUart::Create(void* /*ctx*/, zx_device_t* parent) {
  return Create(parent, /*dispatcher=*/nullptr);
}

zx_status_t BtTransportUart::Create(zx_device_t* parent, async_dispatcher_t* dispatcher) {
  std::unique_ptr<BtTransportUart> dev = std::make_unique<BtTransportUart>(parent, dispatcher);

  zx_status_t bind_status = dev->Bind();
  if (bind_status != ZX_OK) {
    return bind_status;
  }

  // Driver Manager is now in charge of the device.
  // Memory will be explicitly freed in DdkRelease().
  __UNUSED BtTransportUart* unused = dev.release();
  return ZX_OK;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
BtTransportUart::Wait::Wait(BtTransportUart* uart, zx::channel* channel) {
  this->state = ASYNC_STATE_INIT;
  this->handler = Handler;
  this->object = ZX_HANDLE_INVALID;
  this->trigger = ZX_SIGNAL_NONE;
  this->options = 0;
  this->uart = uart;
  this->channel = channel;
}

void BtTransportUart::Wait::Handler(async_dispatcher_t* dispatcher, async_wait_t* async_wait,
                                    zx_status_t status, const zx_packet_signal_t* signal) {
  auto wait = static_cast<Wait*>(async_wait);
  wait->uart->OnChannelSignal(wait, status, signal);
}

size_t BtTransportUart::EventPacketLength() {
  // payload length is in byte 2 of the packet
  // add 3 bytes for packet indicator, event code and length byte
  return event_buffer_offset_ > 2 ? event_buffer_[2] + 3 : 0;
}

size_t BtTransportUart::AclPacketLength() {
  // length is in bytes 3 and 4 of the packet
  // add 5 bytes for packet indicator, control info and length fields
  return acl_buffer_offset_ > 4 ? (acl_buffer_[3] | (acl_buffer_[4] << 8)) + 5 : 0;
}

void BtTransportUart::ChannelCleanupLocked(zx::channel* channel) {
  if (!channel->is_valid()) {
    return;
  }

  if (channel == &cmd_channel_ && cmd_channel_wait_.pending) {
    async_cancel_wait(dispatcher_, &cmd_channel_wait_);
    cmd_channel_wait_.pending = false;
  } else if (channel == &acl_channel_ && acl_channel_wait_.pending) {
    async_cancel_wait(dispatcher_, &acl_channel_wait_);
    acl_channel_wait_.pending = false;
  }
  channel->reset();
}

void BtTransportUart::SnoopChannelWriteLocked(uint8_t flags, uint8_t* bytes, size_t length) {
  if (!snoop_channel_.is_valid()) {
    return;
  }

  // We tack on a flags byte to the beginning of the payload.
  uint8_t snoop_buffer[length + 1];
  snoop_buffer[0] = flags;
  memcpy(snoop_buffer + 1, bytes, length);

  zx_status_t status = zx_channel_write(snoop_channel_.get(), 0, snoop_buffer,
                                        static_cast<uint32_t>(length + 1), nullptr, 0);
  if (status != ZX_OK) {
    if (status != ZX_ERR_PEER_CLOSED) {
      zxlogf(ERROR, "bt-transport-uart: failed to write to snoop channel: %s",
             zx_status_get_string(status));
    }

    // It should be safe to clean up the channel right here as the work thread
    // never waits on this channel from outside of the lock.
    ChannelCleanupLocked(&snoop_channel_);
  }
}

void BtTransportUart::HciBeginShutdown() {
  bool was_shutting_down = shutting_down_.exchange(true, std::memory_order_relaxed);
  if (!was_shutting_down) {
    DdkAsyncRemove();
  }
}

// Takes ownership of buffer.
void BtTransportUart::SerialWrite(void* buffer, size_t length) {
  {
    std::lock_guard guard(mutex_);
    ZX_DEBUG_ASSERT(can_write_);
    // Clear the can_write flag.  The UART can currently only handle one in flight
    // transaction at a time.
    can_write_ = false;
  }

  HciWriteCtx* cookie = static_cast<HciWriteCtx*>(malloc(sizeof(HciWriteCtx)));
  cookie->ctx = this;
  cookie->buffer = static_cast<uint8_t*>(buffer);

  // write_cb takes ownership of cookie & buffer when called.
  serial_impl_async_write_async_callback write_cb = [](void* cookie, zx_status_t status) {
    HciWriteCtx* write_ctx = static_cast<HciWriteCtx*>(cookie);
    write_ctx->ctx->HciWriteComplete(status);
    free(write_ctx->buffer);
    free(write_ctx);
  };

  // Takes ownership of buffer & cookie.
  serial_impl_async_write_async(&serial_, static_cast<uint8_t*>(buffer), length, write_cb, cookie);
}

// Returns false if there's an error while sending the packet to the hardware or
// if the channel peer closed its endpoint.
void BtTransportUart::HciHandleClientChannel(zx::channel* chan, zx_signals_t pending) {
  // Channel may have been closed since signal was received.
  if (!chan->is_valid()) {
    return;
  }

  // Figure out which channel we are dealing with and the constants which go
  // along with it.
  uint32_t max_buf_size;
  BtHciPacketIndicator packet_type;
  bt_hci_snoop_type_t snoop_type;

  if (chan == &cmd_channel_) {
    max_buf_size = kCmdBufSize;
    packet_type = kHciCommand;
    snoop_type = BT_HCI_SNOOP_TYPE_CMD;
  } else if (chan == &acl_channel_) {
    max_buf_size = kAclMaxFrameSize;
    packet_type = kHciAclData;
    snoop_type = BT_HCI_SNOOP_TYPE_ACL;
  } else {
    // This should never happen, we only know about two packet types currently.
    ZX_ASSERT(false);
    return;
  }

  // Handle the read signal first.  If we are also peer closed, we want to make
  // sure that we have processed all of the pending messages before cleaning up.
  if (pending & ZX_CHANNEL_READABLE) {
    zxlogf(TRACE, "received readable signal for %s channel",
           chan == &cmd_channel_ ? "command" : "ACL");
    uint32_t length = max_buf_size - 1;
    uint8_t* buf;
    {
      std::lock_guard guard(mutex_);

      // Do not proceed if we are not allowed to write.  Let the work thread call
      // us back again when it is safe to write.
      if (!can_write_) {
        return;
      }

      zx_status_t status;
      buf = static_cast<uint8_t*>(malloc(length + 1));

      status = zx_channel_read(chan->get(), 0, buf + 1, nullptr, length, 0, &length, nullptr);
      if (status != ZX_OK) {
        zxlogf(ERROR, "hci_read_thread: failed to read from %s channel %s",
               (packet_type == kHciCommand) ? "CMD" : "ACL", zx_status_get_string(status));
        free(buf);
        ChannelCleanupLocked(chan);
        return;
      }

      buf[0] = packet_type;
      length++;

      SnoopChannelWriteLocked(bt_hci_snoop_flags(snoop_type, false), buf + 1, length - 1);
    }

    SerialWrite(buf, length);
  }

  if (pending & ZX_CHANNEL_PEER_CLOSED) {
    zxlogf(DEBUG, "received closed signal for %s channel",
           chan == &cmd_channel_ ? "command" : "ACL");
    std::lock_guard guard(mutex_);
    ChannelCleanupLocked(chan);
  }
}

void BtTransportUart::HciHandleUartReadEvents(const uint8_t* buf, size_t length) {
  const uint8_t* src = buf;
  const uint8_t* const end = src + length;
  BtHciPacketIndicator packet_type = cur_uart_packet_type_;

  while (src < end) {
    if (packet_type == kHciNone) {
      // start of new packet. read packet type
      packet_type = static_cast<BtHciPacketIndicator>(*src++);
      if (packet_type != kHciEvent && packet_type != kHciAclData) {
        zxlogf(INFO, "unsupported HCI packet type %u. We may be out of sync", packet_type);
        return;
      }
    }

    if (packet_type == kHciEvent) {
      size_t packet_length = EventPacketLength();

      while (!packet_length && src < end) {
        // read until we have enough to compute packet length
        event_buffer_[event_buffer_offset_++] = *src++;
        packet_length = EventPacketLength();
      }
      if (!packet_length) {
        break;
      }

      size_t remaining = end - src;
      size_t copy = packet_length - event_buffer_offset_;
      if (copy > remaining) {
        copy = remaining;
      }
      ZX_ASSERT((event_buffer_offset_ + copy) <= sizeof(event_buffer_));
      memcpy(event_buffer_ + event_buffer_offset_, src, copy);
      src += copy;
      event_buffer_offset_ += copy;

      if (event_buffer_offset_ == packet_length) {
        std::lock_guard guard(mutex_);

        // Attempt to send this packet to our cmd channel.  We are working on the callback thread
        // from the UART, so we need to do this inside of the lock to make sure that nothing closes
        // the channel out from under us while we try to write.  If something goes wrong here, close
        // the channel.
        if (cmd_channel_.is_valid()) {
          // send accumulated event packet, minus the packet indicator
          zx_status_t status = zx_channel_write(cmd_channel_.get(), 0, &event_buffer_[1],
                                                packet_length - 1, nullptr, 0);
          if (status != ZX_OK) {
            zxlogf(ERROR, "bt-transport-uart: failed to write CMD packet: %s",
                   zx_status_get_string(status));
            ChannelCleanupLocked(&cmd_channel_);
          }
        }

        SnoopChannelWriteLocked(bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_EVT, true), &event_buffer_[1],
                                packet_length - 1);

        // reset buffer
        packet_type = kHciNone;
        event_buffer_offset_ = 1;
      }
    } else {  // HCI_ACL_DATA
      size_t packet_length = AclPacketLength();

      while (!packet_length && src < end) {
        // read until we have enough to compute packet length
        acl_buffer_[acl_buffer_offset_++] = *src++;
        packet_length = AclPacketLength();
      }

      // Out of bytes, but we still don't know the packet length.  Just wait for
      // the next packet.
      if (!packet_length) {
        break;
      }

      // Sanity check out packet length.  The value computed by
      // ACL_PACKET_LENGTH includes not only the packet payload size (as read
      // from the packet itself), but also the 5 bytes of packet overhead.  We
      // should be able to simply check packet_length against the size of the
      // reassembly buffer.
      if (packet_length > sizeof(acl_buffer_)) {
        zxlogf(ERROR,
               "bt-transport-uart: packet_length is too large (%zu > %zu) during ACL packet "
               "reassembly.  Dropping and attempting to re-sync.\n",
               packet_length, sizeof(acl_buffer_offset_));

        // reset the reassembly state machine.
        packet_type = kHciNone;
        acl_buffer_offset_ = 1;
        break;
      }

      size_t remaining = end - src;
      size_t copy = packet_length - acl_buffer_offset_;
      if (copy > remaining) {
        copy = remaining;
      }

      ZX_ASSERT((acl_buffer_offset_ + copy) <= sizeof(acl_buffer_));
      memcpy(acl_buffer_ + acl_buffer_offset_, src, copy);
      src += copy;
      acl_buffer_offset_ += copy;

      if (acl_buffer_offset_ == packet_length) {
        std::lock_guard guard(mutex_);

        // Attempt to send accumulated ACL data packet, minus the packet
        // indicator.  See the notes in the cmd channel section for details
        // about error handling and why the lock is needed here
        if (acl_channel_.is_valid()) {
          zx_status_t status = zx_channel_write(acl_channel_.get(), 0, &acl_buffer_[1],
                                                packet_length - 1, nullptr, 0);

          if (status != ZX_OK) {
            zxlogf(ERROR, "bt-transport-uart: failed to write ACL packet: %s",
                   zx_status_get_string(status));
            ChannelCleanupLocked(&acl_channel_);
          }
        }

        // If the snoop channel is open then try to write the packet
        // even if acl_channel was closed.
        SnoopChannelWriteLocked(bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_ACL, true), &acl_buffer_[1],
                                packet_length - 1);

        // reset buffer
        packet_type = kHciNone;
        acl_buffer_offset_ = 1;
      }
    }
  }

  cur_uart_packet_type_ = packet_type;
}

void BtTransportUart::HciReadComplete(zx_status_t status, const uint8_t* buffer, size_t length) {
  zxlogf(TRACE, "Read complete with status: %s", zx_status_get_string(status));

  // If we are in the process of shutting down, we are done.
  if (atomic_load_explicit(&shutting_down_, std::memory_order_relaxed)) {
    return;
  }

  if (status == ZX_OK) {
    HciHandleUartReadEvents(buffer, length);
    serial_impl_async_read_async_callback read_cb = [](void* ctx, zx_status_t status,
                                                       const uint8_t* buffer, size_t length) {
      static_cast<BtTransportUart*>(ctx)->HciReadComplete(status, buffer, length);
    };
    serial_impl_async_read_async(&serial_, read_cb, this);
  } else {
    // There is not much we can do in the event of a UART read error.  Do not
    // queue a read job and start the process of shutting down.
    zxlogf(ERROR, "Fatal UART read error (%s), shutting down", zx_status_get_string(status));
    HciBeginShutdown();
  }
}

void BtTransportUart::HciWriteComplete(zx_status_t status) {
  zxlogf(TRACE, "Write complete with status: %s", zx_status_get_string(status));

  // If we are in the process of shutting down, we are done as soon as we
  // have freed our operation.
  if (atomic_load_explicit(&shutting_down_, std::memory_order_relaxed)) {
    return;
  }

  if (status != ZX_OK) {
    HciBeginShutdown();
    return;
  }

  // We can write now.
  {
    std::lock_guard guard(mutex_);
    can_write_ = true;

    // Resume waiting for channel signals. If a packet was queued while the write was processing, it
    // should be immediately signaled.
    if (cmd_channel_wait_.channel->is_valid() && !cmd_channel_wait_.pending) {
      ZX_ASSERT(async_begin_wait(dispatcher_, &cmd_channel_wait_) == ZX_OK);
      cmd_channel_wait_.pending = true;
    }
    if (acl_channel_wait_.channel->is_valid() && !acl_channel_wait_.pending) {
      ZX_ASSERT(async_begin_wait(dispatcher_, &acl_channel_wait_) == ZX_OK);
      acl_channel_wait_.pending = true;
    }
  }
}

void BtTransportUart::OnChannelSignal(Wait* wait, zx_status_t status,
                                      const zx_packet_signal_t* signal) {
  {
    std::lock_guard guard(mutex_);
    wait->pending = false;
  }

  HciHandleClientChannel(wait->channel, signal->observed);

  // The readable signal wait will be re-enabled in the write completion callback.
}

zx_status_t BtTransportUart::HciOpenChannel(zx::channel* in_channel, zx_handle_t in) {
  std::lock_guard guard(mutex_);
  zx_status_t result = ZX_OK;

  if (in_channel->is_valid()) {
    zxlogf(ERROR, "bt-transport-uart: already bound, failing");
    result = ZX_ERR_ALREADY_BOUND;
    return result;
  }

  in_channel->reset(in);

  Wait* wait = nullptr;
  if (in_channel == &cmd_channel_) {
    zxlogf(DEBUG, "opening command channel");
    wait = &cmd_channel_wait_;
  } else if (in_channel == &acl_channel_) {
    zxlogf(DEBUG, "opening ACL channel");
    wait = &acl_channel_wait_;
  } else if (in_channel == &snoop_channel_) {
    zxlogf(DEBUG, "opening snoop channel");
    // TODO(fxb/91348): Handle snoop channel closed signal.
    return ZX_OK;
  }
  ZX_ASSERT(wait);
  wait->object = in_channel->get();
  wait->trigger = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
  ZX_ASSERT(async_begin_wait(dispatcher_, wait) == ZX_OK);
  wait->pending = true;
  return result;
}

void BtTransportUart::DdkUnbind(ddk::UnbindTxn txn) {
  zxlogf(TRACE, "Unbind");

  // We are now shutting down.  Make sure that any pending callbacks in
  // flight from the serial_impl are nerfed and that our thread is shut down.
  std::atomic_store_explicit(&shutting_down_, true, std::memory_order_relaxed);

  {
    std::lock_guard guard(mutex_);

    // Close the transport channels so that the host stack is notified of device
    // removal and tasks aren't posted to work thread.
    ChannelCleanupLocked(&cmd_channel_);
    ChannelCleanupLocked(&acl_channel_);
    ChannelCleanupLocked(&snoop_channel_);
  }

  if (loop_) {
    loop_->Quit();
    loop_->JoinThreads();
  }

  // Finish by making sure that all in flight transactions transactions have
  // been canceled.
  serial_impl_async_cancel_all(&serial_);

  zxlogf(TRACE, "Unbind complete");

  // Tell the DDK we are done unbinding.
  txn.Reply();
}

void BtTransportUart::DdkRelease() {
  zxlogf(TRACE, "Release");
  // Driver manager is given a raw pointer to this dynamically allocated object in Create(), so when
  // DdkRelease() is called we need to free the allocated memory.
  delete this;
}

zx_status_t BtTransportUart::BtHciOpenCommandChannel(zx::channel in) {
  return HciOpenChannel(&cmd_channel_, in.release());
}

zx_status_t BtTransportUart::BtHciOpenAclDataChannel(zx::channel in) {
  return HciOpenChannel(&acl_channel_, in.release());
}

zx_status_t BtTransportUart::BtHciOpenSnoopChannel(zx::channel in) {
  return HciOpenChannel(&snoop_channel_, in.release());
}

zx_status_t BtTransportUart::DdkGetProtocol(uint32_t proto_id, void* out_proto) {
  if (proto_id != ZX_PROTOCOL_BT_HCI) {
    // Pass this on for drivers to load firmware / initialize
    return device_get_protocol(parent(), proto_id, out_proto);
  }

  bt_hci_protocol_t* hci_proto = static_cast<bt_hci_protocol*>(out_proto);
  hci_proto->ops = &bt_hci_protocol_ops_;
  hci_proto->ctx = this;
  return ZX_OK;
}

zx_status_t BtTransportUart::Bind() {
  zxlogf(DEBUG, "Bind");

  serial_impl_async_protocol_t serial;

  zx_status_t status = device_get_protocol(parent(), ZX_PROTOCOL_SERIAL_IMPL_ASYNC, &serial);
  if (status != ZX_OK) {
    zxlogf(ERROR, "bt-transport-uart: get protocol ZX_PROTOCOL_SERIAL failed: %s",
           zx_status_get_string(status));
    return status;
  }

  {
    std::lock_guard guard(mutex_);

    serial_ = serial;

    // pre-populate event packet indicators
    event_buffer_[0] = kHciEvent;
    event_buffer_offset_ = 1;
    acl_buffer_[0] = kHciAclData;
    acl_buffer_offset_ = 1;
  }

  serial_port_info_t info;
  status = serial_impl_async_get_info(&serial, &info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "hci_bind: serial_get_info failed: %s", zx_status_get_string(status));
    return status;
  }

  if (info.serial_class != fuchsia_hardware_serial_Class_BLUETOOTH_HCI) {
    zxlogf(ERROR, "hci_bind: info.device_class != BLUETOOTH_HCI");
    return ZX_ERR_INTERNAL;
  }

  serial_impl_async_enable(&serial, true);

  serial_impl_async_read_async_callback read_cb = [](void* ctx, zx_status_t status,
                                                     const uint8_t* buffer, size_t length) {
    static_cast<BtTransportUart*>(ctx)->HciReadComplete(status, buffer, length);
  };
  serial_impl_async_read_async(&serial_, read_cb, this);

  // Spawn a new thread in production. In tests, use the test dispatcher provided in the
  // constructor.
  if (!dispatcher_) {
    loop_.emplace(&kAsyncLoopConfigNoAttachToCurrentThread);
    status = loop_->StartThread("bt-transport-uart");
    if (status != ZX_OK) {
      zxlogf(ERROR, "failed to start thread: %s", zx_status_get_string(status));
      DdkRelease();
      return status;
    }
    dispatcher_ = loop_->dispatcher();
  }

  zxlogf(DEBUG, "Bind complete, adding device");

  ddk::DeviceAddArgs args("bt-transport-uart");
  // Copy the PID and VID from the platform device info so it can be filtered on
  // for HCI drivers
  zx_device_prop_t props[] = {
      {.id = BIND_PROTOCOL, .reserved = 0, .value = ZX_PROTOCOL_BT_TRANSPORT},
      {.id = BIND_SERIAL_VID, .reserved = 0, .value = info.serial_vid},
      {.id = BIND_SERIAL_PID, .reserved = 0, .value = info.serial_pid},
  };
  args.set_props(props);
  args.set_proto_id(ZX_PROTOCOL_BT_TRANSPORT);
  return DdkAdd(args);
}

static zx_driver_ops_t bt_hci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = BtTransportUart::Create,
};

}  // namespace bt_transport_uart

ZIRCON_DRIVER(bt_transport_uart, bt_transport_uart::bt_hci_driver_ops, "zircon", "0.1");
