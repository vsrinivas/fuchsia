// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdint.h>
#include <magenta/listnode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#if _KERNEL
//TODO: proper includes/defines kernel driver
#else
// includes and defines for userspace driver
#include <magenta/types.h>
#include <magenta/syscalls.h>
#include <ddk/driver.h>
typedef int status_t;
#define __nanosleep(x) mx_nanosleep(mx_deadline_after(x));
#define REG32(addr) ((volatile uint32_t *)(uintptr_t)(addr))
#define writel(v, a) (*REG32(eth->iobase + (a)) = (v))
#define readl(a) (*REG32(eth->iobase + (a)))
#endif

#include "ie.h"

void eth_dump_regs(ethdev_t* eth) {
    printf("STAT %08x CTRL %08x EXT %08x IMS %08x\n",
           readl(IE_STATUS), readl(IE_CTRL), readl(IE_CTRL_EXT), readl(IE_IMS));
    printf("RCTL %08x RDLN %08x RDH %08x RDT %08x\n",
           readl(IE_RCTL), readl(IE_RDLEN), readl(IE_RDH), readl(IE_RDT));
    printf("RXDC %08x RDTR %08x RBH %08x RBL %08x\n",
           readl(IE_RXDCTL), readl(IE_RDTR), readl(IE_RDBAH), readl(IE_RDBAL));
    printf("TCTL %08x TDLN %08x TDH %08x TDT %08x\n",
           readl(IE_TCTL), readl(IE_TDLEN), readl(IE_TDH), readl(IE_TDT));
    printf("TXDC %08x TIDV %08x TBH %08x TBL %08x\n",
           readl(IE_TXDCTL), readl(IE_TIDV), readl(IE_TDBAH), readl(IE_TDBAL));
}

unsigned eth_handle_irq(ethdev_t* eth) {
    // clears irqs on read
    return readl(IE_ICR);
}

bool eth_status_online(ethdev_t* eth) {
    return readl(IE_STATUS) & IE_STATUS_LU;
}

status_t eth_rx(ethdev_t* eth, void** data, size_t* len) {
    uint32_t n = eth->rx_rd_ptr;
    uint64_t info = eth->rxd[n].info;

    if (!(info & IE_RXD_DONE)) {
        return MX_ERR_SHOULD_WAIT;
    }

    // copy out packet
    mx_status_t r = IE_RXD_LEN(info);

    *data = eth->rxb + ETH_RXBUF_SIZE * n;
    *len = r;

    return MX_OK;
}

void eth_rx_ack(ethdev_t* eth) {
    uint32_t n = eth->rx_rd_ptr;

    // make buffer available to hw
    eth->rxd[n].info = 0;
    writel(n, IE_RDT);
    n = (n + 1) & (ETH_RXBUF_COUNT - 1);
    eth->rx_rd_ptr = n;
}

status_t eth_tx(ethdev_t* eth, const void* data, size_t len) {
    if ((len < 60) || (len > ETH_TXBUF_DSIZE)) {
        return MX_ERR_INVALID_ARGS;
    }

    mx_status_t status = MX_OK;

    mtx_lock(&eth->send_lock);

    // reclaim completed buffers from hw
    uint32_t n = eth->tx_rd_ptr;
    for (;;) {
        uint64_t info = eth->txd[n].info;
        if (!(info & IE_TXD_DONE)) {
            break;
        }
        framebuf_t* frame = list_remove_head_type(&eth->busy_frames, framebuf_t, node);
        if (frame == NULL) {
            panic();
        }
        // TODO: verify that this is the matching buffer to txd[n] addr?
        list_add_tail(&eth->free_frames, &frame->node);
        eth->txd[n].info = 0;
        n = (n + 1) & (ETH_TXBUF_COUNT - 1);
    }
    eth->tx_rd_ptr = n;

    // obtain buffer, copy into it, setup descriptor
    framebuf_t *frame = list_remove_head_type(&eth->free_frames, framebuf_t, node);
    if (frame == NULL) {
        status = MX_ERR_NO_MEMORY;
        goto out;
    }

    n = eth->tx_wr_ptr;
    memcpy(frame->data, data, len);
    eth->txd[n].addr = frame->phys;
    eth->txd[n].info = IE_TXD_LEN(len) | IE_TXD_EOP | IE_TXD_IFCS | IE_TXD_RS;
    list_add_tail(&eth->busy_frames, &frame->node);

    // inform hw of buffer availability
    n = (n + 1) & (ETH_TXBUF_COUNT - 1);
    eth->tx_wr_ptr = n;
    writel(n, IE_TDT);

out:
    mtx_unlock(&eth->send_lock);
    return status;
}

status_t eth_reset_hw(ethdev_t* eth) {
    // TODO: don't rely on bootloader having initialized the
    // controller in order to obtain the mac address
    uint32_t n;
    n = readl(IE_RAL(0));
    memcpy(eth->mac + 0, &n, 4);
    n = readl(IE_RAH(0));
    memcpy(eth->mac + 4, &n, 2);
    printf("eth: mac: %02x:%02x:%02x:%02x:%02x:%02x\n",
           eth->mac[0],eth->mac[1],eth->mac[2],
           eth->mac[3],eth->mac[4],eth->mac[5]);

    writel(IE_CTRL_RST, IE_CTRL);
    __nanosleep(5000);

    if (readl(IE_CTRL) & IE_CTRL_RST) {
        printf("eth: reset failed\n");
        return MX_ERR_BAD_STATE;
    }

    writel(IE_CTRL_ASDE | IE_CTRL_SLU, IE_CTRL);
    return MX_OK;
}

void eth_init_hw(ethdev_t* eth) {
    //TODO: tune RXDCTL and TXDCTL settings
    //TODO: TCTL COLD should be based on link state
    //TODO: use address filtering for multicast

    // setup rx ring
    eth->rx_rd_ptr = 0;
    writel(0, IE_RXCSUM);
    writel((4 << 0) | (1 << 8) | (1 << 16) | (1 << 24), IE_RXDCTL);
    writel(eth->rxd_phys, IE_RDBAL);
    writel(eth->rxd_phys >> 32, IE_RDBAH);
    writel(ETH_RXBUF_COUNT * 16, IE_RDLEN);
    writel(ETH_RXBUF_COUNT - 1, IE_RDT);
    writel(IE_RCTL_BSIZE2048 | IE_RCTL_DPF | IE_RCTL_SECRC | IE_RCTL_BAM | IE_RCTL_MPE | IE_RCTL_EN, IE_RCTL);

    // setup tx ring
    eth->tx_wr_ptr = 0;
    eth->tx_rd_ptr = 0;
    writel((4 << 0) | (1 << 8) | (1 << 16) | (1 << 24), IE_TXDCTL);
    writel(eth->txd_phys, IE_TDBAL);
    writel(eth->txd_phys >> 32, IE_TDBAH);
    writel(ETH_TXBUF_COUNT * 16, IE_TDLEN);
    writel(IE_TCTL_CT(15) | IE_TCTL_COLD_FD | IE_TCTL_EN, IE_TCTL);

    // disable all irqs (write to "clear" mask)
    writel(0xFFFF, IE_IMC);
    // enable rx irq (write to "set" mask)
    writel(IE_INT_RXT0, IE_IMS);
    // enable link status change irq
    writel(IE_INT_LSC, IE_IMS);
}

void eth_setup_buffers(ethdev_t* eth, void* iomem, mx_paddr_t iophys) {
    printf("eth: iomem @%p (phys %" PRIxPTR ")\n", iomem, iophys);

    list_initialize(&eth->free_frames);
    list_initialize(&eth->busy_frames);

    eth->rxd = iomem;
    eth->rxd_phys = iophys;
    iomem += ETH_DRING_SIZE;
    iophys += ETH_DRING_SIZE;
    memset(eth->rxd, 0, ETH_DRING_SIZE);

    eth->txd = iomem;
    eth->txd_phys = iophys;
    iomem += ETH_DRING_SIZE;
    iophys += ETH_DRING_SIZE;
    memset(eth->txd, 0, ETH_DRING_SIZE);

    eth->rxb = iomem;
    eth->rxb_phys = iophys;
    iomem += ETH_RXBUF_SIZE * ETH_RXBUF_COUNT;
    iophys += ETH_RXBUF_SIZE * ETH_RXBUF_COUNT;

    for (int n = 0; n < ETH_RXBUF_COUNT; n++) {
        eth->rxd[n].addr = eth->rxb_phys + ETH_RXBUF_SIZE * n;
    }
    for (int n = 0; n < ETH_TXBUF_COUNT - 1; n++) {
        framebuf_t *txb = iomem;
        txb->phys = iophys + ETH_TXBUF_HSIZE;
        txb->size = ETH_TXBUF_SIZE - ETH_TXBUF_HSIZE;
        txb->data = iomem + ETH_TXBUF_HSIZE;
        list_add_tail(&eth->free_frames, &txb->node);

        iomem += ETH_TXBUF_SIZE;
        iophys += ETH_TXBUF_SIZE;
    }
}
