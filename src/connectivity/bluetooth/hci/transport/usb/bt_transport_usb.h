// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_USB_BT_TRANSPORT_USB_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_USB_BT_TRANSPORT_USB_H_

#include <fuchsia/hardware/bt/hci/cpp/banjo.h>
#include <lib/sync/completion.h>
#include <threads.h>

#include <ddktl/device.h>
#include <usb/usb.h>

#include "src/lib/listnode/listnode.h"

namespace bt_transport_usb {

// See ddk::Device in ddktl/device.h
class Device;
using DeviceType = ddk::Device<Device, ddk::GetProtocolable, ddk::Unbindable>;

// This driver can be bound to devices requiring the ZX_PROTOCOL_BT_TRANSPORT protocol, but this
// driver actually implements the ZX_PROTOCOL_BT_HCI protocol. Drivers that bind to
// ZX_PROTOCOL_BT_HCI should never bind to this driver directly, but instead bind to a vendor
// driver.
//
// BtHciProtocol is not a ddk::base_protocol because vendor drivers proxy requests to this driver.
class Device final : public DeviceType, public ddk::BtHciProtocol<Device> {
 public:
  explicit Device(zx_device_t* parent) : DeviceType(parent) {}

  // Static bind function for the ZIRCON_DRIVER() declaration. Binds a Device and passes ownership
  // to the driver manager.
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Adds the device.
  zx_status_t Bind();

  // Methods required by DDK mixins:
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  // ddk::BtHciProtocol mixins:
  zx_status_t BtHciOpenCommandChannel(zx::channel channel);
  zx_status_t BtHciOpenAclDataChannel(zx::channel channel);
  zx_status_t BtHciOpenSnoopChannel(zx::channel channel);

 private:
  // The number of currently supported HCI channel endpoints. We currently have
  // one channel for command/event flow and one for ACL data flow. The snoop channel is managed
  // separately.
  static const int kNumChannels = 2;

  // changed event, unbind event
  static const int kNumReadWaitEvents = 2;

  static const int kEventBufSize = 255 + 2;  // 2 byte header + payload

  using usb_callback_t = void (*)(void*, usb_request_t*);

  zx_status_t InstrumentedRequestAlloc(usb_request_t** out, uint64_t data_size, uint8_t ep_address,
                                       size_t req_size);

  void InstrumentedRequestRelease(usb_request_t* req);

  void UsbRequestCallback(usb_request_t* req);

  void UsbRequestSend(usb_protocol_t* function, usb_request_t* req, usb_callback_t callback);

  void QueueAclReadRequestsLocked() __TA_REQUIRES(mutex_);

  void QueueInterruptRequestsLocked() __TA_REQUIRES(mutex_);

  void ChannelCleanupLocked(zx::channel* channel);

  void SnoopChannelWriteLocked(uint8_t flags, uint8_t* bytes, size_t length) __TA_REQUIRES(mutex_);

  void RemoveDeviceLocked() __TA_REQUIRES(mutex_);

  void HciEventComplete(usb_request_t* req);

  void HciAclReadComplete(usb_request_t* req);

  void HciAclWriteComplete(usb_request_t* req);

  void HciBuildReadWaitItemsLocked() __TA_REQUIRES(mutex_);

  void HciBuildReadWaitItems();

  // Handle a readable or closed signal from the command channel.
  void HciHandleCmdReadEvents(zx_wait_item_t* cmd_item);

  // Handle a readable or closed signal from the ACL channel.
  void HciHandleAclReadEvents(zx_wait_item_t* acl_item);

  // The read thread reads outbound command and ACL packets from the command and ACL channels and
  // forwards them to the USB device.
  static int HciReadThread(void* void_dev);

  zx_status_t HciOpenChannel(zx::channel* out, zx::channel in);

  zx_status_t AllocBtUsbPackets(int limit, uint64_t data_size, uint8_t ep_address, size_t req_size,
                                list_node_t* list);

  // Called upon Bind failure.
  void OnBindFailure(zx_status_t status, const char* msg);

  void HandleUsbResponseError(usb_request_t* req, const char* msg) __TA_REQUIRES(mutex_);

  void CloseChannelWithLog(zx::channel* channel, zx_status_t status, const char* msg);

  usb_protocol_t usb_ __TA_GUARDED(mutex_);

  zx::channel cmd_channel_ __TA_GUARDED(mutex_);
  zx::channel acl_channel_ __TA_GUARDED(mutex_);
  zx::channel snoop_channel_ __TA_GUARDED(mutex_);

  // Port to queue PEER_CLOSED signals on
  zx_handle_t snoop_watch_ = ZX_HANDLE_INVALID;

  // Signaled when a channel opens or closes
  zx_handle_t channels_changed_evt_ = ZX_HANDLE_INVALID;

  // Signaled when the driver is unbound.
  zx_handle_t unbind_evt_ = ZX_HANDLE_INVALID;

  // Waits for channel signale and events that the read thread waits on.
  std::vector<zx_wait_item_t> read_wait_items_ __TA_GUARDED(mutex_);

  thrd_t read_thread_ __TA_GUARDED(mutex_) = 0;

  // for accumulating HCI events
  uint8_t event_buffer_[kEventBufSize] __TA_GUARDED(mutex_);
  size_t event_buffer_offset_ __TA_GUARDED(mutex_) = 0u;
  size_t event_buffer_packet_length_ __TA_GUARDED(mutex_) = 0u;

  // pool of free USB requests
  list_node_t free_event_reqs_ __TA_GUARDED(mutex_);
  list_node_t free_acl_read_reqs_ __TA_GUARDED(mutex_);
  list_node_t free_acl_write_reqs_ __TA_GUARDED(mutex_);

  mtx_t mutex_;
  size_t parent_req_size_ = 0u;
  std::atomic_size_t allocated_requests_count_ = 0u;
  std::atomic_size_t pending_request_count_ = 0u;
  sync_completion_t requests_freed_completion_;

  // Whether or not we are being unbound.
  bool unbound_ __TA_GUARDED(pending_request_lock_) = false;

  // pending_request_lock may be held whether or not mutex is held.
  // If mutex is held, this must be acquired AFTER mutex is locked.
  // Should never be acquired before mutex.
  mtx_t pending_request_lock_;
  cnd_t pending_requests_completed_;
};

}  // namespace bt_transport_usb

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_USB_BT_TRANSPORT_USB_H_
