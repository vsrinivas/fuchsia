// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/bt/hci.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/sdio.h>
#include <fbl/auto_lock.h>
#include <lib/zx/channel.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/vmo.h>
#include <threads.h>
#include <lib/zircon-internal/thread_annotations.h>

namespace bluetooth {

class BtHciMediatek;
using DeviceType = ddk::Device<BtHciMediatek, ddk::GetProtocolable>;

class BtHciMediatek : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_BT_TRANSPORT> {
 public:
  BtHciMediatek(zx_device_t* parent, const ddk::SdioProtocolClient& sdio, zx::port port,
                size_t fw_part_max_size)
      : DeviceType(parent),
        sdio_(sdio),
        port_(std::move(port)),
        fw_part_max_size_(fw_part_max_size) {}
  virtual ~BtHciMediatek() = default;

  void DdkRelease();

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);

  // Visible for testing.
  enum FirmwarePartMode {
    kFirmwarePartFirst = 1,
    kFirmwarePartContinue = 2,
    kFirmwarePartLast = 3
  };

  zx_status_t CardSendVendorPacket(uint8_t id, uint8_t ocf, uint8_t* packet, size_t* size,
                                   size_t buffer_size);

  zx_status_t CardDownloadFirmware(const zx::vmo& vmo, size_t fw_size);

 protected:
  // Visible for testing.
  virtual zx_status_t CardRead32(uint32_t address, uint32_t* value);
  virtual zx_status_t CardWrite32(uint32_t address, uint32_t value);
  virtual zx_status_t CardRecvPacket(uint32_t* size);

  virtual zx_status_t CardReset();

  virtual zx_status_t CardGetHwVersion(uint32_t* version);
  virtual zx_status_t CardSendFirmwarePart(zx_handle_t vmo, uint8_t* buffer, const uint8_t* fw_data,
                                           size_t size, FirmwarePartMode mode);

 private:
  enum PacketKey {
    kSdioInterruptKey,
    kCommandChannelKey,
    kAclChannelKey,
    kSnoopChannelKey,
    kStopThreadKey
  };

  static zx_status_t OpenCommandChannel(void* ctx, zx_handle_t in_handle);
  static zx_status_t OpenAclDataChannel(void* ctx, zx_handle_t in_handle);
  static zx_status_t OpenSnoopChannel(void* ctx, zx_handle_t in_handle);

  zx_status_t Init(const zx::vmo& fw_vmo, size_t fw_size);

  zx_status_t BtHciOpenCommandChannel(zx_handle_t in);
  zx_status_t BtHciOpenAclDataChannel(zx_handle_t in);
  zx_status_t BtHciOpenSnoopChannel(zx_handle_t in);

  zx_status_t OpenChannel(zx::channel* in_channel, zx_handle_t in, PacketKey key);

  zx_status_t CardEnableInterrupt();
  zx_status_t CardDisableInterrupt();

  zx_status_t CardSetOwn(bool driver);
  zx_status_t CardSetPower(bool on);

  int CardGetFirmwareStatus();

  zx_status_t HandleCardInterrupt() TA_REQ(thread_mutex_);
  zx_status_t HostToCardPacket(const zx::channel& channel, uint8_t packet_type, uint32_t snoop_type)
      TA_REQ(thread_mutex_);

  int Thread();

  const ddk::SdioProtocolClient sdio_;
  zx::interrupt sdio_int_;
  zx::port port_;
  fbl::Mutex thread_mutex_;
  zx::channel cmd_channel_ TA_GUARDED(thread_mutex_);
  zx::channel acl_channel_ TA_GUARDED(thread_mutex_);
  zx::channel snoop_channel_ TA_GUARDED(thread_mutex_);
  thrd_t thread_;
  bool thread_running_ TA_GUARDED(thread_mutex_) = false;
  bt_hci_protocol_ops_t protocol_ops_ = {.open_command_channel = OpenCommandChannel,
                                         .open_acl_data_channel = OpenAclDataChannel,
                                         .open_snoop_channel = OpenSnoopChannel};
  const size_t fw_part_max_size_;
};

}  // namespace bluetooth
