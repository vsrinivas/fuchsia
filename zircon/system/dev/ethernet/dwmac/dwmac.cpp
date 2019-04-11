// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dwmac.h"
#include "dw-gmac-dma.h"
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/ethernet/mac.h>
#include <ddk/protocol/platform-device-lib.h>
#include <ddk/protocol/platform/device.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <hw/arch_ops.h>
#include <hw/reg.h>
#include <lib/fzl/vmar-manager.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/compiler.h>

namespace eth {

namespace {

enum {
    COMPONENT_PDEV,
    COMPONENT_ETH_BOARD,
    COMPONENT_COUNT,
};

// MMIO Indexes.
constexpr uint32_t kEthMacMmio = 0;

} // namespace

template <typename T, typename U>
static inline T* offset_ptr(U* ptr, size_t offset) {
    return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(ptr) + offset);
}

int DWMacDevice::Thread() {
    zxlogf(INFO, "ethmac started\n");

    zx_status_t status;
    while (true) {
        status = dma_irq_.wait(nullptr);
        if (!running_.load()) {
            status = ZX_OK;
            break;
        }
        if (status != ZX_OK) {
            zxlogf(ERROR, "dwmac: Interrupt error\n");
            break;
        }
        uint32_t stat = dwdma_regs_->status;
        dwdma_regs_->status = stat;

        if (stat & DMA_STATUS_GLI) {
            fbl::AutoLock lock(&lock_); //Note: limited scope of autolock
            UpdateLinkStatus();
        }
        if (stat & DMA_STATUS_RI) {
            ProcRxBuffer(stat);
        }
        if (stat & DMA_STATUS_AIS) {
            bus_errors_++;
            zxlogf(ERROR, "dwmac: abnormal interrupt %08x\n", stat);
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
    int ret = thrd_create_with_name(&thread_, thunk,
                                    this,
                                    "mac-thread");
    ZX_DEBUG_ASSERT(ret == thrd_success);

    zx_status_t status = DdkAdd("Designware MAC");
    if (status != ZX_OK) {
        zxlogf(ERROR, "dwmac: Could not create eth device: %d\n", status);
        return status;
    } else {
        zxlogf(INFO, "dwmac: Added dwMac device\n");
    }
    return status;
}

void DWMacDevice::UpdateLinkStatus() {
    bool temp = dwmac_regs_->rgmiistatus & GMAC_RGMII_STATUS_LNKSTS;
    if (temp != online_) {
        online_ = temp;
        if (ethmac_client_.is_valid()) {
            ethmac_client_.Status(online_ ? ETHMAC_STATUS_ONLINE : 0u);
        } else {
            zxlogf(ERROR, "dwmac: System not ready\n");
        }
    }
    if (online_) {
        dwmac_regs_->conf |= GMAC_CONF_TE | GMAC_CONF_RE;
    } else {
        dwmac_regs_->conf &= ~(GMAC_CONF_TE | GMAC_CONF_RE);
    }
    zxlogf(INFO, "dwmac: Link is now %s\n", online_ ? "up" : "down");
}

zx_status_t DWMacDevice::InitPdev() {
    // Map mac control registers and dma control registers.
    auto status = pdev_.MapMmio(kEthMacMmio, &dwmac_regs_iobuff_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "dwmac: could not map dwmac mmio: %d\n", status);
        return status;
    }

    dwmac_regs_ = static_cast<dw_mac_regs_t*>(dwmac_regs_iobuff_->get());
    dwdma_regs_ = offset_ptr<dw_dma_regs_t>(dwmac_regs_, DW_DMA_BASE_OFFSET);

    // Map dma interrupt.
    status = pdev_.GetInterrupt(0, &dma_irq_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "dwmac: could not map dma interrupt\n");
        return status;
    }

    // Get our bti.
    status = pdev_.GetBti(0, &bti_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "dwmac: could not obtain bti: %d\n", status);
        return status;
    }

    // Get ETH_BOARD protocol.
    if (!eth_board_.is_valid()) {
        zxlogf(ERROR, "dwmac: could not obtain ETH_BOARD protocol: %d\n", status);
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
        zxlogf(ERROR, "%s could not get ZX_PROTOCOL_COMPOSITE\n", __func__);
        return status;
    }

    zx_device_t* components[COMPONENT_COUNT];
    size_t actual;
    composite_get_components(&composite, components, COMPONENT_COUNT, &actual);
    if (actual != 2) {
        zxlogf(ERROR, "%s could not get components\n", __func__);
        return ZX_ERR_NOT_SUPPORTED;
    }

    status = device_get_protocol(components[COMPONENT_PDEV], ZX_PROTOCOL_PDEV, &pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s could not get ZX_PROTOCOL_PDEV\n", __func__);
        return status;
    }

    status = device_get_protocol(components[COMPONENT_ETH_BOARD], ZX_PROTOCOL_ETH_BOARD,
                                 &eth_board);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s could not get ZX_PROTOCOL_ETH_BOARD\n", __func__);
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
    mac_device->GetMAC(components[COMPONENT_PDEV]);

    // Reset the dma peripheral.
    mac_device->dwdma_regs_->busmode |= DMAMAC_SRST;
    uint32_t loop_count = 10;
    do {
        zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
        loop_count--;
    } while ((mac_device->dwdma_regs_->busmode & DMAMAC_SRST) && loop_count);
    if (!loop_count) {
        return ZX_ERR_TIMED_OUT;
    }

    // Mac address register was erased by the reset; set it!
    mac_device->dwmac_regs_->macaddr0hi = (mac_device->mac_[5] << 8) | (mac_device->mac_[4] << 0);
    mac_device->dwmac_regs_->macaddr0lo = (mac_device->mac_[3] << 24) | (mac_device->mac_[2] << 16)
    | (mac_device->mac_[1] << 8) | (mac_device->mac_[0] << 0);

    auto cleanup = fbl::MakeAutoCall([&]() { mac_device->ShutDown(); });

    status = mac_device->InitBuffers();
    if (status != ZX_OK)
        return status;

    sync_completion_reset(&mac_device->cb_registered_signal_);

    // Populate board specific information
    eth_dev_metadata_t phy_info;
    status = device_get_metadata(components[COMPONENT_PDEV], DEVICE_METADATA_ETH_PHY_DEVICE,
                                 &phy_info, sizeof(eth_dev_metadata_t), &actual);
    if (status != ZX_OK || actual != sizeof(eth_dev_metadata_t)) {
        zxlogf(ERROR, "dwmac: Could not get PHY metadata %d\n", status);
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
        zxlogf(ERROR, "dwmac: Could not create phy device: %d\n", status);

        return status;
    }

    auto worker_thunk = [](void* arg) -> int {
        return reinterpret_cast<DWMacDevice*>(arg)->WorkerThread();
    };

    int ret = thrd_create_with_name(&mac_device->worker_thread_, worker_thunk,
                                    reinterpret_cast<void*>(mac_device.get()),
                                    "mac-worker-thread");
    ZX_DEBUG_ASSERT(ret == thrd_success);

    cleanup.cancel();

    // mac_device intentionally leaked as it is now held by DevMgr.
    __UNUSED auto ptr = mac_device.release();
    return ZX_OK;
} // namespace eth

zx_status_t DWMacDevice::InitBuffers() {

    constexpr size_t kDescSize = ROUNDUP(2 * kNumDesc * sizeof(dw_dmadescr_t), PAGE_SIZE);

    constexpr size_t kBufSize = 2 * kNumDesc * kTxnBufSize;

    txn_buffer_ = PinnedBuffer::Create(kBufSize, bti_, ZX_CACHE_POLICY_CACHED);
    desc_buffer_ = PinnedBuffer::Create(kDescSize, bti_, ZX_CACHE_POLICY_UNCACHED);

    tx_buffer_ = static_cast<uint8_t*>(txn_buffer_->GetBaseAddress());
    zx_cache_flush(tx_buffer_, kBufSize, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
    //rx buffer right after tx
    rx_buffer_ = &tx_buffer_[kBufSize / 2];

    tx_descriptors_ = static_cast<dw_dmadescr_t*>(desc_buffer_->GetBaseAddress());
    //rx descriptors right after tx
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

        desc_buffer_->LookupPhys((((i + 1) % kNumDesc) + kNumDesc) * sizeof(dw_dmadescr_t),
                                 &tmpaddr);
        rx_descriptors_[i].dmamac_next = static_cast<uint32_t>(tmpaddr);

        txn_buffer_->LookupPhys((i + kNumDesc) * kTxnBufSize, &tmpaddr);
        rx_descriptors_[i].dmamac_addr = static_cast<uint32_t>(tmpaddr);
        rx_descriptors_[i].dmamac_cntl =
            (MAC_MAX_FRAME_SZ & DESC_RXCTRL_SIZE1MASK) |
            DESC_RXCTRL_RXCHAIN;

        rx_descriptors_[i].txrx_status = DESC_RXSTS_OWNBYDMA;
    }

    desc_buffer_->LookupPhys(0, &tmpaddr);
    dwdma_regs_->txdesclistaddr = static_cast<uint32_t>(tmpaddr);

    desc_buffer_->LookupPhys(kNumDesc * sizeof(dw_dmadescr_t), &tmpaddr);
    dwdma_regs_->rxdesclistaddr = static_cast<uint32_t>(tmpaddr);
    return ZX_OK;
}

void DWMacDevice::EthmacGetBti(zx::bti* bti) {
    bti_.duplicate(ZX_RIGHT_SAME_RIGHTS, bti);
}

zx_status_t DWMacDevice::EthMacMdioWrite(uint32_t reg, uint32_t val) {
    dwmac_regs_->miidata = val;

    uint32_t miiaddr = (mii_addr_ << MIIADDRSHIFT) |
                       (reg << MIIREGSHIFT) |
                       MII_WRITE;

    dwmac_regs_->miiaddr = miiaddr | MII_CLKRANGE_150_250M | MII_BUSY;

    zx_time_t deadline = zx_deadline_after(ZX_MSEC(3));
    do {
        if (!(dwmac_regs_->miiaddr & MII_BUSY)) {
            return ZX_OK;
        }
        zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
    } while (zx_clock_get_monotonic() < deadline);
    return ZX_ERR_TIMED_OUT;
}

zx_status_t DWMacDevice::EthMacMdioRead(uint32_t reg, uint32_t* val) {
    uint32_t miiaddr = (mii_addr_ << MIIADDRSHIFT) |
                       (reg << MIIREGSHIFT);

    dwmac_regs_->miiaddr = miiaddr | MII_CLKRANGE_150_250M | MII_BUSY;

    zx_time_t deadline = zx_deadline_after(ZX_MSEC(3));
    do {
        if (!(dwmac_regs_->miiaddr & MII_BUSY)) {
            *val = dwmac_regs_->miidata;
            return ZX_OK;
        }
        zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
    } while (zx_clock_get_monotonic() < deadline);
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
    //Unpin the memory used for the dma buffers
    if (txn_buffer_->UnPin() != ZX_OK) {
        zxlogf(ERROR, "dwmac: Error unpinning transaction buffers\n");
    }
    if (desc_buffer_->UnPin() != ZX_OK) {
        zxlogf(ERROR, "dwmac: Error unpinning description buffers\n");
    }
}

void DWMacDevice::DdkRelease() {
    zxlogf(INFO, "Ethmac release...\n");
    delete this;
}

void DWMacDevice::DdkUnbind() {
    zxlogf(INFO, "Ethmac DdkUnbind\n");
    ShutDown();
    DdkRemove();
}

zx_status_t DWMacDevice::ShutDown() {
    if (running_.load()) {
        running_.store(false);
        dma_irq_.destroy();
        thrd_join(thread_, NULL);
    }
    fbl::AutoLock lock(&lock_);
    online_ = false;
    ethmac_client_.clear();
    DeInitDevice();
    ReleaseBuffers();
    return ZX_OK;
}

zx_status_t DWMacDevice::GetMAC(zx_device_t* dev) {
    // look for MAC address device metadata
    // metadata is padded so we need buffer size > 6 bytes
    uint8_t buffer[16];
    size_t actual;
    zx_status_t status = device_get_metadata(dev, DEVICE_METADATA_MAC_ADDRESS, buffer,
                                             sizeof(buffer), &actual);
    if (status != ZX_OK || actual < 6) {
        zxlogf(ERROR, "dwmac: MAC address metadata load failed. Falling back on HW setting.");
        // read MAC address from hardware register
        uint32_t hi = dwmac_regs_->macaddr0hi;
        uint32_t lo = dwmac_regs_->macaddr0lo;

        /* Extract the MAC address from the high and low words */
        buffer[0] = static_cast<uint8_t>(lo & 0xff);
        buffer[1] = static_cast<uint8_t>((lo >> 8) & 0xff);
        buffer[2] = static_cast<uint8_t>((lo >> 16) & 0xff);
        buffer[3] = static_cast<uint8_t>((lo >> 24) & 0xff);
        buffer[4] = static_cast<uint8_t>(hi & 0xff);
        buffer[5] = static_cast<uint8_t>((hi >> 8) & 0xff);
    }

    zxlogf(INFO, "dwmac: MAC address %02x:%02x:%02x:%02x:%02x:%02x\n",
           buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);
    memcpy(mac_, buffer, sizeof mac_);
    return ZX_OK;
}

zx_status_t DWMacDevice::EthmacQuery(uint32_t options, ethmac_info_t* info) {
    memset(info, 0, sizeof(*info));
    info->features = ETHMAC_FEATURE_DMA;
    info->mtu = 1500;
    memcpy(info->mac, mac_, sizeof info->mac);
    info->netbuf_size = sizeof(ethmac_netbuf_t);
    return ZX_OK;
}

void DWMacDevice::EthmacStop() {
    zxlogf(INFO, "Stopping Ethermac\n");
    fbl::AutoLock lock(&lock_);
    ethmac_client_.clear();
}

zx_status_t DWMacDevice::EthmacStart(const ethmac_ifc_protocol_t* ifc) {
    fbl::AutoLock lock(&lock_);

    if (ethmac_client_.is_valid()) {
        zxlogf(ERROR, "dwmac:  Already bound!!!");
        return ZX_ERR_ALREADY_BOUND;
    } else {
        ethmac_client_ = ddk::EthmacIfcProtocolClient(ifc);
        UpdateLinkStatus();
        zxlogf(INFO, "dwmac: Started\n");
    }
    return ZX_OK;
}

zx_status_t DWMacDevice::InitDevice() {

    dwdma_regs_->intenable = 0;
    dwdma_regs_->busmode = X8PBL | DMA_PBL;

    dwdma_regs_->opmode = DMA_OPMODE_TSF | DMA_OPMODE_RSF;

    dwdma_regs_->opmode |= DMA_OPMODE_SR | DMA_OPMODE_ST; //start tx and rx

    //Clear all the interrupt flags
    dwdma_regs_->status = ~0;

    //Enable Interrupts
    dwdma_regs_->intenable = DMA_INT_NIE | DMA_INT_AIE | DMA_INT_FBE |
                             DMA_INT_RIE | DMA_INT_RUE | DMA_INT_OVE |
                             DMA_INT_UNE | DMA_INT_TSE | DMA_INT_RSE;

    dwmac_regs_->macaddr1lo = 0;
    dwmac_regs_->macaddr1hi = 0;
    dwmac_regs_->hashtablehigh = 0xffffffff;
    dwmac_regs_->hashtablelow = 0xffffffff;

    //TODO - configure filters
    zxlogf(INFO, "macaddr0hi = %08x\n", dwmac_regs_->macaddr0hi);
    zxlogf(INFO, "macaddr0lo = %08x\n", dwmac_regs_->macaddr0lo);

    dwmac_regs_->framefilt |= (1 << 10) | (1 << 4) | (1 << 0); //promiscuous

    dwmac_regs_->conf = GMAC_CORE_INIT;

    return ZX_OK;
}

zx_status_t DWMacDevice::DeInitDevice() {
    //Disable Interrupts
    dwdma_regs_->intenable = 0;
    //Disable Transmit and Receive
    dwmac_regs_->conf &= ~(GMAC_CONF_TE | GMAC_CONF_RE);

    //reset the phy (hold in reset)
    //gpio_write(&gpios_[PHY_RESET], 0);

    //transmit and receive are not disables, safe to null descriptor list ptrs
    dwdma_regs_->txdesclistaddr = 0;
    dwdma_regs_->rxdesclistaddr = 0;

    return ZX_OK;
}

uint32_t DWMacDevice::DmaRxStatus() {
    return (dwdma_regs_->status & DMA_STATUS_RS_MASK) >> DMA_STATUS_RS_POS;
}

void DWMacDevice::ProcRxBuffer(uint32_t int_status) {
    while (true) {
        uint32_t pkt_stat = rx_descriptors_[curr_rx_buf_].txrx_status;

        if (pkt_stat & DESC_RXSTS_OWNBYDMA) {
            return;
        }
        size_t fr_len = (pkt_stat & DESC_RXSTS_FRMLENMSK) >> DESC_RXSTS_FRMLENSHFT;
        if (fr_len > kTxnBufSize) {
            zxlogf(ERROR, "dwmac: unsupported packet size received\n");
            return;
        }

        uint8_t* temptr = &rx_buffer_[curr_rx_buf_ * kTxnBufSize];

        zx_cache_flush(temptr, kTxnBufSize, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);

        { // limit scope of autolock
            fbl::AutoLock lock(&lock_);
            if ((ethmac_client_.is_valid())) {

                ethmac_client_.Recv(temptr, fr_len, 0);

            } else {
                zxlogf(ERROR, "Dropping bad packet\n");
            }
        };

        rx_descriptors_[curr_rx_buf_].txrx_status = DESC_RXSTS_OWNBYDMA;
        rx_packet_++;

        curr_rx_buf_ = (curr_rx_buf_ + 1) % kNumDesc;
        if (curr_rx_buf_ == 0) {
            loop_count_++;
        }
        dwdma_regs_->rxpolldemand = ~0;
    }
}

zx_status_t DWMacDevice::EthmacQueueTx(uint32_t options, ethmac_netbuf_t* netbuf) {

    { //Check to make sure we are ready to accept packets
        fbl::AutoLock lock(&lock_);
        if (!online_) {
            return ZX_ERR_UNAVAILABLE;
        }
    }

    if (netbuf->data_size > kTxnBufSize) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (tx_descriptors_[curr_tx_buf_].txrx_status & DESC_TXSTS_OWNBYDMA) {
        zxlogf(ERROR, "TX buffer overrun@ %u\n", curr_tx_buf_);
        return ZX_ERR_UNAVAILABLE;
    }
    uint8_t* temptr = &tx_buffer_[curr_tx_buf_ * kTxnBufSize];

    memcpy(temptr, netbuf->data_buffer, netbuf->data_size);
    hw_mb();

    zx_cache_flush(temptr, netbuf->data_size, ZX_CACHE_FLUSH_DATA);

    // Descriptors are pre-iniitialized with the paddr of their corresponding
    // buffers, only need to setup the control and status fields.
    tx_descriptors_[curr_tx_buf_].dmamac_cntl =
        DESC_TXCTRL_TXINT |
        DESC_TXCTRL_TXLAST |
        DESC_TXCTRL_TXFIRST |
        DESC_TXCTRL_TXCHAIN |
        ((uint32_t)netbuf->data_size & DESC_TXCTRL_SIZE1MASK);

    tx_descriptors_[curr_tx_buf_].txrx_status = DESC_TXSTS_OWNBYDMA;
    curr_tx_buf_ = (curr_tx_buf_ + 1) % kNumDesc;

    hw_mb();
    dwdma_regs_->txpolldemand = ~0;
    tx_counter_++;
    return ZX_OK;
}

zx_status_t DWMacDevice::EthmacSetParam(uint32_t param, int32_t value, const void* data,
                                        size_t data_size) {
    zxlogf(INFO, "SetParam called  %x  %x\n", param, value);
    return ZX_OK;
}

static zx_driver_ops_t driver_ops = [](){
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = DWMacDevice::Create;
    return ops;
}();

} // namespace eth

// clang-format off
ZIRCON_DRIVER_BEGIN(dwmac, eth::driver_ops, "designware_mac", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_DESIGNWARE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_ETH_MAC),
ZIRCON_DRIVER_END(dwmac)
