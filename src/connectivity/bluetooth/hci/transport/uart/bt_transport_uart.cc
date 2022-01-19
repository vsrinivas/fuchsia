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

zx_status_t BtTransportUart::Create(void* ctx, zx_device_t* parent) {
  std::unique_ptr<BtTransportUart> dev = std::make_unique<BtTransportUart>(parent);

  zx_status_t bind_status = dev->Bind();
  if (bind_status != ZX_OK) {
    return bind_status;
  }

  // Driver Manager is now in charge of the device.
  // Memory will be explicitly freed in DdkRelease().
  __UNUSED BtTransportUart* unused = dev.release();
  return ZX_OK;
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

void BtTransportUart::ChannelCleanupLocked(ClientChannel* channel) {
  if (channel->handle != ZX_HANDLE_INVALID) {
    zx_handle_close(channel->handle);
    channel->handle = ZX_HANDLE_INVALID;
    channel->err = ZX_OK;
  }
}

void BtTransportUart::SnoopChannelWriteLocked(uint8_t flags, uint8_t* bytes, size_t length) {
  if (snoop_channel_.handle == ZX_HANDLE_INVALID) {
    return;
  }

  // We tack on a flags byte to the beginning of the payload.
  uint8_t snoop_buffer[length + 1];
  snoop_buffer[0] = flags;
  memcpy(snoop_buffer + 1, bytes, length);

  zx_status_t status = zx_channel_write(snoop_channel_.handle, 0, snoop_buffer,
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
  ZX_DEBUG_ASSERT(can_write_);
  // Clear the can_write flag.  The UART can currently only handle one in flight
  // transaction at a time.
  can_write_ = false;

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
void BtTransportUart::HciHandleClientChannel(ClientChannel* chan, zx_signals_t pending) {
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
    // Do not proceed if we are not allowed to write.  Let the work thread call
    // us back again when it is safe to write.
    if (!can_write_) {
      return;
    }

    zx_status_t status;
    uint32_t length = max_buf_size - 1;
    uint8_t* buf = static_cast<uint8_t*>(malloc(length + 1));

    {
      mtx_lock(&mutex_);
      status = zx_channel_read(chan->handle, 0, buf + 1, nullptr, length, 0, &length, nullptr);

      if (status != ZX_OK) {
        zxlogf(ERROR, "hci_read_thread: failed to read from %s channel %s",
               (packet_type == kHciCommand) ? "CMD" : "ACL", zx_status_get_string(status));
        free(buf);
        chan->err = status;
        mtx_unlock(&mutex_);
        return;
      }

      buf[0] = packet_type;
      length++;

      SnoopChannelWriteLocked(bt_hci_snoop_flags(snoop_type, false), buf + 1, length - 1);
      mtx_unlock(&mutex_);
    }

    SerialWrite(buf, length);
  } else {
    // IF we were not readable, then we must have been peer closed, or we should
    // not be here.
    ZX_DEBUG_ASSERT(pending & ZX_CHANNEL_PEER_CLOSED);

    mtx_lock(&mutex_);
    ChannelCleanupLocked(&cmd_channel_);
    mtx_unlock(&mutex_);
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
        // Attempt to send this packet to our cmd channel.  We are working on
        // the callback thread from the UART, so we need to do this inside of
        // the lock to make sure that nothing closes the channel out from under
        // us while we try to write.  Also, if something goes wrong here, flag
        // the channel for close and wake up the work thread.  Do not close the
        // channel here as the work thread may just to be about to wait on it.
        {
          mtx_lock(&mutex_);
          if (cmd_channel_.handle != ZX_HANDLE_INVALID) {
            // send accumulated event packet, minus the packet indicator
            zx_status_t status = zx_channel_write(cmd_channel_.handle, 0, &event_buffer_[1],
                                                  packet_length - 1, nullptr, 0);
            if (status != ZX_OK) {
              zxlogf(ERROR, "bt-transport-uart: failed to write CMD packet: %s",
                     zx_status_get_string(status));
              cmd_channel_.err = status;
              wakeup_event_.signal(0, ZX_EVENT_SIGNALED);
            }
          }
          mtx_unlock(&mutex_);
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
        // Attempt to send accumulated ACL data packet, minus the packet
        // indicator.  See the notes in the cmd channel section for details
        // about error handling and why the lock is needed here
        {
          mtx_lock(&mutex_);
          if (acl_channel_.handle != ZX_HANDLE_INVALID) {
            zx_status_t status = zx_channel_write(acl_channel_.handle, 0, &acl_buffer_[1],
                                                  packet_length - 1, nullptr, 0);

            if (status != ZX_OK) {
              zxlogf(ERROR, "bt-transport-uart: failed to write ACL packet: %s",
                     zx_status_get_string(status));
              acl_channel_.err = status;
              wakeup_event_.signal(0, ZX_EVENT_SIGNALED);
            }
          }
          mtx_unlock(&mutex_);
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
  // If we are in the process of shutting down, we are done as soon as we
  // have freed our operation.
  if (atomic_load_explicit(&shutting_down_, std::memory_order_relaxed)) {
    return;
  }

  if (status != ZX_OK) {
    HciBeginShutdown();
    return;
  }

  // We can write now.  Set the flag and poke the work thread.
  can_write_ = true;
  wakeup_event_.signal(0, ZX_EVENT_SIGNALED);
}

int BtTransportUart::HciThread(void* arg) {
  BtTransportUart* uart = static_cast<BtTransportUart*>(arg);
  zx_status_t status = ZX_OK;

  while (!atomic_load_explicit(&uart->shutting_down_, std::memory_order_relaxed)) {
    zx_wait_item_t wait_items[kNumWaitItems];
    uint32_t wait_count;
    {  // Explicit scope for lock
      mtx_lock(&uart->mutex_);

      // Make a list of our interesting handles.  The wakeup event is always
      // interesting and always goes in slot 0.
      wait_items[0].handle = uart->wakeup_event_.get();
      wait_items[0].waitfor = ZX_EVENT_SIGNALED;
      wait_items[0].pending = 0;
      wait_count = 1;

      // If we  have received any errors on our channels, clean them up now.
      if (uart->cmd_channel_.err != ZX_OK) {
        ChannelCleanupLocked(&uart->cmd_channel_);
      }

      if (uart->acl_channel_.err != ZX_OK) {
        ChannelCleanupLocked(&uart->acl_channel_);
      }

      // Only wait on our channels if we can currently write to our UART
      if (uart->can_write_) {
        if (uart->cmd_channel_.handle != ZX_HANDLE_INVALID) {
          wait_items[wait_count].handle = uart->cmd_channel_.handle;
          wait_items[wait_count].waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
          wait_items[wait_count].pending = 0;
          wait_count++;
        }

        if (uart->acl_channel_.handle != ZX_HANDLE_INVALID) {
          wait_items[wait_count].handle = uart->acl_channel_.handle;
          wait_items[wait_count].waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
          wait_items[wait_count].pending = 0;
          wait_count++;
        }
      }

      mtx_unlock(&uart->mutex_);
    }

    // Now go ahead and do the wait.  Note, this is only safe because there are
    // only two places where a channel gets closed.  One is in the work thread
    // when we discover that a channel has become peer closed.  The other is
    // when we are shutting down, in which case the thread will have been
    // stopped before we get around to the business of closing channels.
    status = zx_object_wait_many(wait_items, wait_count, ZX_TIME_INFINITE);

    // Did we fail to wait?  This should never happen.  If it does, begin the
    // process of shutdown.
    if (status != ZX_OK) {
      zxlogf(ERROR, "bt-transport-uart: zx_object_wait_many failed (%s) - exiting",
             zx_status_get_string(status));
      uart->HciBeginShutdown();
      break;
    }

    // Were we poked?  There are 3 reasons that we may have been.
    //
    // 1) Our set of active channels may have a new member.
    // 2) Our |can_write| status may have changed.
    // 3) It is time to shut down.
    //
    // Simply reset the event and cycle through the loop.  This will take care
    // of each of these possible cases.
    if (wait_items[0].pending) {
      ZX_DEBUG_ASSERT(wait_items[0].handle == uart->wakeup_event_);
      uart->wakeup_event_.signal(/*clear_mask=*/ZX_EVENT_SIGNALED, /*set_mask=*/0);
      continue;
    }

    // Process our channels now.
    for (uint32_t i = 1; i < wait_count; ++i) {
      // Is our channel signalled for anything we care about?
      zx_handle_t pending = wait_items[i].pending & wait_items[i].waitfor;

      if (pending) {
        zx_handle_t handle = wait_items[i].handle;
        if (handle == uart->cmd_channel_.handle) {
          uart->HciHandleClientChannel(&uart->cmd_channel_, pending);
        } else {
          ZX_DEBUG_ASSERT(handle == uart->acl_channel_.handle);
          uart->HciHandleClientChannel(&uart->acl_channel_, pending);
        }
      }
    }
  }

  zxlogf(INFO, "bt-transport-uart: thread exiting");
  uart->thread_running_ = false;
  return status;
}

zx_status_t BtTransportUart::HciOpenChannel(ClientChannel* in_channel, zx_handle_t in) {
  zx_status_t result = ZX_OK;
  mtx_lock(&mutex_);

  if (in_channel->handle != ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "bt-transport-uart: already bound, failing");
    result = ZX_ERR_ALREADY_BOUND;
    goto done;
  }

  in_channel->handle = in;
  in_channel->err = ZX_OK;

  // Kick off the hci_thread if it's not already running.
  if (!thread_running_) {
    thread_running_ = true;
    if (thrd_create_with_name(&thread_, HciThread, this, "bt_uart_read_thread") != thrd_success) {
      thread_running_ = false;
      result = ZX_ERR_INTERNAL;
      goto done;
    }
  } else {
    // Poke the work thread to let it know that there is a new channel to
    // service.
    wakeup_event_.signal(0, ZX_EVENT_SIGNALED);
  }

done:
  mtx_unlock(&mutex_);
  return result;
}

void BtTransportUart::DdkUnbind(ddk::UnbindTxn txn) {
  // We are now shutting down.  Make sure that any pending callbacks in
  // flight from the serial_impl are nerfed and that our thread is shut down.
  std::atomic_store_explicit(&shutting_down_, true, std::memory_order_relaxed);
  if (thread_running_) {
    wakeup_event_.signal(0, ZX_EVENT_SIGNALED);
    thrd_join(thread_, nullptr);
  }

  // Close the transport channels so that the host stack is notified of device
  // removal.
  mtx_lock(&mutex_);
  ChannelCleanupLocked(&cmd_channel_);
  ChannelCleanupLocked(&acl_channel_);
  ChannelCleanupLocked(&snoop_channel_);
  mtx_unlock(&mutex_);

  // Finish by making sure that all in flight transactions transactions have
  // been canceled.
  serial_impl_async_cancel_all(&serial_);

  // Tell the DDK we are done unbinding.
  txn.Reply();
}

void BtTransportUart::DdkRelease() {
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
  serial_impl_async_protocol_t serial;

  zx_status_t status = device_get_protocol(parent(), ZX_PROTOCOL_SERIAL_IMPL_ASYNC, &serial);
  if (status != ZX_OK) {
    zxlogf(ERROR, "bt-transport-uart: get protocol ZX_PROTOCOL_SERIAL failed: %s",
           zx_status_get_string(status));
    return status;
  }

  serial_ = serial;

  status = zx::event::create(0, &wakeup_event_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "hci_bind: zx_event_create failed: %s", zx_status_get_string(status));
    return status;
  }

  mtx_init(&mutex_, mtx_plain);
  cur_uart_packet_type_ = kHciNone;

  // pre-populate event packet indicators
  event_buffer_[0] = kHciEvent;
  event_buffer_offset_ = 1;
  acl_buffer_[0] = kHciAclData;
  acl_buffer_offset_ = 1;

  serial_port_info_t info;
  status = serial_impl_async_get_info(&serial, &info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "hci_bind: serial_get_info failed: %s", zx_status_get_string(status));
    return status;
  }

  // Initially we can write to the UART
  can_write_ = true;

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
