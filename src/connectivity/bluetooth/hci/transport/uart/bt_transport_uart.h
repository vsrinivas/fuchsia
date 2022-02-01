// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_UART_BT_TRANSPORT_UART_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_UART_BT_TRANSPORT_UART_H_

#include <fuchsia/hardware/bt/hci/cpp/banjo.h>
#include <fuchsia/hardware/serialimpl/async/c/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/fit/thread_checker.h>
#include <lib/zx/event.h>
#include <threads.h>

#include <mutex>

#include <ddktl/device.h>
#include <ddktl/fidl.h>

namespace bt_transport_uart {

class BtTransportUart;
using BtTransportUartType = ddk::Device<BtTransportUart, ddk::GetProtocolable, ddk::Unbindable>;

class BtTransportUart : public BtTransportUartType, public ddk::BtHciProtocol<BtTransportUart> {
 public:
  // If |dispatcher| is non-null, it will be used instead of a new work thread.
  // tests.
  explicit BtTransportUart(zx_device_t* parent, async_dispatcher_t* dispatcher);

  // Static bind function for the ZIRCON_DRIVER() declaration. Binds this device and passes
  // ownership to the driver manager.
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Constructor for tests to inject a dispatcher for the work thread.
  static zx_status_t Create(zx_device_t* parent, async_dispatcher_t* dispatcher);

  // DDK mixins:
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_proto);

  // ddk::BtHciProtocol mixins:
  zx_status_t BtHciOpenCommandChannel(zx::channel in);
  zx_status_t BtHciOpenAclDataChannel(zx::channel in);
  zx_status_t BtHciOpenSnoopChannel(zx::channel in);
  zx_status_t BtHciOpenScoChannel(zx::channel channel);
  void BtHciConfigureSco(sco_coding_format_t coding_format, sco_encoding_t encoding,
                         sco_sample_rate_t sample_rate, bt_hci_configure_sco_callback callback,
                         void* cookie);
  void BtHciResetSco(bt_hci_reset_sco_callback callback, void* cookie);

 private:
  // HCI UART packet indicators
  enum BtHciPacketIndicator : uint8_t {
    kHciNone = 0,
    kHciCommand = 1,
    kHciAclData = 2,
    kHciSco = 3,
    kHciEvent = 4,
  };

  struct HciWriteCtx {
    BtTransportUart* ctx;
    // Owned.
    uint8_t* buffer;
  };

  // This wrapper around async_wait enables us to get a BtTransportUart* in the handler.
  // We use this instead of async::WaitMethod because async::WaitBase isn't thread safe.
  struct Wait : public async_wait {
    explicit Wait(BtTransportUart* uart, zx::channel* channel);
    static void Handler(async_dispatcher_t* dispatcher, async_wait_t* async_wait,
                        zx_status_t status, const zx_packet_signal_t* signal);
    BtTransportUart* uart;
    // Indicates whether a wait has begun and not ended.
    bool pending = false;
    // The channel that this wait waits on.
    zx::channel* channel;
  };

  // Returns length of current event packet being received
  // Must only be called in the read callback (HciHandleUartReadEvents).
  size_t EventPacketLength();

  // Returns length of current ACL data packet being received
  // Must only be called in the read callback (HciHandleUartReadEvents).
  size_t AclPacketLength();

  void ChannelCleanupLocked(zx::channel* channel) __TA_REQUIRES(mutex_);

  void SnoopChannelWriteLocked(uint8_t flags, uint8_t* bytes, size_t length) __TA_REQUIRES(mutex_);

  void HciBeginShutdown() __TA_EXCLUDES(mutex_);

  void SerialWrite(uint8_t* buffer, size_t length) __TA_EXCLUDES(mutex_);

  void HciHandleClientChannel(zx::channel* chan, zx_signals_t pending) __TA_EXCLUDES(mutex_);

  void HciHandleUartReadEvents(const uint8_t* buf, size_t length) __TA_EXCLUDES(mutex_);

  void HciReadComplete(zx_status_t status, const uint8_t* buffer, size_t length)
      __TA_EXCLUDES(mutex_);

  void HciWriteComplete(zx_status_t status) __TA_EXCLUDES(mutex_);

  static int HciThread(void* arg) __TA_EXCLUDES(mutex_);

  void OnChannelSignal(Wait* wait, zx_status_t status, const zx_packet_signal_t* signal);

  zx_status_t HciOpenChannel(zx::channel* in_channel, zx_handle_t in) __TA_EXCLUDES(mutex_);

  // Adds the device.
  zx_status_t Bind() __TA_EXCLUDES(mutex_);

  // 1 byte packet indicator + 3 byte header + payload
  static constexpr uint32_t kCmdBufSize = 255 + 4;

  // The number of currently supported HCI channel endpoints. We currently have
  // one channel for command/event flow and one for ACL data flow. The sniff channel is managed
  // separately.
  static constexpr uint8_t kNumChannels = 2;

  // add one for the wakeup event
  static constexpr uint8_t kNumWaitItems = kNumChannels + 1;

  // The maximum HCI ACL frame size used for data transactions
  // (1024 + 4 bytes for the ACL header + 1 byte packet indicator)
  static constexpr uint32_t kAclMaxFrameSize = 1029;

  // 1 byte packet indicator + 2 byte header + payload
  static constexpr uint32_t kEventBufSize = 255 + 3;

  serial_impl_async_protocol_t serial_ __TA_GUARDED(mutex_);

  zx::channel cmd_channel_ __TA_GUARDED(mutex_);
  Wait cmd_channel_wait_ __TA_GUARDED(mutex_){this, &cmd_channel_};

  zx::channel acl_channel_ __TA_GUARDED(mutex_);
  Wait acl_channel_wait_ __TA_GUARDED(mutex_){this, &acl_channel_};

  zx::channel snoop_channel_ __TA_GUARDED(mutex_);

  std::atomic_bool shutting_down_ = false;

  // True if there is not a UART write pending. Set to false when a write is initiated, and set to
  // true when the write completes.
  bool can_write_ __TA_GUARDED(mutex_) = true;

  // type of current packet being read from the UART
  // Must only be used in the UART read callback (HciHandleUartReadEvents).
  BtHciPacketIndicator cur_uart_packet_type_ = kHciNone;

  // for accumulating HCI events
  // Must only be used in the UART read callback (HciHandleUartReadEvents).
  uint8_t event_buffer_[kEventBufSize];
  // Must only be used in the UART read callback (HciHandleUartReadEvents).
  size_t event_buffer_offset_ = 0;

  // for accumulating ACL data packets
  // Must only be used in the UART read callback (HciHandleUartReadEvents).
  uint8_t acl_buffer_[kAclMaxFrameSize];
  // Must only be used in the UART read callback (HciHandleUartReadEvents).
  size_t acl_buffer_offset_ = 0;

  // for sending outbound packets to the UART
  uint8_t write_buffer_[std::max(kEventBufSize, kAclMaxFrameSize)] __TA_GUARDED(mutex_);

  std::mutex mutex_;

  std::optional<async::Loop> loop_;
  // In production, this is loop_.dispatcher(). In tests, this is the test dispatcher.
  async_dispatcher_t* dispatcher_ = nullptr;
};

}  // namespace bt_transport_uart

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_UART_BT_TRANSPORT_UART_H_
