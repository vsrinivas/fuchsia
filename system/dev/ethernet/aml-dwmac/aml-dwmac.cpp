// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/protocol/platform-device.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/type_support.h>
#include <hw/arch_ops.h>
#include <hw/reg.h>
#include <lib/fzl/vmar-manager.h>
#include <soc/aml-s912/s912-hw.h>
#include <zircon/compiler.h>

#include <stdio.h>
#include <string.h>

#include "aml-dwmac.h"
#include "dw-gmac-dma.h"

namespace eth {

enum {
    PHY_RESET,
    PHY_INTR,
};

template <typename T, typename U>
static inline T* offset_ptr(U* ptr, size_t offset) {
    return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(ptr) + offset);
}

int AmlDWMacDevice::Thread() {
    zxlogf(INFO, "AmLogic ethmac started\n");

    zx_status_t status;
    while (true) {
        status = dma_irq_.wait(nullptr);
        if (!running_.load()) {
            status = ZX_OK;
            break;
        }
        if (status != ZX_OK) {
            zxlogf(ERROR, "aml-dwmac: Interrupt error\n");
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
            zxlogf(ERROR, "aml-dwmac: abnormal interrupt %08x\n", stat);
        }
    }
    return status;
}

void AmlDWMacDevice::UpdateLinkStatus() {
    bool temp = dwmac_regs_->rgmiistatus & GMAC_RGMII_STATUS_LNKSTS;
    if (temp != online_) {
        online_ = temp;
        if (ethmac_proxy_ != nullptr) {
            ethmac_proxy_->Status(online_ ? ETH_STATUS_ONLINE : 0u);
        } else {
            zxlogf(ERROR, "aml-dwmac: System not ready\n");
        }
    }
    if (online_) {
        dwmac_regs_->conf |= GMAC_CONF_TE | GMAC_CONF_RE;
    } else {
        dwmac_regs_->conf &= ~(GMAC_CONF_TE | GMAC_CONF_RE);
    }
    zxlogf(INFO, "aml-dwmac: Link is now %s\n", online_ ? "up" : "down");
}

zx_status_t AmlDWMacDevice::InitPdev() {

    zx_status_t status = device_get_protocol(parent_,
                                             ZX_PROTOCOL_PLATFORM_DEV,
                                             &pdev_);
    if (status != ZX_OK) {
        return status;
    }

    status = device_get_protocol(parent_, ZX_PROTOCOL_GPIO, &gpio_);
    if (status != ZX_OK) {
        return status;
    }

    gpio_config(&gpio_, PHY_RESET, GPIO_DIR_OUT);
    gpio_write(&gpio_, PHY_RESET, 0);

    // Map amlogic peripheral control registers
    status = pdev_map_mmio_buffer(&pdev_, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &periph_regs_iobuff_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-dwmac: could not map periph mmio: %d\n", status);
        return status;
    }

    // Map mac control registers and dma control registers
    status = pdev_map_mmio_buffer(&pdev_, 1, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &dwmac_regs_iobuff_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-dwmac: could not map dwmac mmio: %d\n", status);
        return status;
    }
    dwmac_regs_ = static_cast<dw_mac_regs_t*>(io_buffer_virt(&dwmac_regs_iobuff_));
    dwdma_regs_ = offset_ptr<dw_dma_regs_t>(dwmac_regs_, DW_DMA_BASE_OFFSET);

    // Map HHI regs (clocks and power domains)
    status = pdev_map_mmio_buffer(&pdev_, 2, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &hhi_regs_iobuff_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-dwmac: could not map hiu mmio: %d\n", status);
        return status;
    }

    // Map dma interrupt
    status = pdev_map_interrupt(&pdev_, 0, dma_irq_.reset_and_get_address());
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-dwmac: could not map dma interrupt\n");
        return status;
    }

    // Get our bti
    status = pdev_get_bti(&pdev_, 0, bti_.reset_and_get_address());
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-dwmac: could not obtain bti: %d\n", status);
        return status;
    }

    return status;
}

void AmlDWMacDevice::ResetPhy() {
    gpio_write(&gpio_, PHY_RESET, 0);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
    gpio_write(&gpio_, PHY_RESET, 1);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
}

void AmlDWMacDevice::ConfigPhy() {
    uint32_t val;

    // Fix txdelay issuee for rtl8211.  When a hw reset is performed
    //  on the phy, it defaults to having an extra delay in the TXD path.
    //  Since we reset the phy, this needs to be corrected.
    MDIOWrite(MII_EPAGSR, 0xd08);
    MDIORead(0x11, &val);
    val &= ~0x100;
    MDIOWrite(0x11, val);
    MDIOWrite(MII_EPAGSR, 0x00);

    //Enable GigE advertisement
    MDIOWrite(MII_GBCR, 1 << 9);

    //Restart advertisements
    MDIORead(MII_BMCR, &val);
    val |= BMCR_ANENABLE | BMCR_ANRESTART;
    val &= ~BMCR_ISOLATE;
    MDIOWrite(MII_BMCR, val);
}

zx_status_t AmlDWMacDevice::Create(zx_device_t* device) {

    auto mac_device = fbl::make_unique<AmlDWMacDevice>(device);

    zx_status_t status = mac_device->InitPdev();
    if (status != ZX_OK) {
        return status;
    }

    // Initialize AMLogic peripheral registers associated with dwmac
    void* pregs = io_buffer_virt(&mac_device->periph_regs_iobuff_);
    //Sorry about the magic...rtfm
    writel(0x1621, offset_ptr<uint32_t>(pregs, PER_ETH_REG0));
    writel(0x20000, offset_ptr<uint32_t>(pregs, PER_ETH_REG1));

    writel(REG2_ETH_REG2_REVERSED | REG2_INTERNAL_PHY_ID,
           offset_ptr<uint32_t>(pregs, PER_ETH_REG2));

    writel(REG3_CLK_IN_EN    | REG3_ETH_REG3_19_RESVERD |
           REG3_CFG_PHY_ADDR | REG3_CFG_MODE |
           REG3_CFG_EN_HIGH  | REG3_ETH_REG3_2_RESERVED,
           offset_ptr<uint32_t>(pregs, PER_ETH_REG3));

    // Enable clocks and power domain for dwmac
    void* hregs = io_buffer_virt(&mac_device->hhi_regs_iobuff_);
    set_bitsl(1 << 3, offset_ptr<uint32_t>(hregs, HHI_GCLK_MPEG1));
    clr_bitsl((1 << 3) | (1 << 2), offset_ptr<uint32_t>(hregs, HHI_MEM_PD_REG0));

    // Save the mac address, the reset below will clear out this register
    //  This is temporary until we get mac address from platform
    uint32_t tempmachi = mac_device->dwmac_regs_->macaddr0hi;
    uint32_t tempmaclo = mac_device->dwmac_regs_->macaddr0lo;

    //Reset the dma peripheral
    mac_device->dwdma_regs_->busmode |= DMAMAC_SRST;
    uint32_t loop_count = 10;
    do {
        zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
        loop_count--;
    } while ((mac_device->dwdma_regs_->busmode & DMAMAC_SRST) && loop_count);
    if (!loop_count) {
        return ZX_ERR_TIMED_OUT;
    }

    mac_device->dwmac_regs_->macaddr0hi = tempmachi;
    mac_device->dwmac_regs_->macaddr0lo = tempmaclo;

    auto cleanup = fbl::MakeAutoCall([&]() { mac_device->ShutDown(); });

    status = mac_device->InitBuffers();
    if (status != ZX_OK)
        return status;

    //reset the phy
    mac_device->ResetPhy();
    //configure phy
    mac_device->ConfigPhy();

    mac_device->InitDevice();

    auto thunk = [](void* arg) -> int { return reinterpret_cast<AmlDWMacDevice*>(arg)->Thread(); };

    mac_device->running_.store(true);
    int ret = thrd_create_with_name(&mac_device->thread_, thunk,
                                    reinterpret_cast<void*>(mac_device.get()),
                                    "amlmac-thread");
    ZX_DEBUG_ASSERT(ret == thrd_success);

    status = mac_device->DdkAdd("AmLogic dwMac");
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-dwmac: Could not create eth device: %d\n", status);
        return status;
    } else {
        zxlogf(INFO, "aml-dwmac: Added AmLogic dwMac device\n");
    }

    cleanup.cancel();

    // mac_device intentionally leaked as it is now held by DevMgr
    __UNUSED auto ptr = mac_device.release();
    return ZX_OK;
}

zx_status_t AmlDWMacDevice::InitBuffers() {

    fbl::RefPtr<fzl::VmarManager> vmar_mgr;

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

        desc_buffer_->LookupPhys((((i + 1) % kNumDesc) + kNumDesc) * sizeof(dw_dmadescr_t), &tmpaddr);
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

zx_handle_t AmlDWMacDevice::EthmacGetBti() {

    return bti_.get();
}

zx_status_t AmlDWMacDevice::MDIOWrite(uint32_t reg, uint32_t val) {

    dwmac_regs_->miidata = val;

    uint32_t miiaddr = (mii_addr_ << MIIADDRSHIFT) |
                       (reg       << MIIREGSHIFT)  |
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

zx_status_t AmlDWMacDevice::MDIORead(uint32_t reg, uint32_t* val) {

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

AmlDWMacDevice::AmlDWMacDevice(zx_device_t* device)
    : ddk::Device<AmlDWMacDevice, ddk::Unbindable>(device) {
}

void AmlDWMacDevice::ReleaseBuffers() {
    io_buffer_release(&periph_regs_iobuff_);
    io_buffer_release(&hhi_regs_iobuff_);
    io_buffer_release(&dwmac_regs_iobuff_);
    //Unpin the memory used for the dma buffers
    if (txn_buffer_->UnPin() != ZX_OK) {
        zxlogf(ERROR, "aml_dwmac: Error unpinning transaction buffers\n");
    }
    if (desc_buffer_->UnPin() != ZX_OK) {
        zxlogf(ERROR, "aml-dwmac: Error unpinning description buffers\n");
    }
}

void AmlDWMacDevice::DdkRelease() {
    zxlogf(INFO, "AmLogic Ethmac release...\n");
    delete this;
}

void AmlDWMacDevice::DdkUnbind() {
    zxlogf(INFO, "AmLogic Ethmac DdkUnbind\n");
    ShutDown();
    DdkRemove();
}

zx_status_t AmlDWMacDevice::ShutDown() {
    running_.store(false);
    dma_irq_.destroy();
    thrd_join(thread_, NULL);
    fbl::AutoLock lock(&lock_);
    online_ = false;
    ethmac_proxy_.reset();
    DeInitDevice();
    ReleaseBuffers();

    return ZX_OK;
}

zx_status_t AmlDWMacDevice::GetMAC(uint8_t* addr) {
    // look for MAC address device metadata
    // metadata is padded so we need buffer size > 6 bytes
    uint8_t buffer[16];
    size_t actual;
    zx_status_t status = device_get_metadata(zxdev(), DEVICE_METADATA_MAC_ADDRESS, buffer,
                                             sizeof(buffer), &actual);
    if (status == ZX_OK && actual >= 6) {
        zxlogf(INFO, "aml_dwmac: MAC address %02x:%02x:%02x:%02x:%02x:%02x\n",
               buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);
        memcpy(addr, buffer, 6);
        return ZX_OK;
    }

    // else read MAC address from hardware register
    uint32_t hi = dwmac_regs_->macaddr0hi;
    uint32_t lo = dwmac_regs_->macaddr0lo;

    /* Extract the MAC address from the high and low words */
    addr[0] = static_cast<uint8_t>(lo & 0xff);
    addr[1] = static_cast<uint8_t>((lo >> 8) & 0xff);
    addr[2] = static_cast<uint8_t>((lo >> 16) & 0xff);
    addr[3] = static_cast<uint8_t>((lo >> 24) & 0xff);
    addr[4] = static_cast<uint8_t>(hi & 0xff);
    addr[5] = static_cast<uint8_t>((hi >> 8) & 0xff);
    return ZX_OK;
}

zx_status_t AmlDWMacDevice::EthmacQuery(uint32_t options, ethmac_info_t* info) {
    memset(info, 0, sizeof(*info));
    info->features = ETHMAC_FEATURE_DMA;
    info->mtu = 1500;
    GetMAC(info->mac);
    return ZX_OK;
}

void AmlDWMacDevice::EthmacStop() {
    zxlogf(INFO, "Stopping AmLogic Ethermac\n");
    fbl::AutoLock lock(&lock_);
    ethmac_proxy_.reset();
}

zx_status_t AmlDWMacDevice::EthmacStart(fbl::unique_ptr<ddk::EthmacIfcProxy> proxy) {
    fbl::AutoLock lock(&lock_);

    if (ethmac_proxy_ != nullptr) {
        zxlogf(ERROR, "aml_dwmac:  Already bound!!!");
        return ZX_ERR_ALREADY_BOUND;
    } else {
        ethmac_proxy_ = fbl::move(proxy);
        UpdateLinkStatus();
        zxlogf(INFO, "aml_dwmac: Started\n");
    }
    return ZX_OK;
}

zx_status_t AmlDWMacDevice::InitDevice() {

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

zx_status_t AmlDWMacDevice::DeInitDevice() {
    //Disable Interrupts
    dwdma_regs_->intenable = 0;
    //Disable Transmit and Receive
    dwmac_regs_->conf &= ~(GMAC_CONF_TE | GMAC_CONF_RE);

    //reset the phy (hold in reset)
    gpio_write(&gpio_, PHY_RESET, 0);

    //transmit and receive are not disables, safe to null descriptor list ptrs
    dwdma_regs_->txdesclistaddr = 0;
    dwdma_regs_->rxdesclistaddr = 0;

    return ZX_OK;
}

uint32_t AmlDWMacDevice::DmaRxStatus() {
    return (dwdma_regs_->status & DMA_STATUS_RS_MASK) >> DMA_STATUS_RS_POS;
}

void AmlDWMacDevice::ProcRxBuffer(uint32_t int_status) {
    while (true) {
        uint32_t pkt_stat = rx_descriptors_[curr_rx_buf_].txrx_status;

        if (pkt_stat & DESC_RXSTS_OWNBYDMA) {
            return;
        }
        size_t fr_len = (pkt_stat & DESC_RXSTS_FRMLENMSK) >> DESC_RXSTS_FRMLENSHFT;
        if (fr_len > kTxnBufSize) {
            zxlogf(ERROR, "aml-dwmac: unsupported packet size received\n");
            return;
        }

        uint8_t* temptr = &rx_buffer_[curr_rx_buf_ * kTxnBufSize];

        zx_cache_flush(temptr, kTxnBufSize, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);

        { // limit scope of autolock
            fbl::AutoLock lock(&lock_);
            if ((ethmac_proxy_ != nullptr)) {

                ethmac_proxy_->Recv(temptr, fr_len, 0);

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

zx_status_t AmlDWMacDevice::EthmacQueueTx(uint32_t options, ethmac_netbuf_t* netbuf) {

    { //Check to make sure we are ready to accept packets
        fbl::AutoLock lock(&lock_);
        if (!online_) {
            return ZX_ERR_UNAVAILABLE;
        }
    }

    if (netbuf->len > kTxnBufSize) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (tx_descriptors_[curr_tx_buf_].txrx_status & DESC_TXSTS_OWNBYDMA) {
        zxlogf(ERROR, "TX buffer overrun@ %u\n", curr_tx_buf_);
        return ZX_ERR_UNAVAILABLE;
    }
    uint8_t* temptr = &tx_buffer_[curr_tx_buf_ * kTxnBufSize];

    memcpy(temptr, netbuf->data, netbuf->len);
    hw_mb();

    zx_cache_flush(temptr, netbuf->len, ZX_CACHE_FLUSH_DATA);

    //Descriptors are pre-iniitialized with the paddr of their corresponding
    // buffers, only need to setup the control and status fields.
    tx_descriptors_[curr_tx_buf_].dmamac_cntl =
        DESC_TXCTRL_TXINT |
        DESC_TXCTRL_TXLAST |
        DESC_TXCTRL_TXFIRST |
        DESC_TXCTRL_TXCHAIN |
        (netbuf->len & DESC_TXCTRL_SIZE1MASK);

    tx_descriptors_[curr_tx_buf_].txrx_status = DESC_TXSTS_OWNBYDMA;
    curr_tx_buf_ = (curr_tx_buf_ + 1) % kNumDesc;

    hw_mb();
    dwdma_regs_->txpolldemand = ~0;
    tx_counter_++;
    return ZX_OK;
}

zx_status_t AmlDWMacDevice::EthmacSetParam(uint32_t param, int32_t value, void* data) {
    zxlogf(INFO, "SetParam called  %x  %x\n", param, value);
    return ZX_OK;
}

} // namespace eth

extern "C" zx_status_t aml_eth_bind(void* ctx, zx_device_t* device, void** cookie) {
    return eth::AmlDWMacDevice::Create(device);
}
