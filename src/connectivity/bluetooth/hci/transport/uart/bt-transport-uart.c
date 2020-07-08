// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fuchsia/hardware/serial/c/fidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/device/bt-hci.h>
#include <zircon/status.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/bt/hci.h>
#include <ddk/protocol/serialimpl/async.h>

// The maximum HCI ACL frame size used for data transactions
#define ACL_MAX_FRAME_SIZE 1029  // (1024 + 4 bytes for the ACL header + 1 byte packet indicator)

#define CMD_BUF_SIZE 255 + 4    // 1 byte packet indicator + 3 byte header + payload
#define EVENT_BUF_SIZE 255 + 3  // 1 byte packet indicator + 2 byte header + payload

// The number of currently supported HCI channel endpoints. We currently have
// one channel for command/event flow and one for ACL data flow. The sniff channel is managed
// separately.
#define NUM_CHANNELS 2
#define NUM_WAIT_ITEMS NUM_CHANNELS + 1  // add one for the wakeup event

// HCI UART packet indicators
typedef enum {
  HCI_NONE = 0,
  HCI_COMMAND = 1,
  HCI_ACL_DATA = 2,
  HCI_SCO = 3,
  HCI_EVENT = 4,
} bt_hci_packet_indicator_t;

typedef struct {
  zx_handle_t h;
  zx_status_t err;
} client_channel_t;

typedef struct {
  zx_device_t* zxdev;
  zx_device_t* parent;
  serial_impl_async_protocol_t serial;

  client_channel_t cmd_channel;
  client_channel_t acl_channel;
  client_channel_t snoop_channel;

  // Signaled any time something changes that the work thread needs to know
  // about.
  zx_handle_t wakeup_event;

  thrd_t thread;
  atomic_bool shutting_down;
  bool thread_running;
  bool can_write;

  // type of current packet being read from the UART
  uint8_t cur_uart_packet_type;

  // for accumulating HCI events
  uint8_t event_buffer[EVENT_BUF_SIZE];
  size_t event_buffer_offset;

  // for accumulating ACL data packets
  uint8_t acl_buffer[ACL_MAX_FRAME_SIZE];
  size_t acl_buffer_offset;

  mtx_t mutex;
} hci_t;

// macro for returning length of current event packet being received
// payload length is in byte 2 of the packet
// add 3 bytes for packet indicator, event code and length byte
#define EVENT_PACKET_LENGTH(hci) ((hci)->event_buffer_offset > 2 ? (hci)->event_buffer[2] + 3 : 0)

// macro for returning length of current ACL data packet being received
// length is in bytes 3 and 4 of the packet
// add 5 bytes for packet indicator, control info and length fields
#define ACL_PACKET_LENGTH(hci) \
  ((hci)->acl_buffer_offset > 4 ? ((hci)->acl_buffer[3] | ((hci)->acl_buffer[4] << 8)) + 5 : 0)

static void channel_init(client_channel_t* c) {
  c->h = ZX_HANDLE_INVALID;
  c->err = ZX_OK;
}

static void channel_cleanup_locked(client_channel_t* channel) {
  if (channel->h != ZX_HANDLE_INVALID) {
    zx_handle_close(channel->h);
    channel->h = ZX_HANDLE_INVALID;
    channel->err = ZX_OK;
  }
}

static void snoop_channel_write_locked(hci_t* hci, uint8_t flags, uint8_t* bytes, size_t length) {
  if (hci->snoop_channel.h == ZX_HANDLE_INVALID) {
    return;
  }

  // We tack on a flags byte to the beginning of the payload.
  uint8_t snoop_buffer[length + 1];
  snoop_buffer[0] = flags;
  memcpy(snoop_buffer + 1, bytes, length);

  zx_status_t status = zx_channel_write(hci->snoop_channel.h, 0, snoop_buffer, length + 1, NULL, 0);
  if (status != ZX_OK) {
    if (status != ZX_ERR_PEER_CLOSED) {
      zxlogf(ERROR, "bt-transport-uart: failed to write to snoop channel: %s",
             zx_status_get_string(status));
    }

    // It should be safe to clean up the channel right here as the work thread
    // never waits on this channel from outside of the lock.
    channel_cleanup_locked(&hci->snoop_channel);
  }
}

static void hci_begin_shutdown(hci_t* hci) {
  if (!atomic_load_explicit(&hci->shutting_down, memory_order_relaxed)) {
    atomic_store_explicit(&hci->shutting_down, true, memory_order_relaxed);
    device_async_remove(hci->zxdev);
  }
}

static void hci_write_complete(void* context, zx_status_t status);

typedef struct hci_write_ctx {
  hci_t* hci;
  // Owned.
  uint8_t* buffer;
} hci_write_ctx_t;

// Takes ownership of buffer.
static void serial_write(hci_t* hci, void* buffer, size_t length) {
  ZX_DEBUG_ASSERT(hci->can_write);

  // Create the job
  hci_write_ctx_t* op = malloc(sizeof(hci_write_ctx_t));
  op->hci = hci;
  op->buffer = buffer;

  // Clear the can_write flag.  The UART can currently only handle one in flight
  // transaction at a time.
  hci->can_write = false;
  serial_impl_async_write_async(&hci->serial, buffer, length, hci_write_complete, op);
}

// Returns false if there's an error while sending the packet to the hardware or
// if the channel peer closed its endpoint.
static void hci_handle_client_channel(hci_t* hci, client_channel_t* chan, zx_signals_t pending) {
  // Figure out which channel we are dealing with and the constants which go
  // along with it.
  uint32_t max_buf_size;
  bt_hci_packet_indicator_t packet_type;
  bt_hci_snoop_type_t snoop_type;

  if (chan == &hci->cmd_channel) {
    max_buf_size = CMD_BUF_SIZE;
    packet_type = HCI_COMMAND;
    snoop_type = BT_HCI_SNOOP_TYPE_CMD;
  } else if (chan == &hci->acl_channel) {
    max_buf_size = ACL_MAX_FRAME_SIZE;
    packet_type = HCI_ACL_DATA;
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
    if (!hci->can_write) {
      return;
    }

    zx_status_t status;
    uint32_t length = max_buf_size - 1;
    uint8_t* buf = malloc(length + 1);

    {
      mtx_lock(&hci->mutex);
      status = zx_channel_read(chan->h, 0, buf + 1, NULL, length, 0, &length, NULL);

      if (status != ZX_OK) {
        zxlogf(ERROR, "hci_read_thread: failed to read from %s channel %s",
               (packet_type == HCI_COMMAND) ? "CMD" : "ACL", zx_status_get_string(status));
        free(buf);
        chan->err = status;
        mtx_unlock(&hci->mutex);
        return;
      }

      buf[0] = packet_type;
      length++;

      snoop_channel_write_locked(hci, bt_hci_snoop_flags(snoop_type, false), buf + 1, length - 1);
      mtx_unlock(&hci->mutex);
    }

    serial_write(hci, buf, length);
  } else {
    // IF we were not readable, then we must have been peer closed, or we should
    // not be here.
    ZX_DEBUG_ASSERT(pending & ZX_CHANNEL_PEER_CLOSED);

    mtx_lock(&hci->mutex);
    channel_cleanup_locked(&hci->cmd_channel);
    mtx_unlock(&hci->mutex);
  }
}

static void hci_handle_uart_read_events(hci_t* hci, const uint8_t* buf, size_t length) {
  const uint8_t* src = buf;
  const uint8_t* const end = src + length;
  uint8_t packet_type = hci->cur_uart_packet_type;

  while (src < end) {
    if (packet_type == HCI_NONE) {
      // start of new packet. read packet type
      packet_type = *src++;
      if (packet_type != HCI_EVENT && packet_type != HCI_ACL_DATA) {
        zxlogf(INFO, "unsupported HCI packet type %u. We may be out of sync", packet_type);
        return;
      }
    }

    if (packet_type == HCI_EVENT) {
      size_t packet_length = EVENT_PACKET_LENGTH(hci);

      while (!packet_length && src < end) {
        // read until we have enough to compute packet length
        hci->event_buffer[hci->event_buffer_offset++] = *src++;
        packet_length = EVENT_PACKET_LENGTH(hci);
      }
      if (!packet_length) {
        break;
      }

      size_t remaining = end - src;
      size_t copy = packet_length - hci->event_buffer_offset;
      if (copy > remaining) {
        copy = remaining;
      }
      ZX_ASSERT((hci->event_buffer_offset + copy) <= sizeof(hci->event_buffer));
      memcpy(hci->event_buffer + hci->event_buffer_offset, src, copy);
      src += copy;
      hci->event_buffer_offset += copy;

      if (hci->event_buffer_offset == packet_length) {
        // Attempt to send this packet to our cmd channel.  We are working on
        // the callback thread from the UART, so we need to do this inside of
        // the lock to make sure that nothing closes the channel out from under
        // us while we try to write.  Also, if something goes wrong here, flag
        // the channel for close and wake up the work thread.  Do not close the
        // channel here as the work thread may just to be about to wait on it.
        {
          mtx_lock(&hci->mutex);
          if (hci->cmd_channel.h != ZX_HANDLE_INVALID) {
            // send accumulated event packet, minus the packet indicator
            zx_status_t status = zx_channel_write(hci->cmd_channel.h, 0, &hci->event_buffer[1],
                                                  packet_length - 1, NULL, 0);
            if (status != ZX_OK) {
              zxlogf(ERROR, "bt-transport-uart: failed to write CMD packet: %s",
                     zx_status_get_string(status));
              hci->cmd_channel.err = status;
              zx_object_signal(hci->wakeup_event, 0, ZX_EVENT_SIGNALED);
            }
          }
          mtx_unlock(&hci->mutex);
        }

        snoop_channel_write_locked(hci, bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_EVT, true),
                                   &hci->event_buffer[1], packet_length - 1);

        // reset buffer
        packet_type = HCI_NONE;
        hci->event_buffer_offset = 1;
      }
    } else {  // HCI_ACL_DATA
      size_t packet_length = ACL_PACKET_LENGTH(hci);

      while (!packet_length && src < end) {
        // read until we have enough to compute packet length
        hci->acl_buffer[hci->acl_buffer_offset++] = *src++;
        packet_length = ACL_PACKET_LENGTH(hci);
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
      if (packet_length > sizeof(hci->acl_buffer)) {
        zxlogf(ERROR,
               "bt-transport-uart: packet_length is too large (%zu > %zu) during ACL packet "
               "reassembly.  Dropping and attempting to re-sync.\n",
               packet_length, sizeof(hci->acl_buffer_offset));

        // reset the reassembly state machine.
        packet_type = HCI_NONE;
        hci->acl_buffer_offset = 1;
        break;
      }

      size_t remaining = end - src;
      size_t copy = packet_length - hci->acl_buffer_offset;
      if (copy > remaining) {
        copy = remaining;
      }

      ZX_ASSERT((hci->acl_buffer_offset + copy) <= sizeof(hci->acl_buffer));
      memcpy(hci->acl_buffer + hci->acl_buffer_offset, src, copy);
      src += copy;
      hci->acl_buffer_offset += copy;

      if (hci->acl_buffer_offset == packet_length) {
        // Attempt to send accumulated ACL data packet, minus the packet
        // indicator.  See the notes in the cmd channel section for details
        // about error handling and why the lock is needed here
        {
          mtx_lock(&hci->mutex);
          if (hci->acl_channel.h != ZX_HANDLE_INVALID) {
            zx_status_t status = zx_channel_write(hci->acl_channel.h, 0, &hci->acl_buffer[1],
                                                  packet_length - 1, NULL, 0);

            if (status != ZX_OK) {
              zxlogf(ERROR, "bt-transport-uart: failed to write ACL packet: %s",
                     zx_status_get_string(status));
              hci->acl_channel.err = status;
              zx_object_signal(hci->wakeup_event, 0, ZX_EVENT_SIGNALED);
            }
          }
          mtx_unlock(&hci->mutex);
        }

        // If the snoop channel is open then try to write the packet
        // even if acl_channel was closed.
        snoop_channel_write_locked(hci, bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_ACL, true),
                                   &hci->acl_buffer[1], packet_length - 1);

        // reset buffer
        packet_type = HCI_NONE;
        hci->acl_buffer_offset = 1;
      }
    }
  }

  hci->cur_uart_packet_type = packet_type;
}

static void hci_read_complete(void* context, zx_status_t status, const void* buffer,
                              size_t length) {
  hci_t* hci = context;

  // If we are in the process of shutting down, we are done.
  if (atomic_load_explicit(&hci->shutting_down, memory_order_relaxed)) {
    return;
  }

  if (status == ZX_OK) {
    hci_handle_uart_read_events(context, buffer, length);
    serial_impl_async_read_async(&hci->serial, hci_read_complete, hci);
  } else {
    // There is not much we can do in the event of a UART read error.  Do not
    // queue a read job and start the process of shutting down.
    zxlogf(ERROR, "Fatal UART read error (%s), shutting down", zx_status_get_string(status));
    hci_begin_shutdown(hci);
  }
}

static void hci_write_complete(void* context, zx_status_t status) {
  hci_write_ctx_t* op = context;
  free(op->buffer);

  // If we are in the process of shutting down, we are done as soon as we
  // have freed our operation.
  if (atomic_load_explicit(&op->hci->shutting_down, memory_order_relaxed)) {
    return;
  }

  if (status != ZX_OK) {
    hci_begin_shutdown(op->hci);
    free(op);
    return;
  }

  // We can write now.  Set the flag and poke the work thread.
  op->hci->can_write = true;
  zx_object_signal(op->hci->wakeup_event, 0, ZX_EVENT_SIGNALED);
  free(op);
}

static int hci_thread(void* arg) {
  hci_t* hci = (hci_t*)arg;
  zx_status_t status = ZX_OK;

  while (!atomic_load_explicit(&hci->shutting_down, memory_order_relaxed)) {
    zx_wait_item_t wait_items[NUM_WAIT_ITEMS];
    uint32_t wait_count;
    {  // Explicit scope for lock
      mtx_lock(&hci->mutex);

      // Make a list of our interesting handles.  The wakeup event is always
      // interesting and always goes in slot 0.
      wait_items[0].handle = hci->wakeup_event;
      wait_items[0].waitfor = ZX_EVENT_SIGNALED;
      wait_items[0].pending = 0;
      wait_count = 1;

      // If we  have received any errors on our channels, clean them up now.
      if (hci->cmd_channel.err != ZX_OK) {
        channel_cleanup_locked(&hci->cmd_channel);
      }

      if (hci->acl_channel.err != ZX_OK) {
        channel_cleanup_locked(&hci->acl_channel);
      }

      // Only wait on our channels if we can currently write to our UART
      if (hci->can_write) {
        if (hci->cmd_channel.h != ZX_HANDLE_INVALID) {
          wait_items[wait_count].handle = hci->cmd_channel.h;
          wait_items[wait_count].waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
          wait_items[wait_count].pending = 0;
          wait_count++;
        }

        if (hci->acl_channel.h != ZX_HANDLE_INVALID) {
          wait_items[wait_count].handle = hci->acl_channel.h;
          wait_items[wait_count].waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
          wait_items[wait_count].pending = 0;
          wait_count++;
        }
      }

      mtx_unlock(&hci->mutex);
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
      hci_begin_shutdown(hci);
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
      ZX_DEBUG_ASSERT(wait_items[0].handle == hci->wakeup_event);
      zx_object_signal(hci->wakeup_event, ZX_EVENT_SIGNALED, 0);
      continue;
    }

    // Process our channels now.
    for (uint32_t i = 1; i < wait_count; ++i) {
      // Is our channel signalled for anything we care about?
      zx_handle_t pending = wait_items[i].pending & wait_items[i].waitfor;

      if (pending) {
        zx_handle_t handle = wait_items[i].handle;
        if (handle == hci->cmd_channel.h) {
          hci_handle_client_channel(hci, &hci->cmd_channel, pending);
        } else {
          ZX_DEBUG_ASSERT(handle == hci->acl_channel.h);
          hci_handle_client_channel(hci, &hci->acl_channel, pending);
        }
      }
    }
  }

  zxlogf(INFO, "bt-transport-uart: thread exiting");
  hci->thread_running = false;
  return status;
}

static zx_status_t hci_open_channel(hci_t* hci, client_channel_t* in_channel, zx_handle_t in) {
  zx_status_t result = ZX_OK;
  mtx_lock(&hci->mutex);

  if (in_channel->h != ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "bt-transport-uart: already bound, failing");
    result = ZX_ERR_ALREADY_BOUND;
    goto done;
  }

  in_channel->h = in;
  in_channel->err = ZX_OK;

  // Kick off the hci_thread if it's not already running.
  if (!hci->thread_running) {
    hci->thread_running = true;
    if (thrd_create_with_name(&hci->thread, hci_thread, hci, "bt_uart_read_thread") !=
        thrd_success) {
      hci->thread_running = false;
      result = ZX_ERR_INTERNAL;
      goto done;
    }
  } else {
    // Poke the work thread to let it know that there is a new channel to
    // service.
    zx_object_signal(hci->wakeup_event, 0, ZX_EVENT_SIGNALED);
  }

done:
  mtx_unlock(&hci->mutex);
  return result;
}

static void hci_unbind(void* ctx) {
  hci_t* hci = ctx;

  // We are now shutting down.  Make sure that any pending callbacks in
  // flight from the serial_impl are nerfed and that our thread is shut down.
  atomic_store_explicit(&hci->shutting_down, true, memory_order_relaxed);
  if (hci->thread_running) {
    zx_object_signal(hci->wakeup_event, 0, ZX_EVENT_SIGNALED);
    thrd_join(hci->thread, NULL);
  }

  // Close the transport channels so that the host stack is notified of device
  // removal.
  mtx_lock(&hci->mutex);
  channel_cleanup_locked(&hci->cmd_channel);
  channel_cleanup_locked(&hci->acl_channel);
  channel_cleanup_locked(&hci->snoop_channel);
  mtx_unlock(&hci->mutex);

  // Finish by making sure that all in flight transactions transactions have
  // been canceled.
  serial_impl_async_cancel_all(&hci->serial);

  // Tell the DDK we are done unbinding.
  device_unbind_reply(hci->zxdev);
}

static void hci_release(void* ctx) {
  hci_t* hci = ctx;
  zx_handle_close(hci->wakeup_event);
  free(hci);
}

static zx_status_t hci_open_command_channel(void* ctx, zx_handle_t in) {
  hci_t* hci = ctx;
  return hci_open_channel(hci, &hci->cmd_channel, in);
}

static zx_status_t hci_open_acl_data_channel(void* ctx, zx_handle_t in) {
  hci_t* hci = ctx;
  return hci_open_channel(hci, &hci->acl_channel, in);
}

static zx_status_t hci_open_snoop_channel(void* ctx, zx_handle_t in) {
  hci_t* hci = ctx;
  return hci_open_channel(hci, &hci->snoop_channel, in);
}

static bt_hci_protocol_ops_t hci_protocol_ops = {
    .open_command_channel = hci_open_command_channel,
    .open_acl_data_channel = hci_open_acl_data_channel,
    .open_snoop_channel = hci_open_snoop_channel,
};

static zx_status_t hci_get_protocol(void* ctx, uint32_t proto_id, void* protocol) {
  hci_t* hci = ctx;
  if (proto_id != ZX_PROTOCOL_BT_HCI) {
    // Pass this on for drivers to load firmware / initialize
    return device_get_protocol(hci->parent, proto_id, protocol);
  }

  bt_hci_protocol_t* hci_proto = protocol;

  hci_proto->ops = &hci_protocol_ops;
  hci_proto->ctx = ctx;
  return ZX_OK;
};

static zx_protocol_device_t hci_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = hci_get_protocol,
    .unbind = hci_unbind,
    .release = hci_release,
};

static zx_status_t hci_bind(void* ctx, zx_device_t* parent) {
  serial_impl_async_protocol_t serial;

  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_SERIAL_IMPL_ASYNC, &serial);
  if (status != ZX_OK) {
    zxlogf(ERROR, "bt-transport-uart: get protocol ZX_PROTOCOL_SERIAL failed");
    return status;
  }

  hci_t* hci = calloc(1, sizeof(hci_t));
  if (!hci) {
    zxlogf(ERROR, "bt-transport-uart: Not enough memory for hci_t");
    return ZX_ERR_NO_MEMORY;
  }

  channel_init(&hci->cmd_channel);
  channel_init(&hci->acl_channel);
  channel_init(&hci->snoop_channel);

  hci->serial = serial;
  if (status != ZX_OK) {
    zxlogf(ERROR, "bt-transport-uart: serial_open_socket failed: %s", zx_status_get_string(status));
    goto fail;
  }

  status = zx_event_create(0, &hci->wakeup_event);
  if (status != ZX_OK) {
    zxlogf(ERROR, "hci_bind: zx_event_create failed");
    goto fail;
  }

  mtx_init(&hci->mutex, mtx_plain);
  hci->parent = parent;
  hci->cur_uart_packet_type = HCI_NONE;

  // pre-populate event packet indicators
  hci->event_buffer[0] = HCI_EVENT;
  hci->event_buffer_offset = 1;
  hci->acl_buffer[0] = HCI_ACL_DATA;
  hci->acl_buffer_offset = 1;

  serial_port_info_t info;
  status = serial_impl_async_get_info(&serial, &info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "hci_bind: serial_get_info failed");
    goto fail;
  }

  // Initially we can write to the UART
  hci->can_write = true;

  if (info.serial_class != fuchsia_hardware_serial_Class_BLUETOOTH_HCI) {
    zxlogf(ERROR, "hci_bind: info.device_class != BLUETOOTH_HCI");
    status = ZX_ERR_INTERNAL;
    goto fail;
  }

  // Copy the PID and VID from the platform device info so it can be filtered on
  // for HCI drivers
  zx_device_prop_t props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_BT_TRANSPORT},
      {BIND_SERIAL_VID, 0, info.serial_vid},
      {BIND_SERIAL_PID, 0, info.serial_pid},
  };

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "bt-transport-uart",
      .ctx = hci,
      .ops = &hci_device_proto,
      .proto_id = ZX_PROTOCOL_BT_TRANSPORT,
      .props = props,
      .prop_count = countof(props),
  };
  serial_impl_async_enable(&serial, true);
  serial_impl_async_read_async(&serial, hci_read_complete, hci);
  status = device_add(parent, &args, &hci->zxdev);
  if (status == ZX_OK) {
    return ZX_OK;
  }

fail:
  zxlogf(ERROR, "hci_bind: bind failed: %s", zx_status_get_string(status));
  hci_release(hci);
  return status;
}

static zx_driver_ops_t bt_hci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = hci_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(bt_transport_uart, bt_hci_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_SERIAL_IMPL_ASYNC),
    BI_MATCH_IF(EQ, BIND_SERIAL_CLASS, fuchsia_hardware_serial_Class_BLUETOOTH_HCI),
ZIRCON_DRIVER_END(bt_transport_uart)
