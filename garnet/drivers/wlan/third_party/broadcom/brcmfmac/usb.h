/*
 * Copyright (c) 2011 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef BRCMFMAC_USB_H
#define BRCMFMAC_USB_H

#include <ddk/protocol/usb.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include "linuxisms.h"
#include "netbuf.h"

// ZX USB transfer requests use a pre-allocated buffer. This requires a copy for each transfer,
// and the max transfer size must be known in advance. 4K is bigger than any frame or
// firmware transfer this driver does.
// TODO(cphoenix): Double-check on control transfer sizes.
// TODO(cphoenix): When/if the USB driver gets more sophisticated, rework this for greater
//  efficiency.
#define USB_MAX_TRANSFER_SIZE (4096)

enum brcmf_usb_state {
    BRCMFMAC_USB_STATE_DOWN,
    BRCMFMAC_USB_STATE_DL_FAIL,
    BRCMFMAC_USB_STATE_DL_DONE,
    BRCMFMAC_USB_STATE_UP,
    BRCMFMAC_USB_STATE_SLEEP
};

struct brcmf_stats {
    uint32_t tx_ctlpkts;
    uint32_t tx_ctlerrs;
    uint32_t rx_ctlpkts;
    uint32_t rx_ctlerrs;
};

struct brcmf_usbdev {
    struct brcmf_bus* bus;
    struct brcmf_usbdev_info* devinfo;
    enum brcmf_usb_state state;
    struct brcmf_stats stats;
    int ntxq;
    int nrxq;
    int rxsize;
    uint32_t bus_mtu;
    int devid;
    int chiprev; /* chip revsion number */
};

struct brcmf_urb {
    usb_request_t* zxurb;
    void* context;
    struct brcmf_usbdev_info* devinfo;
    int actual_length;
    int desired_length;
    void* recv_buffer; // For control reads
    zx_status_t status;
};

/* IO Request Block (IRB) */
struct brcmf_usbreq {
    struct list_node list;
    struct brcmf_usbdev_info* devinfo;
    struct brcmf_urb* urb;
    struct brcmf_netbuf* netbuf;
};

#endif /* BRCMFMAC_USB_H */
