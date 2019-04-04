// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <ddk/device.h>
#include <ddk/protocol/test.h>
#include <ddktl/device.h>
#include <lib/mmio/mmio.h>
#include <ddktl/pdev.h>
#include <ddktl/protocol/ethernet.h>
#include <ddktl/protocol/ethernet/board.h>
#include <ddktl/protocol/ethernet/mac.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <lib/sync/completion.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/vmo.h>
#include <optional>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "pinned-buffer.h"

// clang-format off
typedef volatile struct dw_mac_regs {
    uint32_t conf;            /*0 0x00 */
    uint32_t framefilt;       /*1 0x04 */
    uint32_t hashtablehigh;   /*2 0x08 */
    uint32_t hashtablelow;    /*3 0x0c */
    uint32_t miiaddr;         /*4 0x10 */
    uint32_t miidata;         /*5 0x14 */
    uint32_t flowcontrol;     /*6 0x18 */
    uint32_t vlantag;         /*7 0x1c */
    uint32_t version;         /*8 0x20 */
    uint32_t  reserved_1[5];  /*9 - 13 */
    uint32_t intreg;          /*14 0x38 */
    uint32_t intmask;         /*15 0x3c */
    uint32_t macaddr0hi;      /*16 0x40 */
    uint32_t macaddr0lo;      /*17 0x44 */
    uint32_t macaddr1hi;      /*18 0x48 */
    uint32_t macaddr1lo;      /*19 0x4c */
    uint32_t reserved_2[34];  /*18 - 53 */
    uint32_t rgmiistatus;     /*54 0xd8 */
} __PACKED dw_mac_regs_t;

// Offset of DMA regs into dwmac register block
#define DW_DMA_BASE_OFFSET    (0x1000)

typedef volatile struct dw_dma_regs {
    uint32_t busmode;              /*0  0x00 */
    uint32_t txpolldemand;         /*1  0x04 */
    uint32_t rxpolldemand;         /*2  0x08 */
    uint32_t rxdesclistaddr;       /*3  0x0c */
    uint32_t txdesclistaddr;       /*4  0x10 */
    uint32_t status;               /*5  0x14 */
    uint32_t opmode;               /*6  0x18 */
    uint32_t intenable;            /*7  0x1c */
    uint32_t missedframes;         /*8  0x20 */
    uint32_t rxwdt;                /*9  0x24 */
    uint32_t axibusmode;           /*10 0x28 */
    uint32_t axistatus;            /*11 0x2c */
    uint32_t reserved[6];
    uint32_t currhosttxdesc;       /*18 0x48 */
    uint32_t currhostrxdesc;       /*19 0x4c */
    uint32_t currhosttxbuffaddr;   /*20 0x50 */
    uint32_t currhostrxbuffaddr;   /*21 0x54 */
    uint32_t hwfeature;            /*22 0x58 */
} __PACKED dw_dma_regs_t;

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
                    public ddk::EthmacProtocol<DWMacDevice, ddk::base_protocol>,
                    public ddk::EthMacProtocol<DWMacDevice> {
public:
    DWMacDevice(zx_device_t* device);

    static zx_status_t Create(zx_device_t* device);

    void DdkRelease();
    void DdkUnbind();

    // ZX_PROTOCOL_ETHMAC ops.
    zx_status_t EthmacQuery(uint32_t options, ethmac_info_t* info);
    void EthmacStop() __TA_EXCLUDES(lock_);
    zx_status_t EthmacStart(const ethmac_ifc_protocol_t* ifc) __TA_EXCLUDES(lock_);
    zx_status_t EthmacQueueTx(uint32_t options, ethmac_netbuf_t* netbuf) __TA_EXCLUDES(lock_);
    zx_status_t EthmacSetParam(uint32_t param, int32_t value, const void* data, size_t data_size);
    void EthmacGetBti(zx::bti* bti);

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
    void ReleaseBuffers();
    void ProcRxBuffer(uint32_t int_status) __TA_EXCLUDES(lock_);
    uint32_t DmaRxStatus();

    int Thread() __TA_EXCLUDES(lock_);
    int WorkerThread();

    zx_status_t GetMAC(zx_device_t* dev);

    //Number each of tx/rx transaction descriptors
    static constexpr uint32_t kNumDesc = 32;
    //Size of each transaction buffer
    static constexpr uint32_t kTxnBufSize = 2048;

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

    std::optional<ddk::MmioBuffer> dwmac_regs_iobuff_;

    dw_mac_regs_t* dwmac_regs_ = nullptr;
    dw_dma_regs_t* dwdma_regs_ = nullptr;

    fbl::Mutex lock_;
    ddk::EthmacIfcProtocolClient ethmac_client_ __TA_GUARDED(lock_);

    // Only accessed from Thread, so not locked.
    bool online_ = false;

    //statistics
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

} // namespace eth
