// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_UART_BT_TRANSPORT_UART_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_UART_BT_TRANSPORT_UART_H_

#include <fuchsia/hardware/bt/hci/cpp/banjo.h>
#include <fuchsia/hardware/serialimpl/async/c/banjo.h>
#include <lib/zx/event.h>
#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>

namespace bt_transport_uart {

class BtTransportUart;
using BtTransportUartType = ddk::Device<BtTransportUart, ddk::GetProtocolable, ddk::Unbindable>;

class BtTransportUart : public BtTransportUartType, public ddk::BtHciProtocol<BtTransportUart> {
 public:
  explicit BtTransportUart(zx_device_t* parent) : BtTransportUartType(parent) {}

  // Static bind function for the ZIRCON_DRIVER() declaration. Binds this device and passes
  // ownership to the driver manager.
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // DDK mixins:
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_proto);

  // ddk::BtHciProtocol mixins:
  zx_status_t BtHciOpenCommandChannel(zx::channel in);
  zx_status_t BtHciOpenAclDataChannel(zx::channel in);
  zx_status_t BtHciOpenSnoopChannel(zx::channel in);

 private:
  // HCI UART packet indicators
  enum BtHciPacketIndicator : uint8_t {
    kHciNone = 0,
    kHciCommand = 1,
    kHciAclData = 2,
    kHciSco = 3,
    kHciEvent = 4,
  };

  struct ClientChannel {
    zx_handle_t handle = ZX_HANDLE_INVALID;
    zx_status_t err = ZX_OK;
  };

  struct HciWriteCtx {
    BtTransportUart* ctx;
    // Owned.
    uint8_t* buffer;
  };

  // Returns length of current event packet being received
  size_t EventPacketLength();

  // Returns length of current ACL data packet being received
  size_t AclPacketLength();

  static void ChannelCleanupLocked(ClientChannel* channel);

  void SnoopChannelWriteLocked(uint8_t flags, uint8_t* bytes, size_t length);

  void HciBeginShutdown();

  void SerialWrite(void* buffer, size_t length);

  void HciHandleClientChannel(ClientChannel* chan, zx_signals_t pending);

  void HciHandleUartReadEvents(const uint8_t* buf, size_t length);

  void HciReadComplete(zx_status_t status, const uint8_t* buffer, size_t length);

  void HciWriteComplete(zx_status_t status);

  static int HciThread(void* arg);

  zx_status_t HciOpenChannel(ClientChannel* in_channel, zx_handle_t in);

  // Adds the device.
  zx_status_t Bind();

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

  serial_impl_async_protocol_t serial_;

  ClientChannel cmd_channel_;
  ClientChannel acl_channel_;
  ClientChannel snoop_channel_;

  // Signaled any time something changes that the work thread needs to know about.
  zx::event wakeup_event_;

  thrd_t thread_;
  std::atomic_bool shutting_down_;
  bool thread_running_;
  bool can_write_;

  // type of current packet being read from the UART
  BtHciPacketIndicator cur_uart_packet_type_;

  // for accumulating HCI events
  uint8_t event_buffer_[kEventBufSize];
  size_t event_buffer_offset_;

  // for accumulating ACL data packets
  uint8_t acl_buffer_[kAclMaxFrameSize];
  size_t acl_buffer_offset_;

  mtx_t mutex_;
};

}  // namespace bt_transport_uart

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_TRANSPORT_UART_BT_TRANSPORT_UART_H_
