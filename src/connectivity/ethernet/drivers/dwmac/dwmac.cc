// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dwmac.h"

#include <lib/device-protocol/platform-device.h>
#include <lib/fzl/vmar-manager.h>
#include <lib/operation/ethernet.h>
#include <lib/zircon-internal/align.h>
#include <lib/zx/clock.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/compiler.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/ethernet/mac.h>
#include <ddk/protocol/platform/device.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <hw/arch_ops.h>
#include <hw/reg.h>

#include "dw-gmac-dma.h"

namespace eth {

namespace {

enum {
  FRAGMENT_PDEV,
  FRAGMENT_ETH_BOARD,
  FRAGMENT_COUNT,
};

// MMIO Indexes.
constexpr uint32_t kEthMacMmio = 0;

}  // namespace

template <typename T, typename U>
static inline T* offset_ptr(U* ptr, size_t offset) {
  return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(ptr) + offset);
}

int DWMacDevice::Thread() {
  zxlogf(INFO, "dwmac: ethmac started");

  zx_status_t status;
  while (true) {
    status = dma_irq_.wait(nullptr);
    if (!running_.load()) {
      status = ZX_OK;
      break;
    }
    if (status != ZX_OK) {
      zxlogf(ERROR, "dwmac: Interrupt error");
      break;
    }
    uint32_t stat = mmio_->Read32(DW_MAC_DMA_STATUS);
    mmio_->Write32(stat, DW_MAC_DMA_STATUS);

    if (stat & DMA_STATUS_GLI) {
      fbl::AutoLock lock(&lock_);  // Note: limited scope of autolock
      UpdateLinkStatus();
    }
    if (stat & DMA_STATUS_RI) {
      ProcRxBuffer(stat);
    }
    if (stat & DMA_STATUS_AIS) {
      bus_errors_++;
      zxlogf(ERROR, "dwmac: abnormal interrupt. status = 0x%08x", stat);
    }
  }
  return status;
}

int DWMacDevice::WorkerThread() {
  // Note: Need to wait here for all PHY's to register
  //       their callbacks before proceeding further.
  //       Currently only supporting single PHY, we can add
  //       support for multiple PHY's easily when needed.
  sync_completion_wait(&cb_registered_signal_, ZX_TIME_INFINITE);

  // Configure the phy.
  cbs_.config_phy(cbs_.ctx, mac_);

  InitDevice();

  auto thunk = [](void* arg) -> int { return reinterpret_cast<DWMacDevice*>(arg)->Thread(); };

  running_.store(true);
  int ret = thrd_create_with_name(&thread_, thunk, this, "mac-thread");
  ZX_DEBUG_ASSERT(ret == thrd_success);

  zx_status_t status = DdkAdd("Designware MAC");
  if (status != ZX_OK) {
    zxlogf(ERROR, "dwmac: Could not create eth device: %d", status);
    return status;
  } else {
    zxlogf(INFO, "dwmac: Added dwMac device");
  }
  return status;
}

void DWMacDevice::UpdateLinkStatus() {
  bool temp = mmio_->ReadMasked32(GMAC_RGMII_STATUS_LNKSTS, DW_MAC_MAC_RGMIISTATUS);

  if (temp != online_) {
    online_ = temp;
    if (ethernet_client_.is_valid()) {
      ethernet_client_.Status(online_ ? ETHERNET_STATUS_ONLINE : 0u);
    } else {
      zxlogf(ERROR, "dwmac: System not ready");
    }
  }
  if (online_) {
    mmio_->SetBits32((GMAC_CONF_TE | GMAC_CONF_RE), DW_MAC_MAC_CONF);
  } else {
    mmio_->ClearBits32((GMAC_CONF_TE | GMAC_CONF_RE), DW_MAC_MAC_CONF);
  }
  zxlogf(INFO, "dwmac: Link is now %s", online_ ? "up" : "down");
}

zx_status_t DWMacDevice::InitPdev() {
  // Map mac control registers and dma control registers.
  auto status = pdev_.MapMmio(kEthMacMmio, &mmio_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "dwmac: could not map dwmac mmio: %d", status);
    return status;
  }

  // Map dma interrupt.
  status = pdev_.GetInterrupt(0, &dma_irq_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "dwmac: could not map dma interrupt");
    return status;
  }

  // Get our bti.
  status = pdev_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "dwmac: could not obtain bti: %d", status);
    return status;
  }

  // Get ETH_BOARD protocol.
  if (!eth_board_.is_valid()) {
    zxlogf(ERROR, "dwmac: could not obtain ETH_BOARD protocol: %d", status);
    return status;
  }

  return status;
}

zx_status_t DWMacDevice::Create(void* ctx, zx_device_t* device) {
  composite_protocol_t composite;
  pdev_protocol_t pdev;
  eth_board_protocol_t eth_board;

  auto status = device_get_protocol(device, ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not get ZX_PROTOCOL_COMPOSITE", __func__);
    return status;
  }

  zx_device_t* fragments[FRAGMENT_COUNT];
  size_t actual;
  composite_get_fragments(&composite, fragments, FRAGMENT_COUNT, &actual);
  if (actual != 2) {
    zxlogf(ERROR, "%s could not get fragments", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  status = device_get_protocol(fragments[FRAGMENT_PDEV], ZX_PROTOCOL_PDEV, &pdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not get ZX_PROTOCOL_PDEV", __func__);
    return status;
  }

  status = device_get_protocol(fragments[FRAGMENT_ETH_BOARD], ZX_PROTOCOL_ETH_BOARD, &eth_board);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s could not get ZX_PROTOCOL_ETH_BOARD", __func__);
    return status;
  }

  auto mac_device = std::make_unique<DWMacDevice>(device, &pdev, &eth_board);

  status = mac_device->InitPdev();
  if (status != ZX_OK) {
    return status;
  }

  // Reset the phy.
  mac_device->eth_board_.ResetPhy();

  // Get and cache the mac address.
  mac_device->GetMAC(fragments[FRAGMENT_PDEV]);

  // Reset the dma peripheral.
  mac_device->mmio_->SetBits32(DMAMAC_SRST, DW_MAC_DMA_BUSMODE);
  uint32_t loop_count = 10;
  do {
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    loop_count--;
  } while (mac_device->mmio_->ReadMasked32(DMAMAC_SRST, DW_MAC_DMA_BUSMODE) && loop_count);
  if (!loop_count) {
    return ZX_ERR_TIMED_OUT;
  }

  // Mac address register was erased by the reset; set it!
  mac_device->mmio_->Write32((mac_device->mac_[5] << 8) | (mac_device->mac_[4] << 0),
                             DW_MAC_MAC_MACADDR0HI);
  mac_device->mmio_->Write32((mac_device->mac_[3] << 24) | (mac_device->mac_[2] << 16) |
                                 (mac_device->mac_[1] << 8) | (mac_device->mac_[0] << 0),
                             DW_MAC_MAC_MACADDR0LO);

  auto cleanup = fbl::MakeAutoCall([&]() { mac_device->ShutDown(); });

  status = mac_device->InitBuffers();
  if (status != ZX_OK)
    return status;

  sync_completion_reset(&mac_device->cb_registered_signal_);

  // Populate board specific information
  eth_dev_metadata_t phy_info;
  status = device_get_metadata(fragments[FRAGMENT_PDEV], DEVICE_METADATA_ETH_PHY_DEVICE, &phy_info,
                               sizeof(eth_dev_metadata_t), &actual);
  if (status != ZX_OK || actual != sizeof(eth_dev_metadata_t)) {
    zxlogf(ERROR, "dwmac: Could not get PHY metadata %d", status);
    return status;
  }

  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, phy_info.vid},
      {BIND_PLATFORM_DEV_PID, 0, phy_info.pid},
      {BIND_PLATFORM_DEV_DID, 0, phy_info.did},
  };

  device_add_args_t phy_device_args = {};
  phy_device_args.version = DEVICE_ADD_ARGS_VERSION;
  phy_device_args.name = "eth_phy";
  phy_device_args.ops = &mac_device->ddk_device_proto_,
  phy_device_args.proto_id = ZX_PROTOCOL_ETH_MAC;
  phy_device_args.props = props;
  phy_device_args.prop_count = countof(props);
  phy_device_args.ctx = mac_device.get();
  phy_device_args.proto_ops = &mac_device->eth_mac_protocol_ops_;

  // TODO(braval): use proper device pointer, depending on how
  //               many PHY devices we have to load, from the metadata.
  zx_device_t* dev;
  status = device_add(device, &phy_device_args, &dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "dwmac: Could not create phy device: %d", status);

    return status;
  }

  auto worker_thunk = [](void* arg) -> int {
    return reinterpret_cast<DWMacDevice*>(arg)->WorkerThread();
  };

  int ret = thrd_create_with_name(&mac_device->worker_thread_, worker_thunk,
                                  reinterpret_cast<void*>(mac_device.get()), "mac-worker-thread");
  ZX_DEBUG_ASSERT(ret == thrd_success);

  cleanup.cancel();

  // mac_device intentionally leaked as it is now held by DevMgr.
  __UNUSED auto ptr = mac_device.release();
  return ZX_OK;
}  // namespace eth

zx_status_t DWMacDevice::InitBuffers() {
  constexpr size_t kDescSize = ZX_ROUNDUP(2 * kNumDesc * sizeof(dw_dmadescr_t), PAGE_SIZE);

  constexpr size_t kBufSize = 2 * kNumDesc * kTxnBufSize;

  txn_buffer_ = PinnedBuffer::Create(kBufSize, bti_, ZX_CACHE_POLICY_CACHED);
  desc_buffer_ = PinnedBuffer::Create(kDescSize, bti_, ZX_CACHE_POLICY_UNCACHED);

  tx_buffer_ = static_cast<uint8_t*>(txn_buffer_->GetBaseAddress());
  zx_cache_flush(tx_buffer_, kBufSize, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
  // rx buffer right after tx
  rx_buffer_ = &tx_buffer_[kBufSize / 2];

  tx_descriptors_ = static_cast<dw_dmadescr_t*>(desc_buffer_->GetBaseAddress());
  // rx descriptors right after tx
  rx_descriptors_ = &tx_descriptors_[kNumDesc];

  zx_paddr_t tmpaddr;

  // Initialize descriptors. Doing tx and rx all at once
  for (uint i = 0; i < kNumDesc; i++) {
    desc_buffer_->LookupPhys(((i + 1) % kNumDesc) * sizeof(dw_dmadescr_t), &tmpaddr);
    tx_descriptors_[i].dmamac_next = static_cast<uint32_t>(tmpaddr);

    txn_buffer_->LookupPhys(i * kTxnBufSize, &tmpaddr);
    tx_descriptors_[i].dmamac_addr = static_cast<uint32_t>(tmpaddr);
    tx_descriptors_[i].txrx_status = 0;
    tx_descriptors_[i].dmamac_cntl = DESC_TXCTRL_TXCHAIN;

    desc_buffer_->LookupPhys((((i + 1) % kNumDesc) + kNumDesc) * sizeof(dw_dmadescr_t), &tmpaddr);
    rx_descriptors_[i].dmamac_next = static_cast<uint32_t>(tmpaddr);

    txn_buffer_->LookupPhys((i + kNumDesc) * kTxnBufSize, &tmpaddr);
    rx_descriptors_[i].dmamac_addr = static_cast<uint32_t>(tmpaddr);
    rx_descriptors_[i].dmamac_cntl =
        (MAC_MAX_FRAME_SZ & DESC_RXCTRL_SIZE1MASK) | DESC_RXCTRL_RXCHAIN;

    rx_descriptors_[i].txrx_status = DESC_RXSTS_OWNBYDMA;
  }

  desc_buffer_->LookupPhys(0, &tmpaddr);
  mmio_->Write32(static_cast<uint32_t>(tmpaddr), DW_MAC_DMA_TXDESCLISTADDR);

  desc_buffer_->LookupPhys(kNumDesc * sizeof(dw_dmadescr_t), &tmpaddr);
  mmio_->Write32(static_cast<uint32_t>(tmpaddr), DW_MAC_DMA_RXDESCLISTADDR);

  return ZX_OK;
}

void DWMacDevice::EthernetImplGetBti(zx::bti* bti) { bti_.duplicate(ZX_RIGHT_SAME_RIGHTS, bti); }

zx_status_t DWMacDevice::EthMacMdioWrite(uint32_t reg, uint32_t val) {
  mmio_->Write32(val, DW_MAC_MAC_MIIDATA);

  uint32_t miiaddr = (mii_addr_ << MIIADDRSHIFT) | (reg << MIIREGSHIFT) | MII_WRITE;

  mmio_->Write32(miiaddr | MII_CLKRANGE_150_250M | MII_BUSY, DW_MAC_MAC_MIIADDR);

  zx::time deadline = zx::deadline_after(zx::msec(3));
  do {
    if (!mmio_->ReadMasked32(MII_BUSY, DW_MAC_MAC_MIIADDR)) {
      return ZX_OK;
    }
    zx::nanosleep(zx::deadline_after(zx::usec(10)));
  } while (zx::clock::get_monotonic() < deadline);
  return ZX_ERR_TIMED_OUT;
}

zx_status_t DWMacDevice::EthMacMdioRead(uint32_t reg, uint32_t* val) {
  uint32_t miiaddr = (mii_addr_ << MIIADDRSHIFT) | (reg << MIIREGSHIFT);

  mmio_->Write32(miiaddr | MII_CLKRANGE_150_250M | MII_BUSY, DW_MAC_MAC_MIIADDR);

  zx::time deadline = zx::deadline_after(zx::msec(3));
  do {
    if (!mmio_->ReadMasked32(MII_BUSY, DW_MAC_MAC_MIIADDR)) {
      *val = mmio_->Read32(DW_MAC_MAC_MIIDATA);
      return ZX_OK;
    }
    zx::nanosleep(zx::deadline_after(zx::usec(10)));
  } while (zx::clock::get_monotonic() < deadline);
  return ZX_ERR_TIMED_OUT;
}

zx_status_t DWMacDevice::EthMacRegisterCallbacks(const eth_mac_callbacks_t* cbs) {
  if (cbs == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  cbs_ = *cbs;

  sync_completion_signal(&cb_registered_signal_);
  return ZX_OK;
}

DWMacDevice::DWMacDevice(zx_device_t* device, pdev_protocol_t* pdev,
                         eth_board_protocol_t* eth_board)
    : ddk::Device<DWMacDevice, ddk::Unbindable>(device), pdev_(pdev), eth_board_(eth_board) {}

void DWMacDevice::ReleaseBuffers() {
  // Unpin the memory used for the dma buffers
  if (txn_buffer_->UnPin() != ZX_OK) {
    zxlogf(ERROR, "dwmac: Error unpinning transaction buffers");
  }
  if (desc_buffer_->UnPin() != ZX_OK) {
    zxlogf(ERROR, "dwmac: Error unpinning description buffers");
  }
}

void DWMacDevice::DdkRelease() {
  zxlogf(INFO, "Ethernet release...");
  delete this;
}

void DWMacDevice::DdkUnbind(ddk::UnbindTxn txn) {
  zxlogf(INFO, "Ethernet DdkUnbind");
  ShutDown();
  txn.Reply();
}

zx_status_t DWMacDevice::ShutDown() {
  if (running_.load()) {
    running_.store(false);
    dma_irq_.destroy();
    thrd_join(thread_, NULL);
  }
  fbl::AutoLock lock(&lock_);
  online_ = false;
  ethernet_client_.clear();
  DeInitDevice();
  ReleaseBuffers();
  return ZX_OK;
}

zx_status_t DWMacDevice::GetMAC(zx_device_t* dev) {
  // look for MAC address device metadata
  // metadata is padded so we need buffer size > 6 bytes
  uint8_t buffer[16];
  size_t actual;
  zx_status_t status =
      device_get_metadata(dev, DEVICE_METADATA_MAC_ADDRESS, buffer, sizeof(buffer), &actual);
  if (status != ZX_OK || actual < 6) {
    zxlogf(ERROR, "dwmac: MAC address metadata load failed. Falling back on HW setting.");
    // read MAC address from hardware register
    uint32_t hi = mmio_->Read32(DW_MAC_MAC_MACADDR0HI);
    uint32_t lo = mmio_->Read32(DW_MAC_MAC_MACADDR0LO);

    /* Extract the MAC address from the high and low words */
    buffer[0] = static_cast<uint8_t>(lo & 0xff);
    buffer[1] = static_cast<uint8_t>((lo >> 8) & 0xff);
    buffer[2] = static_cast<uint8_t>((lo >> 16) & 0xff);
    buffer[3] = static_cast<uint8_t>((lo >> 24) & 0xff);
    buffer[4] = static_cast<uint8_t>(hi & 0xff);
    buffer[5] = static_cast<uint8_t>((hi >> 8) & 0xff);
  }

  zxlogf(INFO, "dwmac: MAC address %02x:%02x:%02x:%02x:%02x:%02x", buffer[0], buffer[1],
         buffer[2], buffer[3], buffer[4], buffer[5]);
  memcpy(mac_, buffer, sizeof mac_);
  return ZX_OK;
}

zx_status_t DWMacDevice::EthernetImplQuery(uint32_t options, ethernet_info_t* info) {
  memset(info, 0, sizeof(*info));
  info->features = ETHERNET_FEATURE_DMA;
  info->mtu = 1500;
  memcpy(info->mac, mac_, sizeof info->mac);
  info->netbuf_size = eth::BorrowedOperation<>::OperationSize(sizeof(ethernet_netbuf_t));
  return ZX_OK;
}

void DWMacDevice::EthernetImplStop() {
  zxlogf(INFO, "Stopping Ethermac");
  fbl::AutoLock lock(&lock_);
  ethernet_client_.clear();
}

zx_status_t DWMacDevice::EthernetImplStart(const ethernet_ifc_protocol_t* ifc) {
  fbl::AutoLock lock(&lock_);

  if (ethernet_client_.is_valid()) {
    zxlogf(ERROR, "dwmac:  Already bound!!!");
    return ZX_ERR_ALREADY_BOUND;
  } else {
    ethernet_client_ = ddk::EthernetIfcProtocolClient(ifc);
    UpdateLinkStatus();
    zxlogf(INFO, "dwmac: Started");
  }
  return ZX_OK;
}

zx_status_t DWMacDevice::InitDevice() {
  mmio_->Write32(0, DW_MAC_DMA_INTENABLE);

  mmio_->Write32(X8PBL | DMA_PBL, DW_MAC_DMA_BUSMODE);

  mmio_->Write32(DMA_OPMODE_TSF | DMA_OPMODE_RSF, DW_MAC_DMA_OPMODE);
  mmio_->SetBits32(DMA_OPMODE_SR | DMA_OPMODE_ST, DW_MAC_DMA_OPMODE);

  // Clear all the interrupt flags
  mmio_->Write32(~0, DW_MAC_DMA_STATUS);

  // Disable(mask) interrupts generated by the mmc block
  mmio_->Write32(~0, DW_MAC_MMC_INTR_MASK_RX);
  mmio_->Write32(~0, DW_MAC_MMC_INTR_MASK_TX);
  mmio_->Write32(~0, DW_MAC_MMC_IPC_INTR_MASK_RX);

  // Enable Interrupts
  mmio_->Write32(DMA_INT_NIE | DMA_INT_AIE | DMA_INT_FBE | DMA_INT_RIE | DMA_INT_RUE | DMA_INT_OVE |
                     DMA_INT_UNE | DMA_INT_TSE | DMA_INT_RSE,
                 DW_MAC_DMA_INTENABLE);

  mmio_->Write32(0, DW_MAC_MAC_MACADDR0HI);
  mmio_->Write32(0, DW_MAC_MAC_MACADDR0LO);
  mmio_->Write32(~0, DW_MAC_MAC_HASHTABLEHI);
  mmio_->Write32(~0, DW_MAC_MAC_HASHTABLELO);

  // TODO - configure filters
  zxlogf(INFO, "macaddr0hi = %08x", mmio_->Read32(DW_MAC_MAC_MACADDR0HI));
  zxlogf(INFO, "macaddr0lo = %08x", mmio_->Read32(DW_MAC_MAC_MACADDR0LO));

  mmio_->SetBits32((1 << 10) | (1 << 4) | (1 << 0), DW_MAC_MAC_FRAMEFILT);

  mmio_->Write32(GMAC_CORE_INIT, DW_MAC_MAC_CONF);

  return ZX_OK;
}

zx_status_t DWMacDevice::DeInitDevice() {
  // Disable Interrupts
  mmio_->Write32(0, DW_MAC_DMA_INTENABLE);

  // Disable Transmit and Receive
  mmio_->ClearBits32(GMAC_CONF_TE | GMAC_CONF_RE, DW_MAC_MAC_CONF);

  // reset the phy (hold in reset)
  // gpio_write(&gpios_[PHY_RESET], 0);

  // transmit and receive are not disables, safe to null descriptor list ptrs
  mmio_->Write32(0, DW_MAC_DMA_TXDESCLISTADDR);
  mmio_->Write32(0, DW_MAC_DMA_RXDESCLISTADDR);

  return ZX_OK;
}

uint32_t DWMacDevice::DmaRxStatus() {
  return mmio_->ReadMasked32(DMA_STATUS_RS_MASK, DW_MAC_DMA_STATUS) >> DMA_STATUS_RS_POS;
}

void DWMacDevice::ProcRxBuffer(uint32_t int_status) {
  while (true) {
    uint32_t pkt_stat = rx_descriptors_[curr_rx_buf_].txrx_status;

    if (pkt_stat & DESC_RXSTS_OWNBYDMA) {
      return;
    }
    size_t fr_len = (pkt_stat & DESC_RXSTS_FRMLENMSK) >> DESC_RXSTS_FRMLENSHFT;
    if (fr_len > kTxnBufSize) {
      zxlogf(ERROR, "dwmac: unsupported packet size received");
      return;
    }

    uint8_t* temptr = &rx_buffer_[curr_rx_buf_ * kTxnBufSize];

    zx_cache_flush(temptr, kTxnBufSize, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);

    {  // limit scope of autolock
      fbl::AutoLock lock(&lock_);
      if ((ethernet_client_.is_valid())) {
        ethernet_client_.Recv(temptr, fr_len, 0);

      } else {
        zxlogf(ERROR, "Dropping bad packet");
      }
    };

    rx_descriptors_[curr_rx_buf_].txrx_status = DESC_RXSTS_OWNBYDMA;
    rx_packet_++;

    curr_rx_buf_ = (curr_rx_buf_ + 1) % kNumDesc;
    if (curr_rx_buf_ == 0) {
      loop_count_++;
    }
    mmio_->Write32(~0, DW_MAC_DMA_RXPOLLDEMAND);
  }
}

void DWMacDevice::EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                                      ethernet_impl_queue_tx_callback completion_cb, void* cookie) {
  eth::BorrowedOperation<> op(netbuf, completion_cb, cookie, sizeof(ethernet_netbuf_t));

  {  // Check to make sure we are ready to accept packets
    fbl::AutoLock lock(&lock_);
    if (!online_) {
      op.Complete(ZX_ERR_UNAVAILABLE);
      return;
    }
  }

  if (op.operation()->data_size > kTxnBufSize) {
    op.Complete(ZX_ERR_INVALID_ARGS);
    return;
  }
  if (tx_descriptors_[curr_tx_buf_].txrx_status & DESC_TXSTS_OWNBYDMA) {
    zxlogf(ERROR, "dwmac: TX buffer overrun@ %u", curr_tx_buf_);
    op.Complete(ZX_ERR_UNAVAILABLE);
    return;
  }
  uint8_t* temptr = &tx_buffer_[curr_tx_buf_ * kTxnBufSize];

  memcpy(temptr, op.operation()->data_buffer, op.operation()->data_size);
  hw_mb();

  zx_cache_flush(temptr, netbuf->data_size, ZX_CACHE_FLUSH_DATA);

  // Descriptors are pre-iniitialized with the paddr of their corresponding
  // buffers, only need to setup the control and status fields.
  tx_descriptors_[curr_tx_buf_].dmamac_cntl = DESC_TXCTRL_TXINT | DESC_TXCTRL_TXLAST |
                                              DESC_TXCTRL_TXFIRST | DESC_TXCTRL_TXCHAIN |
                                              ((uint32_t)netbuf->data_size & DESC_TXCTRL_SIZE1MASK);

  tx_descriptors_[curr_tx_buf_].txrx_status = DESC_TXSTS_OWNBYDMA;
  curr_tx_buf_ = (curr_tx_buf_ + 1) % kNumDesc;

  hw_mb();
  mmio_->Write32(~0, DW_MAC_DMA_TXPOLLDEMAND);
  tx_counter_++;
  op.Complete(ZX_OK);
}

zx_status_t DWMacDevice::EthernetImplSetParam(uint32_t param, int32_t value, const void* data,
                                              size_t data_size) {
  zxlogf(INFO, "dwmac: SetParam called  %x  %x", param, value);
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = DWMacDevice::Create;
  return ops;
}();

}  // namespace eth

// clang-format off
ZIRCON_DRIVER_BEGIN(dwmac, eth::driver_ops, "designware_mac", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_DESIGNWARE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_ETH_MAC),
ZIRCON_DRIVER_END(dwmac)
