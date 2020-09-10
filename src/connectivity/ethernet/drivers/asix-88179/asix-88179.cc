// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asix-88179.h"

#include <inttypes.h>
#include <lib/cksum.h>
#include <lib/zircon-internal/align.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/listnode.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <fbl/auto_call.h>

#include "asix-88179-regs.h"

static constexpr uint8_t kMediaMode[6][2] = {
    {0x30, 0x01},  // 10 Mbps, half-duplex
    {0x32, 0x01},  // 10 Mbps, full-duplex
    {0x30, 0x03},  // 100 Mbps, half-duplex
    {0x32, 0x03},  // 100 Mbps, full-duplex
    {0, 0},        // unused
    {0x33, 0x01},  // 1000Mbps, full-duplex
};

// The array indices here correspond to the bit positions in the AX88179 MAC
// PLSR register.
static constexpr uint8_t kBulkInConfig[5][5][5] = {
    {
        {0},
        {0},
        {0},
        {0},
        {0},
    },
    {
        // Full Speed
        {0},
        {0x07, 0xcc, 0x4c, 0x18, 0x08},  // 10 Mbps
        {0x07, 0xcc, 0x4c, 0x18, 0x08},  // 100 Mbps
        {0},
        {0x07, 0xcc, 0x4c, 0x18, 0x08},  // 1000 Mbps
    },
    {
        // High Speed
        {0},
        {0x07, 0xcc, 0x4c, 0x18, 0x08},  // 10 Mbps
        {0x07, 0xae, 0x07, 0x18, 0xff},  // 100 Mbps
        {0},
        {0x07, 0x20, 0x03, 0x16, 0xff},  // 1000 Mbps
    },
    {
        {0},
        {0},
        {0},
        {0},
        {0},
    },
    {
        // Super Speed
        {0},
        {0x07, 0xcc, 0x4c, 0x18, 0x08},  // 10 Mbps
        {0x07, 0xae, 0x07, 0x18, 0xff},  // 100 Mbps
        {0},
        {0x07, 0x4f, 0x00, 0x12, 0xff},  // 1000 Mbps
    },
};

static const int32_t kReadRequestCount = 8;
static const int32_t kWriteRequestCount = 8;
static const int32_t kUsbBufferSize = 24576;
static const int32_t kInterruptRequestSize = 8;
static const int32_t kMtu = 1500;
static const int32_t kMaxEthernetHeaderSize = 26;
static const int32_t kMaxMulticastFilterAddress = 32;
static const int32_t kMulticastFilterNBytes = 8;

/*
 * The constants are determined based on Pluggable gigabit Ethernet adapter(Model: USBC-E1000),
 * connected on pixelbook. At times, the device returns NRDY token when it is unable to match the
 * pace of the client driver but does not recover by sending a ERDY token within the controller's
 * time limit. kEthernetInitialTransmitDelay helps us avoids getting into this situation by adding
 * a delay at the beginning.
 */
static constexpr zx::duration kEthernetMaxTransmitDelay = zx::usec(100);
static constexpr zx::duration kEthernetMaxReceiveDelay = zx::usec(100);
static constexpr zx::duration kEthernetTransmitDelay = zx::usec(10);
static constexpr zx::duration kEthernetReceiveDelay = zx::usec(10);
static constexpr zx::duration kEthernetInitialTransmitDelay = zx::usec(15);
static constexpr zx::duration kEthernetInitialReceiveDelay = zx::usec(0);

namespace eth {

template <typename T>
zx_status_t Asix88179Ethernet::ReadMac(uint8_t register_address, T* data) {
  size_t out_length = 0;
  uint8_t register_length = sizeof(T);

  zx_status_t status = usb_.ControlIn(USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                                      AX88179_REQ_MAC, register_address, register_length,
                                      ZX_TIME_INFINITE, data, register_length, &out_length);

  return status;
}

template <typename T>
zx_status_t Asix88179Ethernet::WriteMac(uint8_t register_address, const T& data) {
  uint8_t register_length = sizeof(T);

  return usb_.ControlOut(USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX88179_REQ_MAC,
                         register_address, register_length, ZX_TIME_INFINITE, &data,
                         register_length);
}

zx_status_t Asix88179Ethernet::ReadPhy(uint8_t register_address, uint16_t* data) {
  size_t out_length;
  zx_status_t status = usb_.ControlIn(USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                                      AX88179_REQ_PHY, AX88179_PHY_ID, register_address,
                                      ZX_TIME_INFINITE, data, sizeof(*data), &out_length);
  if (out_length == sizeof(*data)) {
    zxlogf(TRACE, "ax88179: read phy %#x: %#x", register_address, *data);
  }
  return status;
}

zx_status_t Asix88179Ethernet::WritePhy(uint8_t register_address, uint16_t data) {
  zxlogf(TRACE, "ax88179: write phy %#x: %#x", register_address, data);
  return usb_.ControlOut(USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, AX88179_REQ_PHY,
                         AX88179_PHY_ID, register_address, ZX_TIME_INFINITE, &data, sizeof(data));
}

zx_status_t Asix88179Ethernet::ConfigureBulkIn(uint8_t plsr) {
  uint8_t usb_mode = plsr & AX88179_PLSR_USB_MASK;
  if (usb_mode & (usb_mode - 1)) {
    zxlogf(ERROR, "ax88179: invalid usb mode: %#x", usb_mode);
    return ZX_ERR_INVALID_ARGS;
  }

  uint8_t speed = plsr & AX88179_PLSR_EPHY_MASK;
  if (speed & (speed - 1)) {
    zxlogf(ERROR, "ax88179: invalid eth speed: %#x", speed);
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = WriteMac(AX88179_MAC_RQCR, kBulkInConfig[usb_mode][speed >> 4]);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: WriteMac to %#x failed: %d", AX88179_MAC_RQCR, status);
  }
  return status;
}

zx_status_t Asix88179Ethernet::ConfigureMediumMode() {
  uint16_t data = 0;
  zx_status_t status = ReadPhy(AX88179_PHY_PHYSR, &data);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: ReadPhy to %#x failed: %d", AX88179_PHY_PHYSR, status);
    return status;
  }

  unsigned int mode = (data & (AX88179_PHYSR_SPEED | AX88179_PHYSR_DUPLEX)) >> 13;
  zxlogf(DEBUG, "ax88179: medium mode: %#x", mode);
  if (mode == 4 || mode > 5) {
    zxlogf(ERROR, "ax88179: mode invalid (mode=%u)", mode);
    return ZX_ERR_NOT_SUPPORTED;
  }
  status = WriteMac(AX88179_MAC_MSR, kMediaMode[mode]);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: WriteMac to %#x failed: %d", AX88179_MAC_MSR, status);
    return status;
  }

  uint8_t PLSR_value = 0;
  status = ReadMac(AX88179_MAC_PLSR, &PLSR_value);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: ReadMac to %#x failed: %d", AX88179_MAC_PLSR, status);
    return status;
  }
  status = ConfigureBulkIn(PLSR_value);

  return status;
}

zx_status_t Asix88179Ethernet::Receive(usb::Request<>& request) {
  auto& response = request.request()->response;

  struct ReceiveHeader {
    uint16_t number_packets;
    uint16_t packet_header_offset;
  };

  if (response.actual < 4) {
    zxlogf(ERROR, "ax88179: Receive short packet");
    return ZX_ERR_INTERNAL;
  }

  uint8_t* read_data;
  zx_status_t status = request.Mmap(reinterpret_cast<void**>(&read_data));
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: mmap failed: %d", status);
    return status;
  }

  ptrdiff_t header_offset = response.actual - sizeof(ReceiveHeader);
  ReceiveHeader* header = reinterpret_cast<ReceiveHeader*>(read_data + header_offset);
  if (header->number_packets < 1 || header->packet_header_offset >= header_offset) {
    zxlogf(ERROR, "ax88179: %s bad packet", __func__);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  size_t offset = 0;
  size_t packet = 0;

  while (packet < header->number_packets) {
    ptrdiff_t packet_index = packet++ * sizeof(uint32_t);
    uint32_t* packet_header =
        reinterpret_cast<uint32_t*>(read_data + header->packet_header_offset + packet_index);
    if ((uintptr_t)packet_header >= (uintptr_t)header) {
      zxlogf(ERROR, "ax88179: %s packet header out of bounds, packet header=%p rx header=%p",
             __func__, packet_header, header);
      return ZX_ERR_IO_DATA_INTEGRITY;
    }
    uint16_t packet_length = le16toh((*packet_header & AX88179_RX_PKTLEN) >> 16);
    if (packet_length < 2) {
      zxlogf(ERROR, "ax88179: %s short packet (len=%u)", __func__, packet_length);
      return ZX_ERR_IO_DATA_INTEGRITY;
    }
    if (offset + packet_length > header->packet_header_offset) {
      zxlogf(ERROR, "ax88179: %s invalid packet length %u > %lu bytes remaining", __func__,
             packet_length, header->packet_header_offset - offset);
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    bool drop = false;
    if (*packet_header & AX88179_RX_DROPPKT) {
      zxlogf(TRACE, "ax88179: %s DropPkt", __func__);
      drop = true;
    }
    if (*packet_header & AX88179_RX_MIIER) {
      zxlogf(TRACE, "ax88179: %s MII-Er", __func__);
      drop = true;
    }
    if (*packet_header & AX88179_RX_CRCER) {
      zxlogf(TRACE, "ax88179: %s CRC-Er", __func__);
      drop = true;
    }
    if (!(*packet_header & AX88179_RX_OK)) {
      zxlogf(TRACE, "ax88179: %s !GoodPkt", __func__);
      drop = true;
    }
    if (!drop) {
      ifc_.Recv(read_data + offset + 2, packet_length - 2, 0);
    }

    // Advance past this packet in the completed read
    offset += packet_length;
    offset = ZX_ALIGN(offset, 8);
  }

  return ZX_OK;
}

void Asix88179Ethernet::ReadComplete(usb_request_t* usb_request) {
  if (usb_request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    return;
  }

  usb::Request<> request(usb_request, parent_req_size_);

  fbl::AutoLock lock(&lock_);

  if (usb_request->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(DEBUG, "ax88179: ReadComplete usb_reset_endpoint");
    usb_.ResetEndpoint(bulk_in_address_);
  } else if (usb_request->response.status == ZX_ERR_IO_INVALID) {
    zxlogf(DEBUG,
           "ax88179: ReadComplete Slowing down the requests by %ld usec"
           " and resetting the recv endpoint\n",
           kEthernetReceiveDelay.to_usecs());
    if (rx_endpoint_delay_ < kEthernetMaxReceiveDelay) {
      rx_endpoint_delay_ += kEthernetReceiveDelay;
    }
    usb_.ResetEndpoint(bulk_in_address_);
  } else if (usb_request->response.status == ZX_OK) {
    if (ifc_.is_valid()) {
      Receive(request);
    }
  }

  if (online_) {
    zx::nanosleep(zx::deadline_after(rx_endpoint_delay_));
    usb_.RequestQueue(request.take(), &read_request_complete_);
  } else {
    free_read_pool_.Add(std::move(request));
  }
}

void Asix88179Ethernet::WriteComplete(usb_request_t* usb_request) {
  fbl::AutoLock tx_lock(&tx_lock_);
  usb::Request<> request(usb_request, parent_req_size_);

  if (usb_request->response.status == ZX_ERR_IO_NOT_PRESENT) {
    free_write_pool_.Add(std::move(request));
    zxlogf(INFO, "ax88179: remote closed");
    return;
  }

  if (usb_request->response.status == ZX_ERR_IO_REFUSED) {
    zxlogf(DEBUG, "ax88179: WriteComplete usb_reset_endpoint");
    usb_.ResetEndpoint(bulk_out_address_);
  } else if (usb_request->response.status == ZX_ERR_IO_INVALID) {
    zxlogf(DEBUG,
           "ax88179: WriteComplete Slowing down the requests by %ld usec"
           " and resetting the transmit endpoint\n",
           kEthernetTransmitDelay.to_usecs());
    if (tx_endpoint_delay_ < kEthernetMaxTransmitDelay) {
      tx_endpoint_delay_ += kEthernetTransmitDelay;
    }
    usb_.ResetEndpoint(bulk_out_address_);
  }

  // If there are pending netbuf packets, add as many as possible to the new request.
  request.request()->header.length = 0;
  for (int count = 0; !pending_netbuf_queue_.is_empty(); count++) {
    auto netbuf = pending_netbuf_queue_.pop().value();

    if ((RequestAppend(request, netbuf) == ZX_ERR_BUFFER_TOO_SMALL)) {
      if (count != 0) {
        pending_netbuf_queue_.push_next(std::move(netbuf));
        break;
      } else {
        size_t length = netbuf.operation()->data_size;
        // netbuf is too large for a request buffer.
        zxlogf(ERROR, "ax88179: failed to append netbuf to empty request %zu", length);
        netbuf.Complete(ZX_ERR_INTERNAL);
        break;
      }
    }

    netbuf.Complete(ZX_OK);
  }
  if (request.request()->header.length > 0) {
    pending_usb_transmit_queue_.push(std::move(request));
  } else {
    free_write_pool_.Add(std::move(request));
  }

  std::optional<usb::Request<>> next = pending_usb_transmit_queue_.pop();
  if (next) {
    zx::nanosleep(zx::deadline_after(tx_endpoint_delay_));
    usb_.RequestQueue(next->take(), &write_request_complete_);
  }
}

void Asix88179Ethernet::InterruptComplete(usb_request_t* usb_request) {
  fbl::AutoLock lock(&lock_);

  if (usb_request->response.status == ZX_OK &&
      usb_request->response.actual == kInterruptRequestSize) {
    uint8_t status[kInterruptRequestSize];
    usb::Request<> request(usb_request, parent_req_size_, false);

    if (request.CopyFrom(status, sizeof(status), 0) == kInterruptRequestSize) {
      bool online = (status[2] & 1) != 0;
      bool was_online = online_;
      online_ = online;
      if (online && !was_online) {
        ConfigureMediumMode();

        std::optional<usb::Request<>> pending_request;

        size_t request_length = usb::Request<>::RequestSize(parent_req_size_);
        while ((pending_request = free_read_pool_.Get(request_length))) {
          usb_.RequestQueue(pending_request->take(), &read_request_complete_);
        }

        zxlogf(DEBUG, "ax88179: now online");
        if (ifc_.is_valid()) {
          ifc_.Status(ETHERNET_STATUS_ONLINE);
        }
      } else if (!online && was_online) {
        zxlogf(DEBUG, "ax88179: now offline");
        if (ifc_.is_valid()) {
          ifc_.Status(0);
        }
      }
    }
  }

  sync_completion_signal(&interrupt_completion_);
}

zx_status_t Asix88179Ethernet::RequestAppend(usb::Request<>& request,
                                             const eth::BorrowedOperation<>& netbuf) {
  zx_off_t offset = ZX_ALIGN(request.request()->header.length, 4);

  struct {
    uint16_t transmit_length;
    uint16_t unused[3];
  } header = {
      .transmit_length = htole16(netbuf.operation()->data_size),
      .unused = {},
  };

  if ((offset + sizeof(header) + netbuf.operation()->data_size) > kUsbBufferSize) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  request.CopyTo(&header, sizeof(header), offset);
  request.CopyTo(netbuf.operation()->data_buffer, netbuf.operation()->data_size,
                 offset + sizeof(header));
  request.request()->header.length = offset + sizeof(header) + netbuf.operation()->data_size;

  return ZX_OK;
}

void Asix88179Ethernet::EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                                            ethernet_impl_queue_tx_callback completion_cb,
                                            void* cookie) {
  eth::BorrowedOperation<> op(netbuf, completion_cb, cookie, sizeof(ethernet_netbuf_t));

  size_t length = op.operation()->data_size;
  if (length > (kMtu + kMaxEthernetHeaderSize)) {
    zxlogf(ERROR, "ax88179: unsupported packet length %zu", length);
    op.Complete(ZX_ERR_INVALID_ARGS);
    return;
  }

  zx::nanosleep(zx::deadline_after(tx_endpoint_delay_));

  fbl::AutoLock tx_lock(&tx_lock_);
  std::optional<usb::Request<>> request;

  if (!pending_netbuf_queue_.is_empty()) {
    pending_netbuf_queue_.push(std::move(op));
    return;
  }

  if (pending_usb_transmit_queue_.is_empty()) {
    // If the pending queue is empty create a request from the free write pool
    request = free_write_pool_.Get(usb::Request<>::RequestSize(parent_req_size_));

    if (!request) {
      pending_netbuf_queue_.push(std::move(op));
      return;
    }
    request->request()->header.length = 0;
    if (RequestAppend(*request, op) != ZX_OK) {
      zxlogf(ERROR, "ax88179: append failed, length %zu", length);
      op.Complete(ZX_ERR_INTERNAL);
      return;
    }
  } else {
    // If the pending queue is not empty try to append to the last queued request
    request = pending_usb_transmit_queue_.pop_last();
    if (RequestAppend(*request, op) == ZX_ERR_BUFFER_TOO_SMALL) {
      pending_usb_transmit_queue_.push(*std::move(request));

      // Our data won't fit - grab a new request
      request = free_write_pool_.Get(usb::Request<>::RequestSize(parent_req_size_));
      if (!request) {
        pending_netbuf_queue_.push(std::move(op));
        return;
      }
      request->request()->header.length = 0;
      if (RequestAppend(*request, op) != ZX_OK) {
        zxlogf(ERROR, "ax88179: append failed, length %zu", length);
        op.Complete(ZX_ERR_INTERNAL);
        return;
      }
    }
  }

  bool was_transmit_empty = pending_usb_transmit_queue_.is_empty();
  pending_usb_transmit_queue_.push(*std::move(request));
  if ((options & ETHERNET_TX_OPT_MORE) && (was_transmit_empty)) {
    op.Complete(ZX_OK);
    return;
  }

  request = pending_usb_transmit_queue_.pop();
  usb_.RequestQueue(request->take(), &write_request_complete_);

  op.Complete(ZX_OK);
}

void Asix88179Ethernet::Shutdown() {
  {  // Lock scope
    fbl::AutoLock lock(&lock_);
    running_ = false;
    sync_completion_signal(&interrupt_completion_);
  }

  thrd_join(interrupt_thread_, nullptr);

  fbl::AutoLock lock(&lock_);
  usb_.CancelAll(bulk_in_address_);
  usb_.CancelAll(bulk_out_address_);
  usb_.CancelAll(interrupt_address_);

  ifc_.clear();
}

void Asix88179Ethernet::DdkRelease() {
  cancel_thread_.detach();
  delete this;
}

zx_status_t Asix88179Ethernet::EthernetImplQuery(uint32_t options, ethernet_info_t* info) {
  if (options) {
    return ZX_ERR_INVALID_ARGS;
  }

  *info = {};
  info->mtu = kMtu;
  memcpy(info->mac, mac_addr_, sizeof(mac_addr_));
  info->netbuf_size = eth::BorrowedOperation<>::OperationSize(sizeof(ethernet_netbuf_t));

  return ZX_OK;
}

void Asix88179Ethernet::EthernetImplStop() {
  fbl::AutoLock lock(&lock_);
  ifc_.clear();
}

zx_status_t Asix88179Ethernet::EthernetImplStart(const ethernet_ifc_protocol_t* ifc) {
  fbl::AutoLock lock(&lock_);

  if (ifc_.is_valid()) {
    zxlogf(ERROR, "ax88179:  Already bound!!!");
    return ZX_ERR_ALREADY_BOUND;
  }

  ifc_ = ddk::EthernetIfcProtocolClient(ifc);
  zxlogf(INFO, "ax88179: Started");

  // Set status in case the link came up before start.
  ifc_.Status(online_ ? ETHERNET_STATUS_ONLINE : 0);

  return ZX_OK;
}

zx_status_t Asix88179Ethernet::TwiddleRcrBit(uint16_t bit, bool on) {
  uint16_t rcr_bits = 0;
  zx_status_t status = ReadMac(AX88179_MAC_RCR, &rcr_bits);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: ReadMac from %#x failed: %d", AX88179_MAC_RCR, status);
    return status;
  }
  if (on) {
    rcr_bits |= bit;
  } else {
    rcr_bits &= static_cast<uint16_t>(~bit);
  }
  status = WriteMac(AX88179_MAC_RCR, rcr_bits);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: WriteMac to %#x failed: %d", AX88179_MAC_RCR, status);
  }
  return status;
}

zx_status_t Asix88179Ethernet::SetPromisc(bool on) {
  return TwiddleRcrBit(AX88179_RCR_PROMISC, on);
}

zx_status_t Asix88179Ethernet::SetMulticastPromisc(bool on) {
  if (multicast_filter_overflow_ && !on) {
    return ZX_OK;
  }
  return TwiddleRcrBit(AX88179_RCR_AMALL, on);
}

void Asix88179Ethernet::SetFilterBit(const uint8_t* mac, uint8_t* filter) {
  // Invert the seed (standard is ~0) and output to get usable bits.
  uint32_t crc = ~crc32(0, mac, ETH_MAC_SIZE);
  uint8_t reverse[8] = {0, 4, 2, 6, 1, 5, 3, 7};
  filter[reverse[crc & 7]] |= static_cast<uint8_t>(1 << reverse[(crc >> 3) & 7]);
}

zx_status_t Asix88179Ethernet::SetMulticastFilter(int32_t n_addresses, const uint8_t* address_bytes,
                                                  size_t address_size) {
  zx_status_t status = ZX_OK;
  multicast_filter_overflow_ = (n_addresses == ETHERNET_MULTICAST_FILTER_OVERFLOW) ||
                               (n_addresses > kMaxMulticastFilterAddress);
  if (multicast_filter_overflow_) {
    status = SetMulticastPromisc(true);
    return status;
  }
  if (address_size < n_addresses * ETH_MAC_SIZE)
    return ZX_ERR_OUT_OF_RANGE;

  uint8_t filter[kMulticastFilterNBytes] = {};
  for (int32_t i = 0; i < n_addresses; i++) {
    SetFilterBit(address_bytes + i * ETH_MAC_SIZE, filter);
  }
  status = WriteMac(AX88179_MAC_MFA, filter);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: WriteMac to %#x failed: %d", AX88179_MAC_MFA, status);
    return status;
  }
  return status;
}

zx_status_t Asix88179Ethernet::EthernetImplSetParam(uint32_t param, int32_t value, const void* data,
                                                    size_t data_size) {
  zx_status_t status = ZX_OK;

  fbl::AutoLock lock(&lock_);

  switch (param) {
    case ETHERNET_SETPARAM_PROMISC:
      status = SetPromisc(static_cast<bool>(value));
      break;
    case ETHERNET_SETPARAM_MULTICAST_PROMISC:
      status = SetMulticastPromisc(static_cast<bool>(value));
      break;
    case ETHERNET_SETPARAM_MULTICAST_FILTER:
      status = SetMulticastFilter(value, static_cast<const uint8_t*>(data), data_size);
      break;
    case ETHERNET_SETPARAM_DUMP_REGS:
      DumpRegs();
      break;
    default:
      status = ZX_ERR_NOT_SUPPORTED;
  }

  return status;
}

template <uint8_t N>
void Asix88179Ethernet::DumpRegister(const char* name, uint8_t register_address) {
  union {
    uint64_t value;
    uint8_t bytes[N];
  } data;

  zx_status_t status = ReadMac(register_address, &(data.bytes));
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: could not read reg %s : %d", name, status);
  } else {
    zxlogf(TRACE, "ax88179: reg %s = %" PRIx64 "", name, data.value);
  }
}

#define STRINGIFY(x) #x

void Asix88179Ethernet::DumpRegs() {
  DumpRegister<1>(STRINGIFY(AX88179_MAC_PLSR), AX88179_MAC_PLSR);
  DumpRegister<1>(STRINGIFY(AX88179_MAC_GSR), AX88179_MAC_GSR);
  DumpRegister<1>(STRINGIFY(AX88179_MAC_SMSR), AX88179_MAC_SMSR);
  DumpRegister<1>(STRINGIFY(AX88179_MAC_CSR), AX88179_MAC_CSR);
  DumpRegister<2>(STRINGIFY(AX88179_MAC_RCR), AX88179_MAC_RCR);
  DumpRegister<kMulticastFilterNBytes>(STRINGIFY(AX88179_MAC_MFA), AX88179_MAC_MFA);
  DumpRegister<3>(STRINGIFY(AX88179_MAC_IPGCR), AX88179_MAC_IPGCR);
  DumpRegister<1>(STRINGIFY(AX88179_MAC_TR), AX88179_MAC_TR);
  DumpRegister<2>(STRINGIFY(AX88179_MAC_MSR), AX88179_MAC_MSR);
  DumpRegister<1>(STRINGIFY(AX88179_MAC_MMSR), AX88179_MAC_MMSR);
}

#undef STRINGIFY

zx_status_t Asix88179Ethernet::InitializeRegisters() {
  fbl::AutoLock lock(&lock_);

  // Enable embedded PHY
  zx_status_t status = WriteMac<uint16_t>(AX88179_MAC_EPPRCR, 0x0000);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: WriteMac to %#x failed: %d", AX88179_MAC_EPPRCR, status);
    return status;
  }
  zx::nanosleep(zx::deadline_after(zx::msec(1)));

  status = WriteMac<uint16_t>(AX88179_MAC_EPPRCR, 0x0020);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: WriteMac to %#x failed: %d", AX88179_MAC_EPPRCR, status);
    return status;
  }
  zx::nanosleep(zx::deadline_after(zx::msec(200)));

  // Switch clock to normal speed
  status = WriteMac<uint8_t>(AX88179_MAC_CLKSR, 0x03);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: WriteMac to %#x failed: %d", AX88179_MAC_CLKSR, status);
    return status;
  }
  zx::nanosleep(zx::deadline_after(zx::msec(1)));

  // Read the MAC addr
  status = ReadMac(AX88179_MAC_NIDR, &mac_addr_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: ReadMac to %#x failed: %d", AX88179_MAC_NIDR, status);
    return status;
  }

  zxlogf(INFO, "ax88179: MAC address: %02X:%02X:%02X:%02X:%02X:%02X", mac_addr_[0], mac_addr_[1],
         mac_addr_[2], mac_addr_[3], mac_addr_[4], mac_addr_[5]);

  // Ensure that the MAC RX is disabled
  status = WriteMac<uint16_t>(AX88179_MAC_RCR, 0x0000);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: WriteMac to %#x failed: %d", AX88179_MAC_RCR, status);
    return status;
  }

  // Set RX Bulk-in sizes -- use USB 3.0/1000Mbps at this point
  status = ConfigureBulkIn(AX88179_PLSR_USB_SS | AX88179_PLSR_EPHY_1000);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: WriteMac to %#x failed: %d", AX88179_MAC_RQCR, status);
    return status;
  }

  // Configure flow control watermark
  status = WriteMac<uint8_t>(AX88179_MAC_PWLLR, 0x3c);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: WriteMac to %#x failed: %d", AX88179_MAC_PWLLR, status);
    return status;
  }
  status = WriteMac<uint8_t>(AX88179_MAC_PWLHR, 0x5c);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: WriteMac to %#x failed: %d", AX88179_MAC_PWLHR, status);
    return status;
  }

  // RX/TX checksum offload: ipv4, tcp, udp, tcpv6, udpv6
  status = WriteMac<uint8_t>(AX88179_MAC_CRCR, 0x67);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: WriteMac to %#x failed: %d", AX88179_MAC_CRCR, status);
    return status;
  }
  status = WriteMac<uint8_t>(AX88179_MAC_CTCR, 0x67);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: WriteMac to %#x failed: %d", AX88179_MAC_CTCR, status);
    return status;
  }

  // TODO: PHY LED

  uint16_t phy_data = 0;

  // PHY auto-negotiation
  status = ReadPhy(AX88179_PHY_BMCR, &phy_data);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: ReadPhy to %#x failed: %d", AX88179_PHY_BMCR, status);
    return status;
  }
  phy_data |= 0x1200;
  status = WritePhy(AX88179_PHY_BMCR, phy_data);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: WritePhy to %#x failed: %d", AX88179_PHY_BMCR, status);
    return status;
  }

  // Default Ethernet medium mode
  status = WriteMac<uint16_t>(AX88179_MAC_MSR, 0x013b);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: WriteMac to %#x failed: %d", AX88179_MAC_MSR, status);
    return status;
  }

  // Enable MAC RX
  // TODO(eventually): Once we get IGMP, turn off AMALL unless someone wants it.
  status = WriteMac<uint16_t>(AX88179_MAC_RCR, AX88179_RCR_AMALL | AX88179_RCR_AB | AX88179_RCR_AM |
                                                   AX88179_RCR_SO | AX88179_RCR_DROP_CRCE_N |
                                                   AX88179_RCR_IPE_N);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: WriteMac to %#x failed: %d", AX88179_MAC_RCR, status);
    return status;
  }

  uint8_t filter[kMulticastFilterNBytes] = {};
  status = WriteMac(AX88179_MAC_MFA, filter);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: WriteMac to %#x failed: %d", AX88179_MAC_MFA, status);
    return status;
  }

  return ZX_OK;
}

int Asix88179Ethernet::InterruptThread() {
  zx_status_t status = InitializeRegisters();
  if (status != ZX_OK) {
    return status;
  }

  if (init_txn_) {
    // Replying to the init hook will make the device visible and also able to be unbound.
    init_txn_->Reply(ZX_OK);
  } else {
    zxlogf(ERROR, "ax88179: interrupt thread did not find an init txn to complete");
  }

  while (true) {
    {  // Lock scope
      fbl::AutoLock lock(&lock_);

      usb_.RequestQueue(interrupt_request_->request(), &interrupt_request_complete_);
    }

    sync_completion_wait(&interrupt_completion_, ZX_TIME_INFINITE);

    {  // Lock scope
      fbl::AutoLock lock(&lock_);

      if (!running_) {
        return 0;
      }

      sync_completion_reset(&interrupt_completion_);

      status = interrupt_request_->request()->response.status;
      if (status != ZX_OK) {
        return status;
      }
    }
  }

  return 0;
}

void Asix88179Ethernet::DdkInit(ddk::InitTxn txn) {
  {
    fbl::AutoLock lock(&lock_);
    running_ = true;
  }
  // Save the txn so we can reply to it from the interrupt thread.
  init_txn_ = std::move(txn);

  int ret = thrd_create_with_name(
      &interrupt_thread_,
      [](void* arg) -> int { return static_cast<Asix88179Ethernet*>(arg)->InterruptThread(); },
      this, "asix-88179-thread");
  if (ret != thrd_success) {
    zxlogf(ERROR, "ax88179: failed to create interrupt thread: %d", ret);
    // Inform the device manager initialization failed, so that the device will be unbound,
    init_txn_->Reply(ZX_ERR_INTERNAL);
  }
}

void Asix88179Ethernet::DdkUnbind(ddk::UnbindTxn txn) {
  cancel_thread_ = std::thread([this, unbind_txn = std::move(txn)]() mutable {
    Shutdown();
    unbind_txn.Reply();
  });
}

zx_status_t Asix88179Ethernet::Initialize() {
  zx_status_t status = ZX_OK;

  usb::UsbDevice usb(parent());
  if (!usb.is_valid()) {
    return ZX_ERR_PROTOCOL_NOT_SUPPORTED;
  }

  // find our endpoints
  std::optional<usb::InterfaceList> usb_interface_list;
  status = usb::InterfaceList::Create(usb, true, &usb_interface_list);
  if (status != ZX_OK) {
    return status;
  }

  auto interface = usb_interface_list->begin();
  const usb_interface_descriptor_t* interface_descriptor = interface->descriptor();
  if (interface == usb_interface_list->end()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (interface_descriptor->bNumEndpoints < 3) {
    zxlogf(ERROR, "ax88179: Wrong number of endpoints: %d", interface_descriptor->bNumEndpoints);
    return ZX_ERR_NOT_SUPPORTED;
  }

  fbl::AutoLock lock(&lock_);

  interface_number_ = interface_descriptor->bInterfaceNumber;
  uint8_t bulk_in_address = 0;
  uint8_t bulk_out_address = 0;
  uint8_t interrupt_address = 0;

  for (auto endpoint : interface->GetEndpointList()) {
    const usb_endpoint_descriptor_t* endp = &endpoint.descriptor;
    if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
      if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
        bulk_out_address = endp->bEndpointAddress;
      }
    } else {
      if (usb_ep_type(endp) == USB_ENDPOINT_BULK) {
        bulk_in_address = endp->bEndpointAddress;
      } else if (usb_ep_type(endp) == USB_ENDPOINT_INTERRUPT) {
        interrupt_address = endp->bEndpointAddress;
      }
    }
  }

  if (!bulk_in_address || !bulk_out_address || !interrupt_address) {
    zxlogf(ERROR, "ax88179: Bind could not find endpoints");
    return ZX_ERR_NOT_SUPPORTED;
  }

  usb_ = usb;
  bulk_in_address_ = bulk_in_address;
  bulk_out_address_ = bulk_out_address;
  interrupt_address_ = interrupt_address;

  parent_req_size_ = usb_.GetRequestSize();

  rx_endpoint_delay_ = kEthernetInitialReceiveDelay;
  tx_endpoint_delay_ = kEthernetInitialTransmitDelay;

  for (int i = 0; i < kReadRequestCount; i++) {
    std::optional<usb::Request<>> request;
    status = usb::Request<>::Alloc(&request, kUsbBufferSize, bulk_in_address, parent_req_size_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "ax88179: allocating reads failed %d", status);
      Shutdown();
      return status;
    }
    free_read_pool_.Add(*std::move(request));
  }

  for (int i = 0; i < kWriteRequestCount; i++) {
    std::optional<usb::Request<>> request;
    status = usb::Request<>::Alloc(&request, kUsbBufferSize, bulk_out_address, parent_req_size_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "ax88179: allocating writes failed %d", status);
      Shutdown();
      return status;
    }
    free_write_pool_.Add(*std::move(request));
  }

  status = usb::Request<>::Alloc(&interrupt_request_, kInterruptRequestSize, interrupt_address,
                                 parent_req_size_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: allocating interrupt failed %d", status);
    Shutdown();
    return status;
  }

  /* This is not needed, as long as the xhci stack does it for us.
  status = usb_set_configuration(device, 1);
  if (status < 0) {
      zxlogf(ERROR, "aax88179_bind could not set configuration: %d", status);
      return ZX_ERR_NOT_SUPPORTED;
  }
  */

  status = DdkAdd("ax88179");
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: failed to create device: %d", status);
    Shutdown();
    return status;
  }
  return ZX_OK;
}

zx_status_t Asix88179Ethernet::Bind(void* ctx, zx_device_t* dev) {
  fbl::AllocChecker ac;
  auto eth_device = fbl::make_unique_checked<Asix88179Ethernet>(&ac, dev);

  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = eth_device->Initialize();
  if (status != ZX_OK) {
    zxlogf(ERROR, "ax88179: ethernet driver failed to get added: %d", status);
    return status;
  } else {
    zxlogf(INFO, "ax88179: ethernet driver added");
  }

  // On successful Add, Devmgr takes ownership (relinquished on DdkRelease),
  // so transfer our ownership to a local var, and let it go out of scope.
  auto __UNUSED temp_ref = eth_device.release();

  return ZX_OK;
}

}  // namespace eth

static constexpr zx_driver_ops_t ax88179_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = &eth::Asix88179Ethernet::Bind;

  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(ethernet_ax88179, ax88179_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_VID, ASIX_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, AX88179_PID),
ZIRCON_DRIVER_END(ethernet_ax88179)
