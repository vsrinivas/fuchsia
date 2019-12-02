// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bt-hci-mediatek.h"

#include <endian.h>
#include <lib/device-protocol/pdev.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/time.h>
#include <zircon/device/bt-hci.h>

#include <algorithm>
#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <hw/sdio.h>

namespace {

constexpr size_t kBlockSize = 256;

constexpr uint8_t kPacketTypeCmd = 1;
constexpr uint8_t kPacketTypeAcl = 2;
constexpr uint8_t kPacketTypeSco = 3;
constexpr uint8_t kPacketTypeEvent = 4;

constexpr uint8_t kAclPacketHeaderSize = 4;
constexpr size_t kAclPacketSizeOffset = 2;

constexpr uint8_t kEventPacketHeaderSize = 2;
constexpr size_t kEventPacketSizeOffset = 1;

// From the Mediatek driver source. Round down to the block size for convenience.
constexpr size_t kMaxPacketSize = fbl::round_down<size_t, size_t>(2000, kBlockSize);

constexpr uint32_t kSdioHeaderSize = 4;
constexpr uint32_t kHciPacketHeaderSize = kSdioHeaderSize + 1;

constexpr size_t kFwHeaderSize = 30;

constexpr size_t kFwPartHeaderSize = 14;
constexpr size_t kFwPartMaxSize = kMaxPacketSize - kFwPartHeaderSize;

constexpr int kFirmwareReady = 1;
constexpr int kFirmwareNeedDownload = 2;

// Card register addresses and bits.
constexpr uint32_t kChlpcrAddress = 0x04;
constexpr uint32_t kChlpcrFwIntSet = 0x00000001;
constexpr uint32_t kChlpcrFwIntClear = 0x00000002;
constexpr uint32_t kChlpcrFwOwn = 0x00000100;
constexpr uint32_t kChlpcrDriverOwn = 0x00000200;

constexpr uint32_t kCsdioCsrAddress = 0x08;
constexpr uint32_t kCsdioCsrClockFix = 0x00000004;

constexpr uint32_t kChcrAddress = 0x0c;
constexpr uint32_t kChcrWriteClear = 0x00000002;

constexpr uint32_t kChisrAddress = 0x10;
constexpr uint32_t kChierAddress = 0x14;
constexpr uint32_t kIsrRxDone = 0x00000002;
constexpr uint32_t kIsrTxEmpty = 0x00000004;
constexpr uint32_t kIsrTxUnderThreshold = 0x00000008;
constexpr uint32_t kIsrTxCompleteCount = 0x00000070;
constexpr uint32_t kIsrFwInd = 0x00000080;
constexpr uint32_t kIsrTxFifoOverflow = 0x00000100;
constexpr uint32_t kIsrFw = 0x0000fe00;
constexpr uint32_t kIsrRxPacketSizeMask = 0xffff0000;
constexpr uint32_t kIsrRxPacketSizeShift = 16;
constexpr uint32_t kIsrAll = kIsrRxDone | kIsrTxEmpty | kIsrTxUnderThreshold | kIsrTxCompleteCount |
                             kIsrFwInd | kIsrTxFifoOverflow | kIsrFw;

constexpr uint32_t kCtdrAddress = 0x18;
constexpr uint32_t kCrdrAddress = 0x1c;

void SetSizeField(void* packet, size_t size) {
  uint16_t size_le = htole16(static_cast<uint16_t>(size));
  memcpy(packet, &size_le, sizeof(size_le));
}

uint16_t GetSizeField(const uint8_t* packet) {
  return static_cast<uint16_t>(packet[0] | (packet[1] << 8));
}

}  // namespace

namespace bluetooth {

zx_status_t BtHciMediatek::OpenCommandChannel(void* ctx, zx_handle_t in_handle) {
  return reinterpret_cast<BtHciMediatek*>(ctx)->BtHciOpenCommandChannel(in_handle);
}

zx_status_t BtHciMediatek::OpenAclDataChannel(void* ctx, zx_handle_t in_handle) {
  return reinterpret_cast<BtHciMediatek*>(ctx)->BtHciOpenAclDataChannel(in_handle);
}

zx_status_t BtHciMediatek::OpenSnoopChannel(void* ctx, zx_handle_t in_handle) {
  return reinterpret_cast<BtHciMediatek*>(ctx)->BtHciOpenSnoopChannel(in_handle);
}

void BtHciMediatek::DdkRelease() {
  bool join_thread = false;

  {
    fbl::AutoLock lock(&thread_mutex_);

    if (thread_running_) {
      join_thread = true;

      // Send a packet to port_ to wake up the thread, then release the lock and join.
      zx_port_packet_t packet;
      packet.key = kStopThreadKey;
      packet.type = ZX_PKT_TYPE_USER;
      if (port_.queue(&packet) != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to queue packet\n", __FILE__);
      }
    }
  }

  if (join_thread) {
    thrd_join(thread_, nullptr);
  }

  delete this;
}

zx_status_t BtHciMediatek::Create(void* ctx, zx_device_t* parent) {
  ddk::SdioProtocolClient sdio(parent);
  if (!sdio.is_valid()) {
    zxlogf(ERROR, "%s: Failed to get SDIO protocol\n", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  zx::port port;
  zx_status_t status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to create port\n", __FILE__);
    return status;
  }

  zx::vmo fw_vmo;
  size_t fw_size;
  status =
      load_firmware(parent, "mt7668_patch_e2_hdr.bin", fw_vmo.reset_and_get_address(), &fw_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to load firmware\n", __FILE__);
    return status;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<BtHciMediatek> device(
      new (&ac) BtHciMediatek(parent, sdio, std::move(port), kFwPartMaxSize));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: BtHciMediatek alloc failed\n", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  if ((device->Init(fw_vmo, fw_size)) != ZX_OK) {
    return status;
  }

  if ((status = device->DdkAdd("bt-hci-mediatek")) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed\n", __FILE__);
    return status;
  }

  __UNUSED auto* dummy = device.release();

  return ZX_OK;
}

zx_status_t BtHciMediatek::Init(const zx::vmo& fw_vmo, size_t fw_size) {
  zx_status_t status = sdio_.GetInBandIntr(&sdio_int_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get SDIO interrupt\n", __FILE__);
    return status;
  }

  if ((status = sdio_int_.bind(port_, kSdioInterruptKey, 0)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to bind interrupt to port\n", __FILE__);
    return status;
  }

  if ((status = sdio_.EnableFn()) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to set function\n", __FILE__);
    return status;
  }

  if ((status = sdio_.EnableFnIntr()) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to enable function interrupt\n", __FILE__);
    return status;
  }

  if ((status = sdio_.UpdateBlockSize(kBlockSize, false)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to update block size\n", __FILE__);
    return status;
  }

  if ((status = CardEnableInterrupt()) != ZX_OK) {
    return status;
  }

  if ((status = CardSetOwn(true)) != ZX_OK) {
    return status;
  }

  if ((status = CardWrite32(kChierAddress, kIsrAll)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to write card register\n", __FILE__);
    return status;
  }

  if ((status = CardWrite32(kChlpcrAddress, kChlpcrFwIntSet)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to write card register\n", __FILE__);
    return status;
  }

  int fw_status = CardGetFirmwareStatus();
  if (fw_status < 0) {
    return fw_status;
  } else if (fw_status == kFirmwareNeedDownload) {
    if ((status = CardDownloadFirmware(fw_vmo, fw_size)) != ZX_OK) {
      return status;
    } else if (CardGetFirmwareStatus() != kFirmwareReady) {
      zxlogf(ERROR, "%s: Firmware not ready after download\n", __FILE__);
      return ZX_ERR_INTERNAL;
    }
  }

  if ((status = CardSetPower(true)) != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t BtHciMediatek::DdkGetProtocol(uint32_t proto_id, void* out) {
  if (proto_id == ZX_PROTOCOL_BT_HCI) {
    bt_hci_protocol_t* proto = reinterpret_cast<bt_hci_protocol_t*>(out);
    proto->ops = &protocol_ops_;
    proto->ctx = this;
    return ZX_OK;
  }

  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t BtHciMediatek::BtHciOpenCommandChannel(zx_handle_t in) {
  return OpenChannel(&cmd_channel_, in, kCommandChannelKey);
}

zx_status_t BtHciMediatek::BtHciOpenAclDataChannel(zx_handle_t in) {
  return OpenChannel(&acl_channel_, in, kAclChannelKey);
}

zx_status_t BtHciMediatek::BtHciOpenSnoopChannel(zx_handle_t in) {
  return OpenChannel(&snoop_channel_, in, kSnoopChannelKey);
}

zx_status_t BtHciMediatek::OpenChannel(zx::channel* in_channel, zx_handle_t in, PacketKey key) {
  fbl::AutoLock lock(&thread_mutex_);

  if (in_channel->is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }

  *in_channel = zx::channel(in);

  zx_signals_t wait_signals = ZX_CHANNEL_PEER_CLOSED | ZX_SIGNAL_HANDLE_CLOSED;
  if (key != kSnoopChannelKey) {
    wait_signals |= ZX_CHANNEL_READABLE;
  }

  zx_status_t status = ZX_OK;
  if ((status = in_channel->wait_async(port_, key, wait_signals, ZX_WAIT_ASYNC_ONCE)) != ZX_OK) {
    zxlogf(ERROR, "%s: Channel object_wait_async failed\n", __FILE__);
    return status;
  }

  if (!thread_running_) {
    thread_running_ = true;

    thrd_create_with_name(
        &thread_, [](void* ctx) { return reinterpret_cast<BtHciMediatek*>(ctx)->Thread(); }, this,
        "bt-hci-mediatek-thread");
  }

  return status;
}

zx_status_t BtHciMediatek::CardEnableInterrupt() {
  zx_status_t status =
      sdio_.DoRwByte(true, kChlpcrAddress, kChlpcrFwIntSet | kChlpcrFwIntClear, nullptr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to enable card interrupt\n", __FILE__);
    return status;
  }

  uint32_t csdiocsr;
  if ((status = CardRead32(kCsdioCsrAddress, &csdiocsr)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to read CSDIOCSR\n", __FILE__);
    return status;
  }

  if ((status = CardWrite32(kCsdioCsrAddress, csdiocsr | kCsdioCsrClockFix)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to write CSDIOCSR\n", __FILE__);
    return status;
  }

  return ZX_OK;
}

zx_status_t BtHciMediatek::CardDisableInterrupt() {
  zx_status_t status = sdio_.DoRwByte(true, kChlpcrAddress, 0, nullptr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to disable card interrupt\n", __FILE__);
  }

  return status;
}

zx_status_t BtHciMediatek::CardRead32(uint32_t address, uint32_t* value) {
  sdio_rw_txn txn;
  txn.addr = address;
  txn.data_size = sizeof(*value);
  txn.incr = true;
  txn.write = false;
  txn.use_dma = false;
  txn.virt_buffer = value;
  txn.virt_size = sizeof(*value);
  txn.buf_offset = 0;

  zx_status_t status = sdio_.DoRwTxn(&txn);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to read card register\n", __FILE__);
  }

  *value = letoh32(*value);
  return status;
}

zx_status_t BtHciMediatek::CardWrite32(uint32_t address, uint32_t value) {
  value = htole32(value);

  sdio_rw_txn txn;
  txn.addr = address;
  txn.data_size = sizeof(value);
  txn.incr = true;
  txn.write = true;
  txn.use_dma = false;
  txn.virt_buffer = &value;
  txn.virt_size = sizeof(value);
  txn.buf_offset = 0;

  zx_status_t status = sdio_.DoRwTxn(&txn);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to write card register\n", __FILE__);
  }

  return status;
}

zx_status_t BtHciMediatek::CardRecvPacket(uint32_t* size) {
  uint32_t chisr;
  zx_status_t status = CardRead32(kChisrAddress, &chisr);
  for (int i = 0; i < 5 && status == ZX_OK; i++) {
    if ((chisr & kIsrRxDone) != 0 && (chisr & kIsrRxPacketSizeMask) != 0) {
      break;
    }

    status = CardRead32(kChisrAddress, &chisr);
    zx::nanosleep(zx::deadline_after(zx::msec(3)));
  }

  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to read card register\n", __FILE__);
    return status;
  } else if ((chisr & kIsrRxDone) == 0 || (chisr & kIsrRxPacketSizeMask) == 0) {
    // This could be expected so don't print an error message.
    return ZX_ERR_TIMED_OUT;
  }

  *size = letoh16(chisr >> kIsrRxPacketSizeShift);

  chisr &= ~kIsrRxPacketSizeMask & ~kIsrTxEmpty;
  if ((status = CardWrite32(kChisrAddress, chisr)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to write card register\n", __FILE__);
    return status;
  }

  return ZX_OK;
}

zx_status_t BtHciMediatek::CardReset() {
  constexpr uint8_t kResetPacket[] = {0x01, 0x07, 0x01, 0x00, 0x04};
  constexpr uint8_t kResetResponsePacket[] = {0x04, 0xe4, 0x05, 0x02, 0x07, 0x01, 0x00, 0x00};

  uint8_t packet[sizeof(kResetResponsePacket)];
  memcpy(packet, kResetPacket, sizeof(kResetPacket));
  size_t size = sizeof(kResetPacket);
  zx_status_t status = CardSendVendorPacket(kPacketTypeCmd, 0x6f, packet, &size, sizeof(packet));
  if (status != ZX_OK) {
    return status;
  } else if (size != sizeof(kResetResponsePacket)) {
    zxlogf(ERROR, "%s: Packet header doesn't match size\n", __FILE__);
    return ZX_ERR_IO;
  } else if (memcmp(packet, kResetResponsePacket, sizeof(kResetResponsePacket)) != 0) {
    zxlogf(ERROR, "%s: Unexpected response to reset command\n", __FILE__);
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t BtHciMediatek::CardSetOwn(bool driver) {
  const uint32_t mask = driver ? kChlpcrDriverOwn : kChlpcrFwOwn;

  zx_status_t status = CardWrite32(kChlpcrAddress, mask);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to set card own\n", __FILE__);
    return status;
  }

  const uint32_t checkvalue = driver ? kChlpcrFwOwn : 0;

  uint32_t value = 0;
  for (uint32_t i = 0; i < 1000 && (value & kChlpcrFwOwn) != checkvalue; i++) {
    zx::nanosleep(zx::deadline_after(zx::usec(1)));

    if ((status = CardRead32(kChlpcrAddress, &value)) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to read card own\n", __FILE__);
      return status;
    }
  }

  if ((value & kChlpcrFwOwn) != checkvalue) {
    zxlogf(ERROR, "%s: Timed out waiting for card own\n", __FILE__);
    return ZX_ERR_TIMED_OUT;
  }

  return ZX_OK;
}

zx_status_t BtHciMediatek::CardSetPower(bool on) {
  constexpr uint8_t kCardSetPowerPacket[] = {kPacketTypeCmd, 0x6f, 0xfc, 0x06, 0x01,
                                             0x06,           0x02, 0x00, 0x00, 0x01};
  constexpr uint8_t kCardSetPowerResponsePacket[] = {0x04, 0xe4, 0x05, 0x02,
                                                     0x06, 0x01, 0x00, 0x00};

  uint8_t packet[fbl::round_up<size_t, size_t>(sizeof(kCardSetPowerPacket) + 4, 4)];
  SetSizeField(packet, sizeof(kCardSetPowerPacket) + 4);
  memcpy(packet + 4, kCardSetPowerPacket, sizeof(kCardSetPowerPacket));

  sdio_rw_txn txn;
  txn.addr = kCtdrAddress;
  txn.data_size = sizeof(packet);
  txn.incr = false;
  txn.write = true;
  txn.use_dma = false;
  txn.virt_buffer = packet;
  txn.virt_size = sizeof(packet);
  txn.buf_offset = 0;
  zx_status_t status = sdio_.DoRwTxn(&txn);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: SDIO transaction failed\n", __FILE__);
    return status;
  }

  const int tries = on ? 60 : 3;

  uint32_t recv_size;
  status = CardRecvPacket(&recv_size);
  for (int i = 0; i < tries && status == ZX_ERR_TIMED_OUT; i++) {
    zx::nanosleep(zx::deadline_after(zx::msec(100)));
    status = CardRecvPacket(&recv_size);
  }

  if (status != ZX_OK) {
    return status;
  } else if (recv_size > sizeof(packet)) {
    zxlogf(ERROR, "%s: Unexpected response to set power command\n", __FILE__);
    return ZX_ERR_IO;
  }

  txn.addr = kCrdrAddress;
  txn.data_size = recv_size;
  txn.incr = false;
  txn.write = false;
  txn.use_dma = false;
  txn.virt_buffer = packet;
  txn.virt_size = recv_size;
  txn.buf_offset = 0;
  if ((status = sdio_.DoRwTxn(&txn)) != ZX_OK) {
    zxlogf(ERROR, "%s: SDIO transaction failed\n", __FILE__);
    return ZX_ERR_IO;
  }

  if (GetSizeField(packet) != recv_size) {
    zxlogf(ERROR, "%s: Packet header doesn't match size\n", __FILE__);
    return ZX_ERR_IO;
  }

  if (memcmp(packet + kSdioHeaderSize, kCardSetPowerResponsePacket,
             sizeof(kCardSetPowerResponsePacket)) != 0) {
    zxlogf(ERROR, "%s: Unexpected response to set power command\n", __FILE__);
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t BtHciMediatek::CardSendVendorPacket(uint8_t id, uint8_t ocf, uint8_t* packet,
                                                size_t* size, size_t buffer_size) {
  constexpr size_t kCmdPacketHeaderSize = 8;
  constexpr size_t kAclPacketHeaderSize = 9;

  if (id != kPacketTypeCmd && id != kPacketTypeAcl) {
    return ZX_ERR_INVALID_ARGS;
  }

  size_t total_size = *size;
  total_size += (id == kPacketTypeCmd) ? kCmdPacketHeaderSize : kAclPacketHeaderSize;

  size_t vmo_size = fbl::round_up<size_t, size_t>(std::max(total_size, buffer_size), kBlockSize);

  fzl::VmoMapper mapper;
  zx::vmo vmo;
  zx_status_t status =
      mapper.CreateAndMap(vmo_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to create and map VMO\n", __FILE__);
    return status;
  }

  uint8_t* packet_buf = reinterpret_cast<uint8_t*>(mapper.start());

  // SDIO header
  SetSizeField(packet_buf, total_size);
  packet_buf += 2;
  *packet_buf++ = 0x00;
  *packet_buf++ = 0x00;

  // HCI header with vendor opcode
  *packet_buf++ = id;
  *packet_buf++ = ocf;
  *packet_buf++ = 0xfc;

  if (id == kPacketTypeAcl) {
    SetSizeField(packet_buf, *size);
    memcpy(packet_buf + 2, packet, *size);
  } else {
    *packet_buf++ = static_cast<uint8_t>(*size);
    memcpy(packet_buf, packet, *size);
  }

  packet_buf = reinterpret_cast<uint8_t*>(mapper.start());

  sdio_rw_txn txn;
  txn.addr = kCtdrAddress;
  txn.data_size = static_cast<uint32_t>(fbl::round_up<size_t, size_t>(total_size, kBlockSize));
  txn.incr = false;
  txn.write = true;
  txn.use_dma = true;
  txn.dma_vmo = vmo.get();
  txn.buf_offset = 0;
  if ((status = sdio_.DoRwTxn(&txn)) != ZX_OK) {
    zxlogf(ERROR, "%s: SDIO transaction failed\n", __FILE__);
    return status;
  }

  uint32_t recv_size;
  if ((status = CardRecvPacket(&recv_size)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to read packet from card\n", __FILE__);
    return status;
  } else if (recv_size < kSdioHeaderSize) {
    zxlogf(ERROR, "%s: Short read from card\n", __FILE__);
    return ZX_ERR_IO;
  } else if (recv_size - kSdioHeaderSize > buffer_size) {
    zxlogf(ERROR, "%s: Received packet too big for buffer\n", __FILE__);
    return ZX_ERR_INVALID_ARGS;
  }

  txn.addr = kCrdrAddress;
  txn.data_size = recv_size;
  txn.incr = false;
  txn.write = false;
  txn.use_dma = true;
  txn.dma_vmo = vmo.get();
  txn.buf_offset = 0;
  if ((status = sdio_.DoRwTxn(&txn)) != ZX_OK) {
    zxlogf(ERROR, "%s: SDIO transaction failed\n", __FILE__);
    return status;
  } else if (GetSizeField(packet_buf) != recv_size) {
    zxlogf(ERROR, "%s: Packet size doesn't match register value\n", __FILE__);
    return ZX_ERR_IO;
  }

  *size = recv_size - kSdioHeaderSize;
  memcpy(packet, packet_buf + kSdioHeaderSize, *size);

  return ZX_OK;
}

int BtHciMediatek::CardGetFirmwareStatus() {
  constexpr uint8_t kFwStatusPacket[] = {0x01, 0x17, 0x01, 0x00, 0x01};
  constexpr uint8_t kFwStatusResponsePacket[] = {0x04, 0xe4, 0x05, 0x02, 0x17, 0x01, 0x00, 0x00};

  uint8_t packet[sizeof(kFwStatusResponsePacket)];
  memcpy(packet, kFwStatusPacket, sizeof(kFwStatusPacket));
  size_t size = sizeof(kFwStatusPacket);

  zx_status_t status = CardSendVendorPacket(kPacketTypeCmd, 0x6f, packet, &size, sizeof(packet));
  if (status != ZX_OK) {
    return status;
  } else if (size != sizeof(kFwStatusResponsePacket)) {
    zxlogf(ERROR, "%s: Packet header doesn't match size\n", __FILE__);
    return ZX_ERR_IO;
  } else if (memcmp(packet, kFwStatusResponsePacket, sizeof(kFwStatusResponsePacket) - 1) != 0) {
    zxlogf(ERROR, "%s: Unexpected response to firmware status command\n", __FILE__);
    return ZX_ERR_IO;
  }

  return packet[sizeof(kFwStatusResponsePacket) - 1];
}

zx_status_t BtHciMediatek::CardGetHwVersion(uint32_t* version) {
  constexpr uint8_t kHwVersionPacket[] = {
      0x01, 0x08, 0x08, 0x00,                         // Vendor data
      0x02, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x80  // Register address
  };

  constexpr uint8_t kHwVersionResponsePacket[] = {
      0x04, 0xe4, 0x10,        // Vendor header
      0x02, 0x08, 0x0c, 0x00,  // Vendor data
      0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
      0x00, 0x80, 0x00, 0x00, 0x00, 0x00,  // Register data goes here
      0x00                                 // Padding
  };

  constexpr size_t kRegValueOffset = sizeof(kHwVersionResponsePacket) - sizeof(*version) - 1;

  uint8_t packet[sizeof(kHwVersionResponsePacket)];
  memcpy(packet, kHwVersionPacket, sizeof(kHwVersionPacket));
  size_t size = sizeof(kHwVersionPacket);

  zx_status_t status = CardSendVendorPacket(kPacketTypeCmd, 0x6f, packet, &size, sizeof(packet));
  if (status != ZX_OK) {
    return status;
  } else if (size != sizeof(kHwVersionResponsePacket)) {
    zxlogf(ERROR, "%s: Packet header doesn't match size\n", __FILE__);
    return ZX_ERR_IO;
  } else if (memcmp(packet, kHwVersionResponsePacket, kRegValueOffset)) {
    zxlogf(ERROR, "%s: Unexpected response to hardware version command\n", __FILE__);
    return ZX_ERR_IO;
  }

  memcpy(version, packet + kRegValueOffset, sizeof(*version));
  *version = letoh32(*version);

  return ZX_OK;
}

zx_status_t BtHciMediatek::CardDownloadFirmware(const zx::vmo& fw_vmo, size_t fw_size) {
  if (fw_size < kFwHeaderSize) {
    zxlogf(ERROR, "%s: Invalid firmware size\n", __FILE__);
    return ZX_ERR_IO;
  }

  uint32_t hw_version = 0;
  zx_status_t status = CardGetHwVersion(&hw_version);
  if (status != ZX_OK) {
    return status;
  }

  if (hw_version == 0x8a00) {
    zxlogf(ERROR, "%s: No firmware for card version %04x\n", __FILE__, hw_version);
    return ZX_ERR_INTERNAL;
  }

  fzl::VmoMapper fw_mapper;
  if ((status = fw_mapper.Map(fw_vmo, 0, 0, ZX_VM_PERM_READ)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to map firmware VMO\n", __FILE__);
    return status;
  }

  fzl::VmoMapper fw_part_mapper;
  zx::vmo fw_part_vmo;
  status = fw_part_mapper.CreateAndMap(kFwPartHeaderSize + fw_part_max_size_,
                                       ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &fw_part_vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to create and map VMO\n", __FILE__);
    return status;
  }

  const uint8_t* fw_data = reinterpret_cast<const uint8_t*>(fw_mapper.start()) + kFwHeaderSize;
  fw_size -= kFwHeaderSize;

  uint8_t* fw_part_buffer = reinterpret_cast<uint8_t*>(fw_part_mapper.start());

  size_t fw_left = fw_size;
  for (size_t send_size; fw_left > 0 && status == ZX_OK; fw_left -= send_size) {
    send_size = std::min(fw_part_max_size_, fw_left);

    FirmwarePartMode mode = kFirmwarePartContinue;
    if (fw_left == fw_size) {
      mode = kFirmwarePartFirst;
    } else if (fw_left == send_size) {
      mode = kFirmwarePartLast;
    }

    status = CardSendFirmwarePart(fw_part_vmo.get(), fw_part_buffer, fw_data, send_size, mode);
    fw_data += send_size;
  }

  if (fw_left != 0) {
    return status;
  }

  uint32_t chcr;
  if ((status = CardRead32(kChcrAddress, &chcr)) != ZX_OK) {
    return status;
  }

  if ((status = CardWrite32(kChcrAddress, chcr | kChcrWriteClear)) != ZX_OK) {
    return status;
  }

  return CardReset();
}

zx_status_t BtHciMediatek::CardSendFirmwarePart(zx_handle_t vmo, uint8_t* buffer,
                                                const uint8_t* fw_data, size_t size,
                                                FirmwarePartMode mode) {
  constexpr uint8_t kFirmwarePartResponse[] = {0x0c, 0x00, 0x00, 0x00, 0x04, 0xe4,
                                               0x05, 0x02, 0x01, 0x01, 0x00, 0x00};

  size_t total_size = fbl::round_up<size_t, size_t>(kFwPartHeaderSize + size, kBlockSize);
  uint8_t* buffer_ptr = buffer;

  // SDIO header
  SetSizeField(buffer_ptr, kFwPartHeaderSize + size);
  buffer_ptr += 2;
  *buffer_ptr++ = 0x00;
  *buffer_ptr++ = 0x00;

  // Vendor header
  *buffer_ptr++ = kPacketTypeAcl;
  *buffer_ptr++ = 0x6f;
  *buffer_ptr++ = 0xfc;
  SetSizeField(buffer_ptr, size + 5);
  buffer_ptr += 2;

  // STP header
  *buffer_ptr++ = 0x01;
  *buffer_ptr++ = 0x01;
  SetSizeField(buffer_ptr, size + 1);
  buffer_ptr += 2;

  *buffer_ptr++ = mode;

  memcpy(buffer_ptr, fw_data, size);

  sdio_rw_txn txn;
  txn.addr = kCtdrAddress;
  txn.data_size = static_cast<uint32_t>(total_size);
  txn.incr = false;
  txn.write = true;
  txn.use_dma = true;
  txn.dma_vmo = vmo;
  txn.buf_offset = 0;
  zx_status_t status = sdio_.DoRwTxn(&txn);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: SDIO transaction failed\n", __FILE__);
    return status;
  }

  uint32_t recv_size;
  if ((status = CardRecvPacket(&recv_size)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to read packet from card\n", __FILE__);
    return status;
  } else if (recv_size != sizeof(kFirmwarePartResponse)) {
    zxlogf(ERROR, "%s: Packet header doesn't match size\n", __FILE__);
    return ZX_ERR_IO;
  }

  txn.addr = kCrdrAddress;
  txn.data_size = recv_size;
  txn.incr = false;
  txn.write = false;
  txn.use_dma = true;
  txn.dma_vmo = vmo;
  txn.buf_offset = 0;
  if ((status = sdio_.DoRwTxn(&txn)) != ZX_OK) {
    zxlogf(ERROR, "%s: SDIO transaction failed\n", __FILE__);
    return status;
  } else if (memcmp(buffer, kFirmwarePartResponse, sizeof(kFirmwarePartResponse)) != 0) {
    zxlogf(ERROR, "%s: Unexpected response to firmware packet\n", __FILE__);
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t BtHciMediatek::HandleCardInterrupt() {
  fzl::VmoMapper mapper;
  zx::vmo vmo;
  zx_status_t status =
      mapper.CreateAndMap(kMaxPacketSize, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to create and map VMO\n", __FILE__);
    return status;
  }

  uint8_t* header_buf = reinterpret_cast<uint8_t*>(mapper.start());
  uint8_t* packet_buf = header_buf + kHciPacketHeaderSize;
  uint8_t* snoop_buf = header_buf + kHciPacketHeaderSize - 1;

  uint32_t chisr;
  if ((status = CardRead32(kChisrAddress, &chisr)) != ZX_OK) {
    return status;
  }

  if ((chisr & kIsrTxEmpty) != 0) {
    if ((status = CardWrite32(kChisrAddress, kIsrTxEmpty | kIsrTxCompleteCount)) != ZX_OK) {
      return status;
    }
  }

  uint32_t recv_size = 0;
  if ((chisr & kIsrRxDone) != 0) {
    if ((status = CardRecvPacket(&recv_size)) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to read packet from card\n", __FILE__);
      return status;
    }
  }

  if (recv_size == 0) {
    return ZX_OK;
  } else if (recv_size > kMaxPacketSize) {
    zxlogf(ERROR, "%s: Received packet too big for buffer\n", __FILE__);
    return ZX_ERR_IO;
  }

  sdio_rw_txn txn;
  txn.addr = kCrdrAddress;
  txn.data_size = fbl::round_up<uint32_t, uint32_t>(recv_size, kBlockSize);
  txn.incr = false;
  txn.write = false;
  txn.use_dma = true;
  txn.dma_vmo = vmo.get();
  txn.buf_offset = 0;
  if ((status = sdio_.DoRwTxn(&txn)) != ZX_OK) {
    zxlogf(ERROR, "%s: SDIO transaction failed\n", __FILE__);
    return status;
  } else if (GetSizeField(header_buf) != recv_size) {
    zxlogf(ERROR, "%s: Packet header doesn't match size\n", __FILE__);
    return ZX_ERR_IO;
  }

  recv_size -= kHciPacketHeaderSize;

  const zx::channel* channel;
  uint32_t snoop_type;

  switch (header_buf[kHciPacketHeaderSize - 1]) {
    case kPacketTypeAcl:
      channel = &acl_channel_;
      snoop_type = BT_HCI_SNOOP_TYPE_ACL;

      // The MT7668 rounds packets up to a multiple of four bytes, so decode the actual packet
      // size and only send that much data over the channel.
      if (recv_size < kAclPacketHeaderSize) {
        zxlogf(ERROR, "%s: ACL packet from card is too short\n", __FILE__);
        return ZX_ERR_IO;
      }
      if (GetSizeField(packet_buf + kAclPacketSizeOffset) > recv_size - kAclPacketHeaderSize) {
        zxlogf(ERROR, "%s: ACL packet from card is too big\n", __FILE__);
        return ZX_ERR_IO;
      }

      recv_size = GetSizeField(packet_buf + kAclPacketSizeOffset) + kAclPacketHeaderSize;
      break;
    case kPacketTypeEvent:
      channel = &cmd_channel_;
      snoop_type = BT_HCI_SNOOP_TYPE_EVT;

      if (recv_size < kEventPacketHeaderSize) {
        zxlogf(ERROR, "%s: Event packet from card is too short\n", __FILE__);
        return ZX_ERR_IO;
      }
      if (packet_buf[kEventPacketSizeOffset] > recv_size - kEventPacketHeaderSize) {
        zxlogf(ERROR, "%s: Event packet from card is too big\n", __FILE__);
        return ZX_ERR_IO;
      }

      recv_size = packet_buf[kEventPacketSizeOffset] + kEventPacketHeaderSize;
      break;
    case kPacketTypeSco:
    default:
      zxlogf(ERROR, "%s: Unknown packet type %u received from card\n", __FILE__,
             header_buf[kHciPacketHeaderSize - 1]);
      return ZX_OK;
  }

  uint32_t snoop_size = recv_size + 1;

  if (channel->is_valid()) {
    if ((status = channel->write(0, packet_buf, recv_size, nullptr, 0)) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to write to channel\n", __FILE__);
      return status;
    }
  }

  if (snoop_channel_.is_valid()) {
    snoop_buf[0] = bt_hci_snoop_flags(snoop_type, true);
    if ((status = snoop_channel_.write(0, snoop_buf, snoop_size, nullptr, 0)) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to write to snoop channel\n", __FILE__);
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t BtHciMediatek::HostToCardPacket(const zx::channel& channel, uint8_t packet_type,
                                            uint32_t snoop_type) {
  fzl::VmoMapper mapper;
  zx::vmo vmo;
  zx_status_t status =
      mapper.CreateAndMap(kMaxPacketSize, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to create and map VMO\n", __FILE__);
    return status;
  }

  uint8_t* header_buf = reinterpret_cast<uint8_t*>(mapper.start());

  uint8_t* packet_buf = header_buf + kHciPacketHeaderSize;
  const uint32_t packet_buf_size = static_cast<uint32_t>(mapper.size() - kHciPacketHeaderSize);

  uint8_t* snoop_buf = header_buf + kHciPacketHeaderSize - 1;

  uint32_t actual;
  status = channel.read(0, packet_buf, nullptr, packet_buf_size, 0, &actual, nullptr);
  while (status == ZX_OK) {
    uint32_t snoop_size = actual + 1;
    actual += kHciPacketHeaderSize;

    if (actual > kMaxPacketSize) {
      zxlogf(ERROR, "%s: Host packet too big for card\n", __FILE__);
      return ZX_ERR_NOT_SUPPORTED;
    }

    SetSizeField(header_buf, actual);
    header_buf[2] = 0;
    header_buf[3] = 0;
    header_buf[4] = packet_type;

    sdio_rw_txn txn;
    txn.addr = kCtdrAddress;
    txn.data_size = fbl::round_up<uint32_t, uint32_t>(actual, kBlockSize);
    txn.incr = false;
    txn.write = true;
    txn.use_dma = true;
    txn.dma_vmo = vmo.get();
    txn.buf_offset = 0;
    if ((status = sdio_.DoRwTxn(&txn)) != ZX_OK) {
      zxlogf(ERROR, "%s: SDIO transaction failed\n", __FILE__);
      return status;
    }

    if (snoop_channel_.is_valid()) {
      snoop_buf[0] = bt_hci_snoop_flags(snoop_type, false);
      if ((status = snoop_channel_.write(0, snoop_buf, snoop_size, nullptr, 0)) != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to write to snoop channel\n", __FILE__);
        return status;
      }
    }

    status = channel.read(0, packet_buf, nullptr, packet_buf_size, 0, &actual, nullptr);
  }

  if (status != ZX_ERR_SHOULD_WAIT) {
    zxlogf(ERROR, "%s: Failed to read from command channel\n", __FILE__);
    return status;
  }

  return ZX_OK;
}

int BtHciMediatek::Thread() {
  for (;;) {
    zx_port_packet_t packet;
    zx_status_t status = port_.wait(zx::deadline_after(zx::duration::infinite()), &packet);

    fbl::AutoLock lock(&thread_mutex_);

    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Port wait failed\n", __FILE__);
      thread_running_ = false;
      return thrd_error;
    }

    if (packet.type == ZX_PKT_TYPE_SIGNAL_ONE && packet.key == kCommandChannelKey) {
      bool rearm = true;
      if (packet.signal.observed & ZX_CHANNEL_READABLE) {
        status = HostToCardPacket(cmd_channel_, kPacketTypeCmd, BT_HCI_SNOOP_TYPE_CMD);
        if (status != ZX_OK) {
          thread_running_ = false;
          return thrd_error;
        }
      }

      if (packet.signal.observed & (ZX_CHANNEL_PEER_CLOSED | ZX_SIGNAL_HANDLE_CLOSED)) {
        port_.cancel(cmd_channel_, packet.key);
        cmd_channel_.reset();
        rearm = false;
      }
      if (rearm) {
        zx_signals_t wait_signals =
            ZX_CHANNEL_PEER_CLOSED | ZX_SIGNAL_HANDLE_CLOSED | ZX_CHANNEL_READABLE;
        status = cmd_channel_.wait_async(port_, packet.key, wait_signals, ZX_WAIT_ASYNC_ONCE);
        if (status != ZX_OK) {
          zxlogf(ERROR, "%s: Channel object_wait_async failed: %d\n", __FILE__, status);
        }
      }
    } else if (packet.type == ZX_PKT_TYPE_SIGNAL_ONE && packet.key == kAclChannelKey) {
      bool rearm = true;
      if (packet.signal.observed & ZX_CHANNEL_READABLE) {
        status = HostToCardPacket(acl_channel_, kPacketTypeAcl, BT_HCI_SNOOP_TYPE_ACL);
        if (status != ZX_OK) {
          thread_running_ = false;
          return thrd_error;
        }
      }

      if (packet.signal.observed & (ZX_CHANNEL_PEER_CLOSED | ZX_SIGNAL_HANDLE_CLOSED)) {
        port_.cancel(acl_channel_, packet.key);
        acl_channel_.reset();
        rearm = false;
      }
      if (rearm) {
        zx_signals_t wait_signals =
            ZX_CHANNEL_PEER_CLOSED | ZX_SIGNAL_HANDLE_CLOSED | ZX_CHANNEL_READABLE;
        status = acl_channel_.wait_async(port_, packet.key, wait_signals, ZX_WAIT_ASYNC_ONCE);
        if (status != ZX_OK) {
          zxlogf(ERROR, "%s: Channel object_wait_async failed: %d\n", __FILE__, status);
        }
      }
    } else if (packet.type == ZX_PKT_TYPE_SIGNAL_ONE && packet.key == kSnoopChannelKey) {
      if (packet.signal.observed & (ZX_CHANNEL_PEER_CLOSED | ZX_SIGNAL_HANDLE_CLOSED)) {
        port_.cancel(snoop_channel_, packet.key);
        snoop_channel_.reset();
      } else {
        zx_signals_t wait_signals = ZX_CHANNEL_PEER_CLOSED | ZX_SIGNAL_HANDLE_CLOSED;
        status = acl_channel_.wait_async(port_, packet.key, wait_signals, ZX_WAIT_ASYNC_ONCE);
        if (status != ZX_OK) {
          zxlogf(ERROR, "%s: Channel object_wait_async failed: %d\n", __FILE__, status);
        }
      }
    } else if (packet.type == ZX_PKT_TYPE_INTERRUPT && packet.key == kSdioInterruptKey) {
      CardWrite32(kChlpcrAddress, kChlpcrFwIntClear);
      status = HandleCardInterrupt();
      CardWrite32(kChlpcrAddress, kChlpcrFwIntSet);

      sdio_int_.ack();

      if (status != ZX_OK) {
        thread_running_ = false;
        return thrd_error;
      }
    } else if (packet.type == ZX_PKT_TYPE_USER && packet.key == kStopThreadKey) {
      thread_running_ = false;
      break;
    } else {
      zxlogf(WARN, "%s: Unknown packet type %u or key %lu\n", __FILE__, packet.type, packet.key);
    }

    if (!cmd_channel_.is_valid() && !acl_channel_.is_valid() && !snoop_channel_.is_valid()) {
      thread_running_ = false;
      break;
    }
  }

  return thrd_success;
}

}  // namespace bluetooth

static constexpr zx_driver_ops_t bt_hci_mediatek_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = bluetooth::BtHciMediatek::Create;
  return ops;
}();

ZIRCON_DRIVER_BEGIN(bt_hci_mediatek, bt_hci_mediatek_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_SDIO_VID, 0x037a), BI_ABORT_IF(NE, BIND_SDIO_PID, 0x7668),
    BI_MATCH_IF(EQ, BIND_SDIO_FUNCTION, SDIO_FN_2), ZIRCON_DRIVER_END(bt_hci_mediatek)
