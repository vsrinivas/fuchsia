// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_USB_BT_TRANSPORT_USB_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_USB_BT_TRANSPORT_USB_H_

#include <fuchsia/hardware/bt/hci/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sync/completion.h>

#include <mutex>
#include <queue>

#include <ddktl/device.h>
#include <usb/usb.h>

#include "packet_reassembler.h"
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
  // If |dispatcher| is non-null, it will be used instead of a new work thread.
  // tests.
  explicit Device(zx_device_t* parent, async_dispatcher_t* dispatcher);

  // Static bind function for the ZIRCON_DRIVER() declaration. Binds a Device and passes ownership
  // to the driver manager.
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Constructor for tests to inject a dispatcher for the work thread.
  static zx_status_t Create(zx_device_t* parent, async_dispatcher_t* dispatcher);

  // Adds the device.
  zx_status_t Bind();

  // Methods required by DDK mixins:
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  // ddk::BtHciProtocol mixins:
  zx_status_t BtHciOpenCommandChannel(zx::channel channel);
  zx_status_t BtHciOpenAclDataChannel(zx::channel channel);
  zx_status_t BtHciOpenScoChannel(zx::channel channel);
  void BtHciConfigureSco(sco_coding_format_t coding_format, sco_encoding_t encoding,
                         sco_sample_rate_t sample_rate, bt_hci_configure_sco_callback callback,
                         void* cookie);
  void BtHciResetSco(bt_hci_reset_sco_callback callback, void* cookie);
  zx_status_t BtHciOpenSnoopChannel(zx::channel channel);

 private:
  struct IsocEndpointDescriptors {
    usb_endpoint_descriptor_t in;
    usb_endpoint_descriptor_t out;
  };

  struct IsocAltSettingRequest {
    uint8_t alt_setting;
    bt_hci_configure_sco_callback callback;
    // The pointer to pass to callback.
    void* cookie;
  };

  class ChannelWrapper;

  // This wrapper around async_wait enables us to get a Device* in the handler.
  // We use this instead of async::WaitMethod because async::WaitBase isn't thread safe.
  struct Wait : public async_wait {
    explicit Wait(Device* device, ChannelWrapper* channel);
    static void Handler(async_dispatcher_t* dispatcher, async_wait_t* async_wait,
                        zx_status_t status, const zx_packet_signal_t* signal);
    Device* device;
    // Indicates whether a wait has begun and not ended.
    bool pending = false;
    // The channel that this wait waits on.
    ChannelWrapper* channel;
  };

  // This wrapper around zx::channel enables us to process handles more generically and tightly
  // couple a channel with its Wait.
  class ChannelWrapper {
   public:
    using SignalHandler = void (Device::*)(zx_signals_t);

    explicit ChannelWrapper(const char* name, Device* device, SignalHandler handler)
        : device_(device), wait_(device, this), signal_handler_(handler), name_(name) {}

    bool IsOpen() const { return channel_.is_valid(); }

    void CleanUp();

    bool WaitPending() const { return wait_.pending; }

    // Begins waiting for signals. Cleans up the channel on error.
    // Returns true if the wait was successfully started.
    bool BeginWait();

    zx_status_t Read(void* bytes, uint32_t num_bytes, uint32_t* actual_bytes) {
      return channel_.read(/*flags=*/0, bytes, /*handles=*/nullptr, num_bytes, /*num_handles=*/0,
                           actual_bytes, /*actual_handles=*/nullptr);
    }

    zx_status_t Write(const void* bytes, uint32_t num_bytes) {
      return channel_.write(/*flags=*/0, bytes, num_bytes, /*handles=*/nullptr, /*num_handles=*/0);
    }

    void set_channel(zx::channel channel) {
      channel_ = std::move(channel);
      wait_.object = channel_.get();
    }

    const char* name() const { return name_; }

   private:
    friend struct Wait;

    Device* device_;
    zx::channel channel_;
    Wait wait_;
    const SignalHandler signal_handler_;
    const char* const name_;
  };

  // The number of currently supported HCI channel endpoints. We currently have
  // one channel for command/event flow and one for ACL data flow. The snoop channel is managed
  // separately.
  static const int kNumChannels = 2;

  static const int kEventBufSize = 255 + 2;  // 2 byte header + payload

  static const int kScoMaxPacketSize = 255 + 3;  // 3 byte header + payload

  using usb_callback_t = void (*)(void*, usb_request_t*);

  void ReadIsocInterfaces(usb_desc_iter_t* config_desc_iter);

  zx_status_t InstrumentedRequestAlloc(usb_request_t** out, uint64_t data_size, uint8_t ep_address,
                                       size_t req_size);

  void InstrumentedRequestRelease(usb_request_t* req);

  void UsbRequestCallback(usb_request_t* req);

  void UsbRequestSend(usb_protocol_t* function, usb_request_t* req, usb_callback_t callback);

  void QueueAclReadRequestsLocked() __TA_REQUIRES(mutex_);

  void QueueScoReadRequestsLocked() __TA_REQUIRES(mutex_);

  void QueueInterruptRequestsLocked() __TA_REQUIRES(mutex_);

  void SnoopChannelWriteLocked(uint8_t flags, const uint8_t* bytes, size_t length)
      __TA_REQUIRES(mutex_);

  // Requests removal of this device. Idempotent.
  void RemoveDeviceLocked() __TA_REQUIRES(mutex_);

  void HciEventComplete(usb_request_t* req);

  void HciAclReadComplete(usb_request_t* req);

  void HciAclWriteComplete(usb_request_t* req);

  void HciScoReadComplete(usb_request_t* req);

  // Called by sco_reassembler_ when a packet is recombined.
  // This method assumes mutex_ is held during invocation. We disable thread safety analysis because
  // the PacketReassembler callback is too complex for Clang. sco_reassembler_ requires mutex_, so
  // this method effectively requires mutex_.
  void OnScoReassemblerPacketLocked(cpp20::span<const uint8_t> packet)
      __TA_NO_THREAD_SAFETY_ANALYSIS;

  void HciScoWriteComplete(usb_request_t* req);

  // Handle a readable or closed signal from the command channel.
  void HciHandleCmdReadEvents(zx_signals_t);

  // Handle a readable or closed signal from the ACL channel.
  void HciHandleAclReadEvents(zx_signals_t);

  // Handle a readable or closed signal from the SCO channel.
  void HciHandleScoReadEvents(zx_signals_t);

  // Handle a readable or closed signal from the snoop channel.
  void HciHandleSnoopSignals(zx_signals_t);

  zx_status_t HciOpenChannel(ChannelWrapper* out, zx::channel in);

  zx_status_t AllocBtUsbPackets(int limit, uint64_t data_size, uint8_t ep_address, size_t req_size,
                                list_node_t* list);

  // Called upon Bind failure.
  void OnBindFailure(zx_status_t status, const char* msg);

  void HandleUsbResponseError(usb_request_t* req, const char* req_description)
      __TA_REQUIRES(mutex_);

  void ProcessNextIsocAltSettingRequest();

  mtx_t mutex_;

  usb_protocol_t usb_ __TA_GUARDED(mutex_);

  std::optional<async::Loop> loop_;
  // In production, this is loop_.dispatcher(). In tests, this is the test dispatcher.
  async_dispatcher_t* dispatcher_ = nullptr;

  ChannelWrapper cmd_channel_ __TA_GUARDED(mutex_){"command", this,
                                                   &Device::HciHandleCmdReadEvents};
  ChannelWrapper acl_channel_ __TA_GUARDED(mutex_){"ACL", this, &Device::HciHandleAclReadEvents};
  ChannelWrapper sco_channel_ __TA_GUARDED(mutex_){"SCO", this, &Device::HciHandleScoReadEvents};
  ChannelWrapper snoop_channel_ __TA_GUARDED(mutex_){"snoop", this, &Device::HciHandleSnoopSignals};

  // Set during binding and never modified after.
  std::optional<usb_endpoint_descriptor_t> bulk_out_endp_desc_;
  std::optional<usb_endpoint_descriptor_t> bulk_in_endp_desc_;
  std::optional<usb_endpoint_descriptor_t> intr_endp_desc_;

  // The alternate setting of the ISOC (SCO) interface.
  uint8_t isoc_alt_setting_ __TA_GUARDED(mutex_) = 0;

  std::queue<IsocAltSettingRequest> isoc_alt_setting_requests_ __TA_GUARDED(mutex_);

  // If true, ISOC out requests may be queued.
  // Must only be modified while isoc_alt_setting_mutex_ is held.
  bool isoc_alt_setting_being_changed_ __TA_GUARDED(mutex_) = false;

  // Set during bind, never modified afterwards.
  std::vector<IsocEndpointDescriptors> isoc_endp_descriptors_;

  // for accumulating HCI events
  uint8_t event_buffer_[kEventBufSize] __TA_GUARDED(mutex_);
  size_t event_buffer_offset_ __TA_GUARDED(mutex_) = 0u;
  size_t event_buffer_packet_length_ __TA_GUARDED(mutex_) = 0u;

  PacketReassembler<kScoMaxPacketSize> sco_reassembler_ __TA_GUARDED(mutex_);

  // pool of free USB requests
  list_node_t free_event_reqs_ __TA_GUARDED(mutex_);
  list_node_t free_acl_read_reqs_ __TA_GUARDED(mutex_);
  list_node_t free_acl_write_reqs_ __TA_GUARDED(mutex_);
  list_node_t free_sco_read_reqs_ __TA_GUARDED(mutex_);
  list_node_t free_sco_write_reqs_ __TA_GUARDED(mutex_);

  size_t parent_req_size_ = 0u;
  std::atomic_size_t allocated_requests_count_ = 0u;
  std::atomic_size_t pending_request_count_ = 0u;
  std::atomic_size_t pending_sco_write_request_count_ = 0u;
  cnd_t pending_sco_write_request_count_0_cnd_;
  sync_completion_t requests_freed_completion_;

  // Whether or not we are being unbound.
  bool unbound_ __TA_GUARDED(pending_request_lock_) = false;

  // Set to true when RemoveDeviceLocked() has been called.
  bool remove_requested_ __TA_GUARDED(mutex_) = false;

  // Locked while sending a request, when handling a request callback, or when unbinding.
  // Useful when any operation needs to terminate if the driver is being unbound.
  // Also used to receive condition signals from request callbacks (e.g. indicating 0 pending
  // requests remain).
  // This is separate from mutex_ so that request operations don't need to acquire mutex_ (which
  // may degrade performance).
  mtx_t pending_request_lock_ __TA_ACQUIRED_AFTER(mutex_);
};

}  // namespace bt_transport_usb

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_USB_BT_TRANSPORT_USB_H_
