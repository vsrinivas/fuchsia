// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_DWMAC_DWMAC_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_DWMAC_DWMAC_H_

#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/sync/completion.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/vmo.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <atomic>
#include <optional>

#include <ddk/device.h>
#include <ddk/protocol/test.h>
#include <ddktl/device.h>
#include <ddktl/protocol/ethernet.h>
#include <ddktl/protocol/ethernet/board.h>
#include <ddktl/protocol/ethernet/mac.h>
#include <fbl/mutex.h>

#include "pinned-buffer.h"

// clang-format off
#define DW_MAC_MAC_CONF             (0x0000)
#define DW_MAC_MAC_FRAMEFILT        (0x0004)
#define DW_MAC_MAC_HASHTABLEHI      (0x0008)
#define DW_MAC_MAC_HASHTABLELO      (0x000c)
#define DW_MAC_MAC_MIIADDR          (0x0010)
#define DW_MAC_MAC_MIIDATA          (0x0014)
#define DW_MAC_MAC_FLOWCONTROL      (0x0018)
#define DW_MAC_MAC_VALANTAG         (0x001c)
#define DW_MAC_MAC_VERSION          (0x0020)
#define DW_MAC_MAC_DEBUG            (0x0024)
#define DW_MAC_MAC_REMOTEWAKEFILT   (0x0028)
#define DW_MAC_MAC_PMTCONTROL       (0x002c)
#define DW_MAC_MAC_LPICONTROL       (0x0030)
#define DW_MAC_MAC_LPITIMERS        (0x0034)
#define DW_MAC_MAC_INTREG           (0x0038)
#define DW_MAC_MAC_INTMASK          (0x003c)
#define DW_MAC_MAC_MACADDR0HI       (0x0040)
#define DW_MAC_MAC_MACADDR0LO       (0x0044)
#define DW_MAC_MAC_MACADDR1HI       (0x0048)
#define DW_MAC_MAC_MACADDR1LO       (0x004c)
#define DW_MAC_MAC_RGMIISTATUS      (0x00d8)

// Offsets of the mac management counters
#define DW_MAC_MMC_CNTRL            (0x0100)
#define DW_MAC_MMC_INTR_RX          (0x0104)
#define DW_MAC_MMC_INTR_TX          (0x0108)
#define DW_MAC_MMC_INTR_MASK_RX     (0x010c)
#define DW_MAC_MMC_INTR_MASK_TX     (0x0110)
#define DW_MAC_MMC_RXFRAMECOUNT_GB  (0x0180)
#define DW_MAC_MMC_RXOCTETCOUNT_GB  (0x0184)
#define DW_MAC_MMC_RXOCTETCOUNT_G   (0x0188)
#define DW_MAC_MMC_IPC_INTR_MASK_RX (0x0200)
#define DW_MAC_MMC_IPC_INTR_RX      (0x0208)

// Offsets of DMA registers
#define DW_MAC_DMA_BUSMODE              (0x1000)
#define DW_MAC_DMA_TXPOLLDEMAND         (0x1004)
#define DW_MAC_DMA_RXPOLLDEMAND         (0x1008)
#define DW_MAC_DMA_RXDESCLISTADDR       (0x100c)
#define DW_MAC_DMA_TXDESCLISTADDR       (0x1010)
#define DW_MAC_DMA_STATUS               (0x1014)
#define DW_MAC_DMA_OPMODE               (0x1018)
#define DW_MAC_DMA_INTENABLE            (0x101c)
#define DW_MAC_DMA_MISSEDFRAMES         (0x1020)
#define DW_MAC_DMA_RXWDT                (0x1024)
#define DW_MAC_DMA_AXIBUSMODE           (0x1028)
#define DW_MAC_DMA_AXISTATUS            (0x102c)
#define DW_MAC_DMA_CURRHOSTTXDESC       (0x1048)
#define DW_MAC_DMA_CURRHOSTRXDESC       (0x104c)
#define DW_MAC_DMA_CURRHOSTTXBUFFADDR   (0x1050)
#define DW_MAC_DMA_CURRHOSTRXBUFFADDR   (0x1054)
#define DW_MAC_DMA_HWFEATURE            (0x1058)

//DMA transaction descriptors
typedef volatile struct dw_dmadescr {
    uint32_t txrx_status;
    uint32_t dmamac_cntl;
    uint32_t dmamac_addr;
    uint32_t dmamac_next;
} __ALIGNED(64) dw_dmadescr_t;
// clang-format on

namespace eth {

class DWMacDevice : public ddk::Device<DWMacDevice, ddk::Unbindable>,
                    public ddk::EthernetImplProtocol<DWMacDevice, ddk::base_protocol>,
                    public ddk::EthMacProtocol<DWMacDevice> {
 public:
  DWMacDevice(zx_device_t* device, pdev_protocol_t* pdev, eth_board_protocol_t* eth_board);

  static zx_status_t Create(void* ctx, zx_device_t* device);

  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

  // ZX_PROTOCOL_ETHERNET_IMPL ops.
  zx_status_t EthernetImplQuery(uint32_t options, ethernet_info_t* info);
  void EthernetImplStop() __TA_EXCLUDES(lock_);
  zx_status_t EthernetImplStart(const ethernet_ifc_protocol_t* ifc) __TA_EXCLUDES(lock_);
  void EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                           ethernet_impl_queue_tx_callback completion_cb, void* cookie)
      __TA_EXCLUDES(lock_);
  zx_status_t EthernetImplSetParam(uint32_t param, int32_t value, const void* data,
                                   size_t data_size);
  void EthernetImplGetBti(zx::bti* bti);

  // ZX_PROTOCOL_ETH_MAC ops.
  zx_status_t EthMacMdioWrite(uint32_t reg, uint32_t val);
  zx_status_t EthMacMdioRead(uint32_t reg, uint32_t* val);
  zx_status_t EthMacRegisterCallbacks(const eth_mac_callbacks_t* callbacks);

 private:
  zx_status_t InitBuffers();
  zx_status_t InitDevice();
  zx_status_t DeInitDevice() __TA_REQUIRES(lock_);
  zx_status_t InitPdev();
  zx_status_t ShutDown() __TA_EXCLUDES(lock_);

  void UpdateLinkStatus() __TA_REQUIRES(lock_);
  void DumpRegisters();
  void DumpStatus(uint32_t status);
  void ReleaseBuffers();
  void ProcRxBuffer(uint32_t int_status) __TA_EXCLUDES(lock_);
  uint32_t DmaRxStatus();

  int Thread() __TA_EXCLUDES(lock_);
  int WorkerThread();

  zx_status_t GetMAC(zx_device_t* dev);

  // Number each of tx/rx transaction descriptors
  //  4096 buffers = ~48ms of packets
  static constexpr uint32_t kNumDesc = 4096;
  // Size of each transaction buffer
  static constexpr uint32_t kTxnBufSize = 4096;

  dw_dmadescr_t* tx_descriptors_ = nullptr;
  dw_dmadescr_t* rx_descriptors_ = nullptr;

  fbl::RefPtr<PinnedBuffer> txn_buffer_;
  fbl::RefPtr<PinnedBuffer> desc_buffer_;

  uint8_t* tx_buffer_ = nullptr;
  uint32_t curr_tx_buf_ = 0;
  uint8_t* rx_buffer_ = nullptr;
  uint32_t curr_rx_buf_ = 0;

  // designware mac options
  uint32_t options_ = 0;

  // ethermac fields
  uint32_t features_ = 0;
  uint32_t mtu_ = 0;
  uint8_t mac_[MAC_ARRAY_LENGTH] = {};
  uint16_t mii_addr_ = 0;

  zx::bti bti_;
  zx::interrupt dma_irq_;

  ddk::PDev pdev_;
  ddk::EthBoardProtocolClient eth_board_;

  std::optional<ddk::MmioBuffer> mmio_;

  fbl::Mutex lock_;
  ddk::EthernetIfcProtocolClient ethernet_client_ __TA_GUARDED(lock_);

  // Only accessed from Thread, so not locked.
  bool online_ = false;

  // statistics
  uint32_t bus_errors_;
  uint32_t tx_counter_ = 0;
  uint32_t rx_packet_ = 0;
  uint32_t loop_count_ = 0;

  std::atomic<bool> running_;

  thrd_t thread_;
  thrd_t worker_thread_;

  // PHY callbacks.
  eth_mac_callbacks_t cbs_;

  // Callbacks registered signal.
  sync_completion_t cb_registered_signal_;
};

}  // namespace eth

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_DWMAC_DWMAC_H_
