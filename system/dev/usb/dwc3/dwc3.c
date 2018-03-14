// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/usb-function.h>
#include <hw/reg.h>
#include <pretty/hexdump.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dwc3.h"
#include "dwc3-regs.h"
#include "dwc3-types.h"

// MMIO indices
enum {
    MMIO_USB3OTG,
};

// IRQ indices
enum {
    IRQ_USB3,
};

void dwc3_wait_bits(volatile uint32_t* ptr, uint32_t bits, uint32_t expected) {
    uint32_t value = DWC3_READ32(ptr);
    while ((value & bits) != expected) {
        usleep(1000);
        value = DWC3_READ32(ptr);
    }
}

void dwc3_print_status(dwc3_t* dwc) {
    volatile void* mmio = dwc3_mmio(dwc);
    uint32_t status = DWC3_READ32(mmio + DSTS);
    zxlogf(TRACE, "DSTS: ");
    zxlogf(TRACE, "USBLNKST: %d ", DSTS_USBLNKST(status));
    zxlogf(TRACE, "SOFFN: %d ", DSTS_SOFFN(status));
    zxlogf(TRACE, "CONNECTSPD: %d ", DSTS_CONNECTSPD(status));
    if (status & DSTS_DCNRD) zxlogf(TRACE, "DCNRD ");
    if (status & DSTS_SRE) zxlogf(TRACE, "SRE ");
    if (status & DSTS_RSS) zxlogf(TRACE, "RSS ");
    if (status & DSTS_SSS) zxlogf(TRACE, "SSS ");
    if (status & DSTS_COREIDLE) zxlogf(TRACE, "COREIDLE ");
    if (status & DSTS_DEVCTRLHLT) zxlogf(TRACE, "DEVCTRLHLT ");
    if (status & DSTS_RXFIFOEMPTY) zxlogf(TRACE, "RXFIFOEMPTY ");
    zxlogf(TRACE, "\n");

    status = DWC3_READ32(mmio + GSTS);
    zxlogf(TRACE, "GSTS: ");
    zxlogf(TRACE, "CBELT: %d ", GSTS_CBELT(status));
    zxlogf(TRACE, "CURMOD: %d ", GSTS_CURMOD(status));
    if (status & GSTS_SSIC_IP) zxlogf(TRACE, "SSIC_IP ");
    if (status & GSTS_OTG_IP) zxlogf(TRACE, "OTG_IP ");
    if (status & GSTS_BC_IP) zxlogf(TRACE, "BC_IP ");
    if (status & GSTS_ADP_IP) zxlogf(TRACE, "ADP_IP ");
    if (status & GSTS_HOST_IP) zxlogf(TRACE, "HOST_IP ");
    if (status & GSTS_DEVICE_IP) zxlogf(TRACE, "DEVICE_IP ");
    if (status & GSTS_CSR_TIMEOUT) zxlogf(TRACE, "CSR_TIMEOUT ");
    if (status & GSTS_BUSERRADDRVLD) zxlogf(TRACE, "BUSERRADDRVLD ");
    zxlogf(TRACE, "\n");
}

static void dwc3_stop(dwc3_t* dwc) {
    volatile void* mmio = dwc3_mmio(dwc);
    uint32_t temp;

    mtx_lock(&dwc->lock);

    temp = DWC3_READ32(mmio + DCTL);
    temp &= ~DCTL_RUN_STOP;
    temp |= DCTL_CSFTRST;
    DWC3_WRITE32(mmio + DCTL, temp);
    dwc3_wait_bits(mmio + DCTL, DCTL_CSFTRST, 0);

    mtx_unlock(&dwc->lock);
}

static void dwc3_start_peripheral_mode(dwc3_t* dwc) {
    volatile void* mmio = dwc3_mmio(dwc);
    uint32_t temp;

    mtx_lock(&dwc->lock);

    // configure and enable PHYs
    temp = DWC3_READ32(mmio + GUSB2PHYCFG(0));
    temp &= ~(GUSB2PHYCFG_USBTRDTIM_MASK | GUSB2PHYCFG_SUSPENDUSB20);
    temp |= GUSB2PHYCFG_USBTRDTIM(9);
    DWC3_WRITE32(mmio + GUSB2PHYCFG(0), temp);

    temp = DWC3_READ32(mmio + GUSB3PIPECTL(0));
    temp &= ~(GUSB3PIPECTL_DELAYP1TRANS | GUSB3PIPECTL_SUSPENDENABLE);
    temp |= GUSB3PIPECTL_LFPSFILTER | GUSB3PIPECTL_SS_TX_DE_EMPHASIS(1);
    DWC3_WRITE32(mmio + GUSB3PIPECTL(0), temp);

    // configure for device mode
    DWC3_WRITE32(mmio + GCTL, GCTL_U2EXIT_LFPS | GCTL_PRTCAPDIR_DEVICE | GCTL_U2RSTECN |
                              GCTL_PWRDNSCALE(2));

    temp = DWC3_READ32(mmio + DCFG);
    uint32_t nump = 16;
    uint32_t max_speed = DCFG_DEVSPD_SUPER;
    temp &= ~DWC3_MASK(DCFG_NUMP_START, DCFG_NUMP_BITS);
    temp |= nump << DCFG_NUMP_START;
    temp &= ~DWC3_MASK(DCFG_DEVSPD_START, DCFG_DEVSPD_BITS);
    temp |= max_speed << DCFG_DEVSPD_START;
    temp &= ~DWC3_MASK(DCFG_DEVADDR_START, DCFG_DEVADDR_BITS);  // clear address
    DWC3_WRITE32(mmio + DCFG, temp);

    dwc3_events_start(dwc);
    mtx_unlock(&dwc->lock);

    dwc3_ep0_start(dwc);

    mtx_lock(&dwc->lock);

    // start the controller
    DWC3_WRITE32(mmio + DCTL, DCTL_RUN_STOP);

    mtx_unlock(&dwc->lock);
}

static void dwc3_start_host_mode(dwc3_t* dwc) {
    volatile void* mmio = dwc3_mmio(dwc);

    mtx_lock(&dwc->lock);

    // configure for host mode
    DWC3_WRITE32(mmio + GCTL, GCTL_U2EXIT_LFPS | GCTL_PRTCAPDIR_HOST | GCTL_U2RSTECN |
                              GCTL_PWRDNSCALE(2));
    mtx_unlock(&dwc->lock);

}

void dwc3_usb_reset(dwc3_t* dwc) {
    zxlogf(INFO, "dwc3_usb_reset\n");

    dwc3_ep0_reset(dwc);

    for (unsigned i = 2; i < countof(dwc->eps); i++) {
        dwc3_ep_end_transfers(dwc, i, ZX_ERR_IO_NOT_PRESENT);
        dwc3_ep_set_stall(dwc, i, false);
    }

    dwc3_set_address(dwc, 0);
    dwc3_ep0_start(dwc);
    usb_dci_set_connected(&dwc->dci_intf, true);
}

void dwc3_disconnected(dwc3_t* dwc) {
    zxlogf(INFO, "dwc3_disconnected\n");

    dwc3_cmd_ep_end_transfer(dwc, EP0_OUT);
    dwc->ep0_state = EP0_STATE_NONE;

    if (dwc->dci_intf.ops) {
        usb_dci_set_connected(&dwc->dci_intf, false);
    }

    for (unsigned i = 2; i < countof(dwc->eps); i++) {
        dwc3_ep_end_transfers(dwc, i, ZX_ERR_IO_NOT_PRESENT);
        dwc3_ep_set_stall(dwc, i, false);
    }
}

void dwc3_connection_done(dwc3_t* dwc) {
    volatile void* mmio = dwc3_mmio(dwc);

    mtx_lock(&dwc->lock);
    uint32_t status = DWC3_READ32(mmio + DSTS);
    uint32_t speed = DSTS_CONNECTSPD(status);
    unsigned ep0_max_packet = 0;

    switch (speed) {
    case DSTS_CONNECTSPD_HIGH:
        dwc->speed = USB_SPEED_HIGH;
        ep0_max_packet = 64;
        break;
    case DSTS_CONNECTSPD_FULL:
        dwc->speed = USB_SPEED_FULL;
        ep0_max_packet = 64;
        break;
    case DSTS_CONNECTSPD_SUPER:
    case DSTS_CONNECTSPD_ENHANCED_SUPER:
        dwc->speed = USB_SPEED_SUPER;
        ep0_max_packet = 512;
        break;
    default:
        zxlogf(ERROR, "dwc3_connection_done: unsupported speed %u\n", speed);
        dwc->speed = USB_SPEED_UNDEFINED;
        break;
    }

    mtx_unlock(&dwc->lock);

    if (ep0_max_packet) {
        dwc->eps[EP0_OUT].max_packet_size = ep0_max_packet;
        dwc->eps[EP0_IN].max_packet_size = ep0_max_packet;
        dwc3_cmd_ep_set_config(dwc, EP0_OUT, USB_ENDPOINT_CONTROL, ep0_max_packet, 0, true);
        dwc3_cmd_ep_set_config(dwc, EP0_IN, USB_ENDPOINT_CONTROL, ep0_max_packet, 0, true);
    }

    usb_dci_set_speed(&dwc->dci_intf, dwc->speed);
}

void dwc3_set_address(dwc3_t* dwc, unsigned address) {
    volatile void* mmio = dwc3_mmio(dwc);
    mtx_lock(&dwc->lock);
    DWC3_SET_BITS32(mmio + DCFG, DCFG_DEVADDR_START, DCFG_DEVADDR_BITS, address);
    mtx_unlock(&dwc->lock);
}

void dwc3_reset_configuration(dwc3_t* dwc) {
    volatile void* mmio = dwc3_mmio(dwc);

    mtx_lock(&dwc->lock);

    // disable all endpoints except EP0_OUT and EP0_IN
    DWC3_WRITE32(mmio + DALEPENA, (1 << EP0_OUT) | (1 << EP0_IN));

    mtx_unlock(&dwc->lock);

    for (unsigned i = 2; i < countof(dwc->eps); i++) {
        dwc3_ep_end_transfers(dwc, i, ZX_ERR_IO_NOT_PRESENT);
        dwc3_ep_set_stall(dwc, i, false);
    }
}

static void dwc3_request_queue(void* ctx, usb_request_t* req) {
    dwc3_t* dwc = ctx;

    zxlogf(LTRACE, "dwc3_request_queue ep: %u\n", req->header.ep_address);
    unsigned ep_num = dwc3_ep_num(req->header.ep_address);
    if (ep_num < 2 || ep_num >= countof(dwc->eps)) {
        zxlogf(ERROR, "dwc3_request_queue: bad ep address 0x%02X\n", req->header.ep_address);
        usb_request_complete(req, ZX_ERR_INVALID_ARGS, 0);
        return;
    }

    dwc3_ep_queue(dwc, ep_num, req);
}

static zx_status_t dwc3_set_interface(void* ctx, usb_dci_interface_t* dci_intf) {
    dwc3_t* dwc = ctx;
    memcpy(&dwc->dci_intf, dci_intf, sizeof(dwc->dci_intf));
    return ZX_OK;
}

static zx_status_t dwc3_config_ep(void* ctx, usb_endpoint_descriptor_t* ep_desc,
                                  usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    dwc3_t* dwc = ctx;
    return dwc3_ep_config(dwc, ep_desc, ss_comp_desc);
}

static zx_status_t dwc3_disable_ep(void* ctx, uint8_t ep_addr) {
    dwc3_t* dwc = ctx;
    return dwc3_ep_disable(dwc, ep_addr);
}

static zx_status_t dwc3_set_stall(void* ctx, uint8_t ep_address) {
    dwc3_t* dwc = ctx;
    return dwc3_ep_set_stall(dwc, dwc3_ep_num(ep_address), true);
}

static zx_status_t dwc3_clear_stall(void* ctx, uint8_t ep_address) {
    dwc3_t* dwc = ctx;
    return dwc3_ep_set_stall(dwc, dwc3_ep_num(ep_address), false);
}

static zx_status_t dwc3_get_bti(void* ctx, zx_handle_t* out_handle) {
    dwc3_t* dwc = ctx;
    *out_handle = dwc->bti_handle;
    return ZX_OK;
}

usb_dci_protocol_ops_t dwc_dci_protocol = {
    .request_queue = dwc3_request_queue,
    .set_interface = dwc3_set_interface,
    .config_ep = dwc3_config_ep,
    .disable_ep = dwc3_disable_ep,
    .ep_set_stall = dwc3_set_stall,
    .ep_clear_stall = dwc3_clear_stall,
    .get_bti = dwc3_get_bti,
};

static zx_status_t dwc3_get_initial_mode(void* ctx, usb_mode_t* out_mode) {
    dwc3_t* dwc = ctx;
    return usb_mode_switch_get_initial_mode(&dwc->ums, out_mode);
}

static zx_status_t dwc3_set_mode(void* ctx, usb_mode_t mode) {
    dwc3_t* dwc = ctx;
    zx_status_t status = ZX_OK;

    if (mode == USB_MODE_OTG) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (dwc->usb_mode == mode) {
        return ZX_OK;
    }

    // Shutdown if we are in device mode
    if (dwc->usb_mode == USB_MODE_DEVICE) {
        dwc3_events_stop(dwc);
        zx_handle_close(dwc->irq_handle);
        dwc->irq_handle = ZX_HANDLE_INVALID;
        dwc3_disconnected(dwc);
        dwc3_stop(dwc);
    }

    if (mode == USB_MODE_HOST) {
        dwc3_start_host_mode(dwc);
    }

    status = usb_mode_switch_set_mode(&dwc->ums, mode);
    if (status != ZX_OK) {
        goto fail;
    }

    if (mode == USB_MODE_DEVICE) {
        status = pdev_map_interrupt(&dwc->pdev, IRQ_USB3, &dwc->irq_handle);
        if (status != ZX_OK) {
            zxlogf(ERROR, "dwc3_set_mode: pdev_map_interrupt failed\n");
            goto fail;
        }

        dwc3_start_peripheral_mode(dwc);
    }

    dwc->usb_mode = mode;
    return ZX_OK;

fail:
    usb_mode_switch_set_mode(&dwc->ums, USB_MODE_NONE);
    dwc->usb_mode = USB_MODE_NONE;

    return status;
}

usb_mode_switch_protocol_ops_t dwc_ums_protocol = {
    .get_initial_mode = dwc3_get_initial_mode,
    .set_mode = dwc3_set_mode,
};

static void dwc3_unbind(void* ctx) {
    dwc3_t* dwc = ctx;

    zx_interrupt_signal(dwc->irq_handle, ZX_INTERRUPT_SLOT_USER, 0);

    thrd_join(dwc->irq_thread, NULL);
    device_remove(dwc->zxdev);
}

static zx_status_t dwc3_get_protocol(void* ctx, uint32_t proto_id, void* out) {
    switch (proto_id) {
    case ZX_PROTOCOL_USB_DCI: {
        usb_dci_protocol_t* proto = out;
        proto->ops = &dwc_dci_protocol;
        proto->ctx = ctx;
        return ZX_OK;
    }
    case ZX_PROTOCOL_USB_MODE_SWITCH: {
        usb_mode_switch_protocol_t* proto = out;
        proto->ops = &dwc_ums_protocol;
        proto->ctx = ctx;
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void dwc3_release(void* ctx) {
    dwc3_t* dwc = ctx;

    for (unsigned i = 0; i < countof(dwc->eps); i++) {
        dwc3_ep_fifo_release(dwc, i);
    }
    pdev_vmo_buffer_release(&dwc->event_buffer);
    pdev_vmo_buffer_release(&dwc->ep0_buffer);
    pdev_vmo_buffer_release(&dwc->mmio);
    zx_handle_close(dwc->irq_handle);
    zx_handle_close(dwc->bti_handle);
    free(dwc);
}

static zx_protocol_device_t dwc3_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = dwc3_get_protocol,
    .release = dwc3_release,
};

static zx_status_t dwc3_bind(void* ctx, zx_device_t* parent) {
    zxlogf(INFO, "dwc3_bind\n");

    dwc3_t* dwc = calloc(1, sizeof(dwc3_t));
    if (!dwc) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &dwc->pdev);
    if (status != ZX_OK) {
        goto fail;
    }

    status = device_get_protocol(parent, ZX_PROTOCOL_USB_MODE_SWITCH, &dwc->ums);
    if (status != ZX_OK) {
        goto fail;
    }

    mtx_init(&dwc->lock, mtx_plain);

    status = pdev_get_bti(&dwc->pdev, 0, &dwc->bti_handle);
    if (status != ZX_OK) {
        goto fail;
    }

    for (unsigned i = 0; i < countof(dwc->eps); i++) {
        dwc3_endpoint_t* ep = &dwc->eps[i];
        ep->ep_num = i;
        mtx_init(&ep->lock, mtx_plain);
        list_initialize(&ep->queued_reqs);
    }
    dwc->parent = parent;
    dwc->usb_mode = USB_MODE_NONE;

    status = pdev_map_mmio_buffer(&dwc->pdev, MMIO_USB3OTG, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &dwc->mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "dwc3_bind: pdev_map_mmio_buffer failed\n");
        goto fail;
    }

    status = pdev_map_contig_buffer(&dwc->pdev, EVENT_BUFFER_SIZE, 0, ZX_VM_FLAG_PERM_READ,
                                    ZX_CACHE_POLICY_CACHED, &dwc->event_buffer);
    if (status != ZX_OK) {
        zxlogf(ERROR, "dwc3_bind: pdev_map_contig_buffer failed\n");
        goto fail;
    }
    pdev_vmo_buffer_cache_flush(&dwc->event_buffer, 0, EVENT_BUFFER_SIZE);

    status = pdev_map_contig_buffer(&dwc->pdev, 65536, 0,
                                    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                                    ZX_CACHE_POLICY_CACHED, &dwc->ep0_buffer);
    if (status != ZX_OK) {
        zxlogf(ERROR, "dwc3_bind: pdev_map_contig_buffer failed\n");
        goto fail;
    }

    status = dwc3_ep0_init(dwc);
    if (status != ZX_OK) {
        zxlogf(ERROR, "dwc3_bind: dwc3_ep_init failed\n");
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "dwc3",
        .ctx = dwc,
        .ops = &dwc3_device_proto,
        .proto_id = ZX_PROTOCOL_USB_DCI,
        .proto_ops = &dwc_dci_protocol,
    };

    status = device_add(parent, &args, &dwc->zxdev);
    if (status != ZX_OK) {
        goto fail;
    }

    return ZX_OK;

fail:
    zxlogf(ERROR, "dwc3_bind failed %d\n", status);
    dwc3_release(dwc);
    return status;
}

static zx_driver_ops_t dwc3_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = dwc3_bind,
};

// The formatter does not play nice with these macros.
// clang-format off
ZIRCON_DRIVER_BEGIN(dwc3, dwc3_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_DWC3),
ZIRCON_DRIVER_END(dwc3)
// clang-format on
