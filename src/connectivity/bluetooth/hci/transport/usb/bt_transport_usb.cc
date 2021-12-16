// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bt_transport_usb.h"

#include <assert.h>
#include <fuchsia/hardware/bt/hci/c/banjo.h>
#include <fuchsia/hardware/usb/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/sync/completion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/device/bt-hci.h>
#include <zircon/status.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include <usb/usb-request.h>
#include <usb/usb.h>

#include "src/connectivity/bluetooth/hci/transport/usb/bt_transport_usb_bind.h"
#include "src/lib/listnode/listnode.h"

#define EVENT_REQ_COUNT 8

// TODO(armansito): Consider increasing these.
#define ACL_READ_REQ_COUNT 8
#define ACL_WRITE_REQ_COUNT 8

// The maximum HCI ACL frame size used for data transactions
#define ACL_MAX_FRAME_SIZE 1028  // (1024 + 4 bytes for the ACL header)

#define CMD_BUF_SIZE 255 + 3    // 3 byte header + payload
#define EVENT_BUF_SIZE 255 + 2  // 2 byte header + payload

// TODO(fxbug.dev/90072): move these to hw/usb.h (or hw/bluetooth.h if that exists)
#define USB_SUBCLASS_BLUETOOTH 1
#define USB_PROTOCOL_BLUETOOTH 1

namespace bt_transport_usb {

struct HciEventHeader {
  uint8_t event_code;
  uint8_t parameter_total_size;
} __PACKED;

zx_status_t Device::Create(void* ctx, zx_device_t* parent) {
  std::unique_ptr<Device> dev = std::make_unique<Device>(parent);

  zx_status_t bind_status = dev->Bind();
  if (bind_status != ZX_OK) {
    zxlogf(ERROR, "bt-transport-usb: failed to bind: %s", zx_status_get_string(bind_status));
    return bind_status;
  }

  // Driver Manager is now in charge of the device.
  // Memory will be explicitly freed in DdkUnbind().
  __UNUSED Device* unused = dev.release();
  return ZX_OK;
}

zx_status_t Device::Bind() {
  zxlogf(DEBUG, "%s", __FUNCTION__);
  usb_protocol_t usb;

  zx_status_t status = device_get_protocol(parent(), ZX_PROTOCOL_USB, &usb);
  if (status != ZX_OK) {
    zxlogf(ERROR, "bt-transport-usb: get protocol failed: %s", zx_status_get_string(status));
    return status;
  }

  // Get the configuration descriptor, which contains interface descriptors.
  usb_desc_iter_t iter;
  zx_status_t result = usb_desc_iter_init(&usb, &iter);
  if (result < 0) {
    zxlogf(ERROR, "bt-transport-usb: failed to get usb configuration descriptor: %s",
           zx_status_get_string(status));
    return result;
  }

  // The first interface should contain the interrupt, bulk in, and bulk out endpoints.
  // This is expected to fail when the bind rules match but the interface isn't the right one.
  usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
  if (!intf || intf->b_num_endpoints != 3) {
    usb_desc_iter_release(&iter);
    zxlogf(DEBUG, "%s: failed to get usb interface 0 with interrupt & bulk endpoints",
           __FUNCTION__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint8_t bulk_in_addr = 0;
  uint8_t bulk_out_addr = 0;
  uint8_t intr_addr = 0;
  uint16_t intr_max_packet = 0;

  usb_endpoint_descriptor_t* endp = usb_desc_iter_next_endpoint(&iter);
  while (endp) {
    if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
      if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
        bulk_out_addr = endp->b_endpoint_address;
      }
    } else {
      if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
        bulk_in_addr = endp->b_endpoint_address;
      } else if (usb_ep_type(endp) == USB_ENDPOINT_INTERRUPT) {
        intr_addr = endp->b_endpoint_address;
        intr_max_packet = usb_ep_max_packet(endp);
      }
    }
    endp = usb_desc_iter_next_endpoint(&iter);
  }
  usb_desc_iter_release(&iter);

  if (!bulk_in_addr || !bulk_out_addr || !intr_addr) {
    zxlogf(ERROR, "bt-transport-usb: bind could not find endpoints");
    return ZX_ERR_NOT_SUPPORTED;
  }

  list_initialize(&free_event_reqs_);
  list_initialize(&free_acl_read_reqs_);
  list_initialize(&free_acl_write_reqs_);

  // Initialize events used by read thread.
  zx_status_t channels_changed_evt_status = zx_event_create(/*options=*/0, &channels_changed_evt_);
  zx_status_t unbind_evt_status = zx_event_create(/*options=*/0, &unbind_evt_);
  if (channels_changed_evt_status != ZX_OK || unbind_evt_status != ZX_OK) {
    OnBindFailure(status, "zx_event_create failure");
    return status;
  }

  mtx_init(&mutex_, mtx_plain);
  mtx_init(&pending_request_lock_, mtx_plain);
  cnd_init(&pending_requests_completed_);

  memcpy(&usb_, &usb, sizeof(usb));

  parent_req_size_ = usb_get_request_size(&usb);
  size_t req_size = parent_req_size_ + sizeof(usb_req_internal_t) + sizeof(void*);
  status =
      AllocBtUsbPackets(EVENT_REQ_COUNT, intr_max_packet, intr_addr, req_size, &free_event_reqs_);
  if (status != ZX_OK) {
    OnBindFailure(status, "event USB request allocation failure");
    return status;
  }
  status = AllocBtUsbPackets(ACL_READ_REQ_COUNT, ACL_MAX_FRAME_SIZE, bulk_in_addr, req_size,
                             &free_acl_read_reqs_);
  if (status != ZX_OK) {
    OnBindFailure(status, "ACL read USB request allocation failure");
    return status;
  }
  status = AllocBtUsbPackets(ACL_WRITE_REQ_COUNT, ACL_MAX_FRAME_SIZE, bulk_out_addr, req_size,
                             &free_acl_write_reqs_);
  if (status != ZX_OK) {
    OnBindFailure(status, "ACL write USB request allocation failure");
    return status;
  }

  mtx_lock(&mutex_);
  QueueInterruptRequestsLocked();
  QueueAclReadRequestsLocked();
  HciBuildReadWaitItemsLocked();
  mtx_unlock(&mutex_);

  // Copy the PID and VID from the underlying BT so that it can be filtered on
  // for HCI drivers
  usb_device_descriptor_t dev_desc;
  usb_get_device_descriptor(&usb, &dev_desc);
  zx_device_prop_t props[] = {
      {.id = BIND_PROTOCOL, .reserved = 0, .value = ZX_PROTOCOL_BT_TRANSPORT},
      {.id = BIND_USB_VID, .reserved = 0, .value = dev_desc.id_vendor},
      {.id = BIND_USB_PID, .reserved = 0, .value = dev_desc.id_product},
  };
  zxlogf(DEBUG, "bt-transport-usb: vendor id = %hu, product id = %hu", dev_desc.id_vendor,
         dev_desc.id_product);

  ddk::DeviceAddArgs args("bt_transport_usb");
  args.set_props(props);
  args.set_proto_id(ZX_PROTOCOL_BT_TRANSPORT);
  status = DdkAdd(args);
  if (status != ZX_OK) {
    OnBindFailure(status, "DdkAdd");
    return status;
  }

  // Start the read thread.
  zxlogf(DEBUG, "starting read thread");
  thrd_create(&read_thread_, &Device::HciReadThread, this);
  return ZX_OK;
}

zx_status_t Device::DdkGetProtocol(uint32_t proto_id, void* out) {
  if (proto_id != ZX_PROTOCOL_BT_HCI) {
    // Pass this on for drivers to load firmware / initialize
    return device_get_protocol(parent(), proto_id, out);
  }

  bt_hci_protocol_t* hci_proto = static_cast<bt_hci_protocol_t*>(out);
  hci_proto->ops = &bt_hci_protocol_ops_;
  hci_proto->ctx = this;
  return ZX_OK;
}

void Device::DdkUnbind(ddk::UnbindTxn txn) {
  zxlogf(DEBUG, "%s", __FUNCTION__);

  // Copy the thread so it can be used without the mutex.
  thrd_t read_thread;

  // Close the transport channels so that the host stack is notified of device removal.
  mtx_lock(&mutex_);
  mtx_lock(&pending_request_lock_);

  read_thread = read_thread_;
  unbound_ = true;

  mtx_unlock(&pending_request_lock_);

  ChannelCleanupLocked(&cmd_channel_);
  ChannelCleanupLocked(&acl_channel_);
  ChannelCleanupLocked(&snoop_channel_);

  mtx_unlock(&mutex_);

  // Wait for all pending USB requests to complete (the USB driver should fail them).
  zxlogf(DEBUG, "waiting for all requests to complete before unbinding");
  mtx_lock(&pending_request_lock_);
  while (atomic_load(&pending_request_count_)) {
    cnd_wait(&pending_requests_completed_, &pending_request_lock_);
  }
  mtx_unlock(&pending_request_lock_);
  zxlogf(DEBUG, "all pending requests completed");

  zxlogf(DEBUG, "%s: waiting for read thread to complete", __FUNCTION__);
  // Signal and wait for the read thread to complete (this is necessary to prevent use-after-free of
  // member variables in the read thread).
  zx_object_signal(/*handle=*/unbind_evt_, /*clear_mask=*/0,
                   /*set_mask=*/ZX_EVENT_SIGNALED);
  int join_res = 0;
  thrd_join(read_thread, &join_res);
  zxlogf(DEBUG, "read thread completed with status %d", join_res);

  txn.Reply();
}

void Device::DdkRelease() {
  zxlogf(DEBUG, "%s", __FUNCTION__);
  mtx_lock(&mutex_);

  usb_request_t* req;
  while ((req = usb_req_list_remove_head(&free_event_reqs_, parent_req_size_)) != nullptr) {
    InstrumentedRequestRelease(req);
  }
  while ((req = usb_req_list_remove_head(&free_acl_read_reqs_, parent_req_size_)) != nullptr) {
    InstrumentedRequestRelease(req);
  }
  while ((req = usb_req_list_remove_head(&free_acl_write_reqs_, parent_req_size_)) != nullptr) {
    InstrumentedRequestRelease(req);
  }

  mtx_unlock(&mutex_);
  // Wait for all the requests in the pipeline to asynchronously fail.
  // Either the completion routine or the submitter should free the requests.
  // It shouldn't be possible to have any "stray" requests that aren't in-flight at this point,
  // so this is guaranteed to complete.
  zxlogf(DEBUG, "%s: waiting for all requests to be freed before releasing", __FUNCTION__);
  sync_completion_wait(&requests_freed_completion_, ZX_TIME_INFINITE);

  // Driver manager is given a raw pointer to this dynamically allocated object in Bind(), so when
  // DdkRelease() is called we need to free the allocated memory.
  delete this;
}

// Allocates a USB request and keeps track of how many requests have been allocated.
zx_status_t Device::InstrumentedRequestAlloc(usb_request_t** out, uint64_t data_size,
                                             uint8_t ep_address, size_t req_size) {
  atomic_fetch_add(&allocated_requests_count_, 1);
  return usb_request_alloc(out, data_size, ep_address, req_size);
}

// Releases a USB request and decrements the usage count.
// Signals a completion when all requests have been released.
void Device::InstrumentedRequestRelease(usb_request_t* req) {
  usb_request_release(req);
  size_t req_count = atomic_fetch_sub(&allocated_requests_count_, 1);
  zxlogf(TRACE, "remaining allocated requests: %zu", req_count - 1);
  // atomic_fetch_sub returns the value prior to being updated, so a value of 1 means that this is
  // the last request.
  if (req_count == 1) {
    sync_completion_signal(&requests_freed_completion_);
  }
}

// usb_request_callback is a hook that is inserted for every USB request
// which guarantees the following conditions:
// * No completions will be invoked during driver unbind.
// * pending_request_count shall indicate the number of requests outstanding.
// * pending_requests_completed shall be asserted when the number of requests pending equals zero.
// * Requests are properly freed during shutdown.
void Device::UsbRequestCallback(usb_request_t* req) {
  zxlogf(TRACE, "%s", __FUNCTION__);
  // Invoke the real completion if not shutting down.
  mtx_lock(&pending_request_lock_);
  if (!unbound_) {
    // Request callback pointer is stored at the end of the usb_request_t after
    // other data that has been appended to the request by drivers elsewhere in the stack.
    // memcpy is necessary here to prevent undefined behavior since there are no guarantees
    // about the alignment of data that other drivers append to the usb_request_t.
    usb_callback_t callback;
    memcpy(&callback,
           reinterpret_cast<unsigned char*>(req) + parent_req_size_ + sizeof(usb_req_internal_t),
           sizeof(callback));
    // Our threading model allows a callback to immediately re-queue a request here
    // which would result in attempting to recursively lock pending_request_lock.
    // Unlocking the mutex is necessary to prevent a crash.
    mtx_unlock(&pending_request_lock_);
    callback(this, req);
    mtx_lock(&pending_request_lock_);
  } else {
    InstrumentedRequestRelease(req);
  }
  size_t pending_request_count = std::atomic_fetch_sub(&pending_request_count_, 1);
  zxlogf(TRACE, "%s: pending requests: %zu", __FUNCTION__, pending_request_count - 1);
  // Since atomic_fetch_add returns the value that was in pending_request_count prior to
  // decrementing, there are no pending requests when the value returned is 1.
  if (pending_request_count == 1) {
    cnd_signal(&pending_requests_completed_);
  }
  mtx_unlock(&pending_request_lock_);
}

void Device::UsbRequestSend(usb_protocol_t* function, usb_request_t* req, usb_callback_t callback) {
  mtx_lock(&pending_request_lock_);
  if (unbound_) {
    mtx_unlock(&pending_request_lock_);
    return;
  }
  std::atomic_fetch_add(&pending_request_count_, 1);
  size_t parent_req_size = parent_req_size_;
  mtx_unlock(&pending_request_lock_);

  usb_request_complete_callback_t internal_completion = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            static_cast<Device*>(ctx)->UsbRequestCallback(request);
          },
      .ctx = this};
  memcpy(reinterpret_cast<unsigned char*>(req) + parent_req_size + sizeof(usb_req_internal_t),
         &callback, sizeof(callback));
  usb_request_queue(function, req, &internal_completion);
}

void Device::QueueAclReadRequestsLocked() {
  usb_request_t* req = nullptr;
  while ((req = usb_req_list_remove_head(&free_acl_read_reqs_, parent_req_size_)) != nullptr) {
    UsbRequestSend(&usb_, req, [](void* ctx, usb_request_t* req) {
      static_cast<Device*>(ctx)->HciAclReadComplete(req);
    });
  }
}

void Device::QueueInterruptRequestsLocked() {
  usb_request_t* req = nullptr;
  while ((req = usb_req_list_remove_head(&free_event_reqs_, parent_req_size_)) != nullptr) {
    UsbRequestSend(&usb_, req, [](void* ctx, usb_request_t* req) {
      static_cast<Device*>(ctx)->HciEventComplete(req);
    });
  }
}

void Device::ChannelCleanupLocked(zx::channel* channel) {
  if (!channel->is_valid()) {
    return;
  }

  channel->reset();
  zx_object_signal(channels_changed_evt_, 0, ZX_EVENT_SIGNALED);
}

void Device::SnoopChannelWriteLocked(uint8_t flags, uint8_t* bytes, size_t length) {
  if (snoop_channel_ == ZX_HANDLE_INVALID)
    return;

  // We tack on a flags byte to the beginning of the payload.
  uint8_t snoop_buffer[length + 1];
  snoop_buffer[0] = flags;
  memcpy(snoop_buffer + 1, bytes, length);
  zx_status_t status = zx_channel_write(snoop_channel_.get(), 0, snoop_buffer,
                                        static_cast<uint32_t>(length + 1), nullptr, 0);
  if (status < 0) {
    if (status != ZX_ERR_PEER_CLOSED) {
      zxlogf(ERROR, "bt-transport-usb: failed to write to snoop channel: %s",
             zx_status_get_string(status));
    }
    ChannelCleanupLocked(&snoop_channel_);
  }
}

void Device::RemoveDeviceLocked() { DdkAsyncRemove(); }

void Device::HciEventComplete(usb_request_t* req) {
  zxlogf(TRACE, "bt-transport-usb: Event received");
  mtx_lock(&mutex_);

  if (req->response.status != ZX_OK) {
    HandleUsbResponseError(req, "hci event");
    mtx_unlock(&mutex_);
    return;
  }

  // Handle the interrupt as long as either the command channel or the snoop channel is open.
  if (cmd_channel_ == ZX_HANDLE_INVALID && snoop_channel_ == ZX_HANDLE_INVALID) {
    zxlogf(
        DEBUG,
        "bt-transport-usb: received hci event while command channel and snoop channel are closed");
    // Re-queue the HCI event USB request.
    zx_status_t status = usb_req_list_add_head(&free_event_reqs_, req, parent_req_size_);
    ZX_ASSERT(status == ZX_OK);
    QueueInterruptRequestsLocked();
    mtx_unlock(&mutex_);
    return;
  }

  void* buffer;
  zx_status_t status = usb_request_mmap(req, &buffer);
  if (status != ZX_OK) {
    zxlogf(ERROR, "bt-transport-usb: usb_req_mmap failed: %s", zx_status_get_string(status));
    mtx_unlock(&mutex_);
    return;
  }
  size_t length = req->response.actual;
  uint8_t event_parameter_total_size = static_cast<uint8_t*>(buffer)[1];
  size_t packet_size = event_parameter_total_size + sizeof(HciEventHeader);

  // simple case - packet fits in received data
  if (event_buffer_offset_ == 0 && length >= sizeof(HciEventHeader)) {
    if (packet_size == length) {
      if (cmd_channel_ != ZX_HANDLE_INVALID) {
        zx_status_t status = zx_channel_write(cmd_channel_.get(), 0, buffer,
                                              static_cast<uint32_t>(length), nullptr, 0);
        if (status < 0) {
          zxlogf(ERROR,
                 "bt-transport-usb: hci_event_complete failed to write to command channel: %s",
                 zx_status_get_string(status));
        }
      }
      SnoopChannelWriteLocked(bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_EVT, true),
                              static_cast<uint8_t*>(buffer), length);

      // Re-queue the HCI event USB request.
      status = usb_req_list_add_head(&free_event_reqs_, req, parent_req_size_);
      ZX_ASSERT(status == ZX_OK);
      QueueInterruptRequestsLocked();
      mtx_unlock(&mutex_);
      return;
    }
  }

  // complicated case - need to accumulate into hci->event_buffer

  if (event_buffer_offset_ + length > sizeof(event_buffer_)) {
    zxlogf(ERROR, "bt-transport-usb: event_buffer would overflow!");
    mtx_unlock(&mutex_);
    return;
  }

  memcpy(&event_buffer_[event_buffer_offset_], buffer, length);
  if (event_buffer_offset_ == 0) {
    event_buffer_packet_length_ = packet_size;
  } else {
    packet_size = event_buffer_packet_length_;
  }
  event_buffer_offset_ += length;

  // check to see if we have a full packet
  if (packet_size <= event_buffer_offset_) {
    zxlogf(TRACE,
           "bt-transport-usb: Accumulated full HCI event packet, sending on command & snoop "
           "channels.");
    zx_status_t status = zx_channel_write(cmd_channel_.get(), 0, event_buffer_,
                                          static_cast<uint32_t>(packet_size), nullptr, 0);
    if (status < 0) {
      zxlogf(ERROR, "bt-transport-usb: failed to write to command channel: %s",
             zx_status_get_string(status));
    }

    SnoopChannelWriteLocked(bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_EVT, true), event_buffer_,
                            packet_size);

    uint32_t remaining = static_cast<uint32_t>(event_buffer_offset_ - packet_size);
    memmove(event_buffer_, event_buffer_ + packet_size, remaining);
    event_buffer_offset_ = 0;
    event_buffer_packet_length_ = 0;
  } else {
    zxlogf(TRACE,
           "bt-transport-usb: Received incomplete chunk of HCI event packet. Appended to buffer.");
  }

  // Re-queue the HCI event USB request.
  status = usb_req_list_add_head(&free_event_reqs_, req, parent_req_size_);
  ZX_ASSERT(status == ZX_OK);
  QueueInterruptRequestsLocked();
  mtx_unlock(&mutex_);
}

void Device::HciAclReadComplete(usb_request_t* req) {
  zxlogf(TRACE, "bt-transport-usb: ACL frame received");
  mtx_lock(&mutex_);

  if (req->response.status == ZX_ERR_IO_INVALID) {
    zxlogf(TRACE, "bt-transport-usb: request stalled, ignoring.");
    zx_status_t status = usb_req_list_add_head(&free_acl_read_reqs_, req, parent_req_size_);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    QueueAclReadRequestsLocked();

    mtx_unlock(&mutex_);
    return;
  }

  if (req->response.status != ZX_OK) {
    HandleUsbResponseError(req, "acl read");
    mtx_unlock(&mutex_);
    return;
  }

  void* buffer;
  zx_status_t status = usb_request_mmap(req, &buffer);
  if (status != ZX_OK) {
    zxlogf(ERROR, "bt-transport-usb: usb_req_mmap failed: %s", zx_status_get_string(status));
    mtx_unlock(&mutex_);
    return;
  }

  if (acl_channel_ == ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "bt-transport-usb: ACL data received while channel is closed");
  } else {
    status = zx_channel_write(acl_channel_.get(), 0, buffer,
                              static_cast<uint32_t>(req->response.actual), nullptr, 0);
    if (status < 0) {
      zxlogf(ERROR, "bt-transport-usb: hci_acl_read_complete failed to write: %s",
             zx_status_get_string(status));
    }
  }

  // If the snoop channel is open then try to write the packet even if acl_channel was closed.
  SnoopChannelWriteLocked(bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_ACL, true),
                          static_cast<uint8_t*>(buffer), req->response.actual);

  status = usb_req_list_add_head(&free_acl_read_reqs_, req, parent_req_size_);
  ZX_DEBUG_ASSERT(status == ZX_OK);
  QueueAclReadRequestsLocked();

  mtx_unlock(&mutex_);
}

void Device::HciAclWriteComplete(usb_request_t* req) {
  mtx_lock(&mutex_);

  if (req->response.status != ZX_OK) {
    HandleUsbResponseError(req, "acl write");
    mtx_unlock(&mutex_);
    return;
  }

  zx_status_t status = usb_req_list_add_tail(&free_acl_write_reqs_, req, parent_req_size_);
  ZX_DEBUG_ASSERT(status == ZX_OK);

  if (snoop_channel_) {
    void* buffer;
    zx_status_t status = usb_request_mmap(req, &buffer);
    if (status != ZX_OK) {
      zxlogf(ERROR, "bt-transport-usb: usb_req_mmap failed: %s", zx_status_get_string(status));
      mtx_unlock(&mutex_);
      return;
    }

    SnoopChannelWriteLocked(bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_ACL, false),
                            static_cast<uint8_t*>(buffer), req->response.actual);
  }

  mtx_unlock(&mutex_);
}

void Device::HciBuildReadWaitItemsLocked() {
  read_wait_items_.clear();

  if (cmd_channel_.is_valid()) {
    read_wait_items_.push_back(zx_wait_item_t{
        .handle = cmd_channel_.get(), .waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED});
  }
  if (acl_channel_.is_valid()) {
    read_wait_items_.push_back(zx_wait_item_t{
        .handle = acl_channel_.get(), .waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED});
  }
  read_wait_items_.push_back(
      zx_wait_item_t{.handle = channels_changed_evt_, .waitfor = ZX_EVENT_SIGNALED});
  read_wait_items_.push_back(zx_wait_item_t{.handle = unbind_evt_, .waitfor = ZX_EVENT_SIGNALED});

  zx_object_signal(channels_changed_evt_, ZX_EVENT_SIGNALED, 0);
}

void Device::HciBuildReadWaitItems() {
  mtx_lock(&mutex_);
  HciBuildReadWaitItemsLocked();
  mtx_unlock(&mutex_);
}

void Device::HciHandleCmdReadEvents(zx_wait_item_t* cmd_item) {
  if ((cmd_item->pending & (ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED)) == 0) {
    return;
  }
  uint8_t buf[CMD_BUF_SIZE];
  uint32_t length = sizeof(buf);
  zx_status_t status =
      zx_channel_read(cmd_item->handle, 0, buf, nullptr, length, 0, &length, nullptr);
  if (status < 0) {
    CloseChannelWithLog(&cmd_channel_, status, "command channel read failed");
    return;
  }

  status = usb_control_out(&usb_, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_DEVICE, 0, 0, 0,
                           ZX_TIME_INFINITE, buf, length);
  if (status < 0) {
    CloseChannelWithLog(&cmd_channel_, status, "usb_control_out failed");
    return;
  }

  mtx_lock(&mutex_);
  SnoopChannelWriteLocked(bt_hci_snoop_flags(BT_HCI_SNOOP_TYPE_CMD, false), buf, length);
  mtx_unlock(&mutex_);
}

void Device::HciHandleAclReadEvents(zx_wait_item_t* acl_item) {
  if (acl_item->pending & (ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED)) {
    mtx_lock(&mutex_);
    list_node_t* node = list_peek_head(&free_acl_write_reqs_);
    mtx_unlock(&mutex_);

    // We don't have enough reqs. Simply punt the channel read until later.
    if (!node)
      return;

    uint8_t buf[ACL_MAX_FRAME_SIZE];
    uint32_t length = sizeof(buf);
    zx_status_t status =
        zx_channel_read(acl_item->handle, 0, buf, nullptr, length, 0, &length, nullptr);
    if (status < 0) {
      CloseChannelWithLog(&acl_channel_, status, "ACL channel read failed");
      return;
    }

    mtx_lock(&mutex_);
    node = list_remove_head(&free_acl_write_reqs_);
    mtx_unlock(&mutex_);

    // At this point if we don't get a free node from |free_acl_write_reqs| that means that
    // they were cleaned up in hci_release(). Just drop the packet.
    if (!node)
      return;

    usb_req_internal_t* req_int = containerof(node, usb_req_internal_t, node);
    usb_request_t* req = REQ_INTERNAL_TO_USB_REQ(req_int, parent_req_size_);
    size_t result = usb_request_copy_to(req, buf, length, 0);
    ZX_ASSERT(result == length);
    req->header.length = length;
    UsbRequestSend(&usb_, req, [](void* ctx, usb_request_t* req) {
      static_cast<Device*>(ctx)->HciAclWriteComplete(req);
    });
  }
}

int Device::HciReadThread(void* void_dev) {
  Device* dev = static_cast<Device*>(void_dev);

  while (true) {
    // Make a copy of read_wait_items_ so that they can be waited on without the lock.
    mtx_lock(&dev->mutex_);
    std::vector<zx_wait_item_t> read_wait_items = dev->read_wait_items_;
    zx_handle_t cmd_channel = dev->cmd_channel_.get();
    zx_handle_t acl_channel = dev->acl_channel_.get();
    mtx_unlock(&dev->mutex_);

    zx_status_t status =
        zx_object_wait_many(read_wait_items.data(), read_wait_items.size(), ZX_TIME_INFINITE);
    if (status < 0) {
      zxlogf(ERROR, "%s: zx_object_wait_many failed (%s) - exiting", __FUNCTION__,
             zx_status_get_string(status));
      mtx_lock(&dev->mutex_);
      dev->ChannelCleanupLocked(&dev->cmd_channel_);
      dev->ChannelCleanupLocked(&dev->acl_channel_);
      mtx_unlock(&dev->mutex_);
      break;
    }

    for (auto item : read_wait_items) {
      if (item.handle == cmd_channel) {
        dev->HciHandleCmdReadEvents(&item);
      } else if (item.handle == acl_channel) {
        dev->HciHandleAclReadEvents(&item);
      } else if (item.handle == dev->unbind_evt_ && (item.pending & ZX_EVENT_SIGNALED)) {
        // The driver is being unbound, so terminate the read thread.
        zxlogf(DEBUG, "%s: unbinding", __FUNCTION__);
        return 0;
      } else if (item.handle == dev->channels_changed_evt_ && (item.pending & ZX_EVENT_SIGNALED)) {
        zxlogf(DEBUG, "%s: Ignoring channels changed event", __FUNCTION__);
      }
    }

    // The channels might have been changed by the *_read_events, recheck the event.
    status = zx_object_wait_one(dev->channels_changed_evt_, ZX_EVENT_SIGNALED, 0u, nullptr);
    if (status == ZX_OK) {
      zxlogf(DEBUG, "%s: handling channels changed event", __FUNCTION__);
      mtx_lock(&dev->mutex_);
      dev->HciBuildReadWaitItemsLocked();
      mtx_unlock(&dev->mutex_);
    }
  }

  zxlogf(DEBUG, "%s exiting", __FUNCTION__);
  return 0;
}

zx_status_t Device::HciOpenChannel(zx::channel* out, zx::channel in) {
  mtx_lock(&mutex_);
  mtx_lock(&pending_request_lock_);
  if (unbound_) {
    mtx_unlock(&pending_request_lock_);
    mtx_unlock(&mutex_);
    return ZX_ERR_CANCELED;
  }
  mtx_unlock(&pending_request_lock_);

  if (*out != ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "bt-transport-usb: already bound, failing");
    mtx_unlock(&mutex_);
    return ZX_ERR_ALREADY_BOUND;
  }

  *out = std::move(in);

  zxlogf(DEBUG, "%s: signaling channels changed", __FUNCTION__);
  // Poke the changed event to get the new channel.
  zx_object_signal(/*handle=*/channels_changed_evt_, /*clear_mask=*/0,
                   /*set_mask=*/ZX_EVENT_SIGNALED);

  mtx_unlock(&mutex_);
  return ZX_OK;
}

zx_status_t Device::BtHciOpenCommandChannel(zx::channel channel) {
  zxlogf(TRACE, "%s", __FUNCTION__);
  return HciOpenChannel(&cmd_channel_, std::move(channel));
}

zx_status_t Device::BtHciOpenAclDataChannel(zx::channel channel) {
  zxlogf(TRACE, "%s", __FUNCTION__);
  return HciOpenChannel(&acl_channel_, std::move(channel));
}

zx_status_t Device::BtHciOpenSnoopChannel(zx::channel channel) {
  zxlogf(TRACE, "%s", __FUNCTION__);

  mtx_lock(&mutex_);
  mtx_lock(&pending_request_lock_);
  if (unbound_) {
    mtx_unlock(&mutex_);
    mtx_unlock(&pending_request_lock_);
    return ZX_ERR_CANCELED;
  }
  mtx_unlock(&pending_request_lock_);

  // Initialize snoop_watch_ port for detecting if a previous client closed the channel.
  // This is only necessary for the first snoop client.
  if (snoop_watch_ == ZX_HANDLE_INVALID) {
    zx_status_t status = zx_port_create(0, &snoop_watch_);
    if (status != ZX_OK) {
      zxlogf(ERROR,
             "bt-transport-usb: failed to create a port to watch snoop channel: "
             "%s\n",
             zx_status_get_string(status));
      mtx_unlock(&mutex_);
      return status;
    }
  } else {
    // Check if previous snoop client closed the channel, in which case the new channel can be
    // configured.
    zx_port_packet_t packet;
    zx_status_t status = zx_port_wait(snoop_watch_, /*deadline=*/0, &packet);
    if (status == ZX_ERR_TIMED_OUT) {
      zxlogf(TRACE, "bt-transport-usb: snoop port wait timed out: %s",
             zx_status_get_string(status));
    } else if (packet.signal.observed & ZX_CHANNEL_PEER_CLOSED) {
      zxlogf(
          TRACE,
          "previous snoop channel peer closed; proceeding with configuration of new snoop channel");
      snoop_channel_.reset();
    }
  }

  if (snoop_channel_.is_valid()) {
    mtx_unlock(&mutex_);
    return ZX_ERR_ALREADY_BOUND;
  }

  snoop_channel_ = std::move(channel);
  // Send a signal to the snoop_watch_ port when the snoop channel is closed by the peer.
  zx_object_wait_async(snoop_channel_.get(), snoop_watch_, 0, ZX_CHANNEL_PEER_CLOSED, 0);
  mtx_unlock(&mutex_);
  return ZX_OK;
}

zx_status_t Device::AllocBtUsbPackets(int limit, uint64_t data_size, uint8_t ep_address,
                                      size_t req_size, list_node_t* list) {
  zx_status_t status;
  for (int i = 0; i < limit; i++) {
    usb_request_t* req;
    status = InstrumentedRequestAlloc(&req, data_size, ep_address, req_size);
    if (status != ZX_OK) {
      return status;
    }
    status = usb_req_list_add_head(list, req, parent_req_size_);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

void Device::OnBindFailure(zx_status_t status, const char* msg) {
  zxlogf(ERROR, "bind failed due to %s: %s", msg, zx_status_get_string(status));
  DdkRelease();
}

void Device::HandleUsbResponseError(usb_request_t* req, const char* msg) {
  zxlogf(ERROR, "%s request completed with error status %d (%s). Removing device", msg,
         req->response.status, zx_status_get_string(req->response.status));
  InstrumentedRequestRelease(req);
  RemoveDeviceLocked();
}

void Device::CloseChannelWithLog(zx::channel* channel, zx_status_t status, const char* msg) {
  zxlogf(ERROR, "hci_read_thread: %s, closing channel: %s", msg, zx_status_get_string(status));
  mtx_lock(&mutex_);
  ChannelCleanupLocked(channel);
  mtx_unlock(&mutex_);
}

// A lambda is used to create an empty instance of zx_driver_ops_t.
static zx_driver_ops_t usb_bt_hci_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Device::Create;
  return ops;
}();

}  // namespace bt_transport_usb

ZIRCON_DRIVER(bt_transport_usb, bt_transport_usb::usb_bt_hci_driver_ops, "zircon", "0.1");
