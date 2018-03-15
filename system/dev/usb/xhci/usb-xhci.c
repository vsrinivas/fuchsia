// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/usb-hci.h>
#include <ddk/protocol/usb.h>

#include <hw/arch_ops.h>
#include <hw/reg.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "xhci-device-manager.h"
#include "xhci-root-hub.h"
#include "xhci-util.h"
#include "xhci.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define MAX_SLOTS 255

#define DEFAULT_PRIORITY 16
#define HIGH_PRIORITY    24

#define PDEV_MMIO_INDEX  0
#define PDEV_IRQ_INDEX   0

zx_status_t xhci_add_device(xhci_t* xhci, int slot_id, int hub_address, int speed) {
    zxlogf(TRACE, "xhci_add_new_device\n");

    if (!xhci->bus.ops) {
        zxlogf(ERROR, "no bus device in xhci_add_device\n");
        return ZX_ERR_INTERNAL;
    }

    return usb_bus_add_device(&xhci->bus, slot_id, hub_address, speed);
}

void xhci_remove_device(xhci_t* xhci, int slot_id) {
    zxlogf(TRACE, "xhci_remove_device %d\n", slot_id);

    if (!xhci->bus.ops) {
        zxlogf(ERROR, "no bus device in xhci_remove_device\n");
        return;
    }

    usb_bus_remove_device(&xhci->bus, slot_id);
}

static void xhci_hci_request_queue(void* ctx, usb_request_t* req) {
    xhci_request_queue(ctx, req);
}

static void xhci_set_bus_interface(void* ctx, usb_bus_interface_t* bus) {
    xhci_t* xhci = ctx;

    if (bus) {
        memcpy(&xhci->bus, bus, sizeof(xhci->bus));
        // wait until bus driver has started before doing this
        xhci_queue_start_root_hubs(xhci);
    } else {
        memset(&xhci->bus, 0, sizeof(xhci->bus));
    }
}

static size_t xhci_get_max_device_count(void* ctx) {
    xhci_t* xhci = ctx;
    // add one to allow device IDs to be 1-based
    return xhci->max_slots + XHCI_RH_COUNT + 1;
}

static zx_status_t xhci_enable_ep(void* ctx, uint32_t device_id,
                                  usb_endpoint_descriptor_t* ep_desc,
                                  usb_ss_ep_comp_descriptor_t* ss_comp_desc, bool enable) {
    xhci_t* xhci = ctx;
    return xhci_enable_endpoint(xhci, device_id, ep_desc, ss_comp_desc, enable);
}

static uint64_t xhci_get_frame(void* ctx) {
    xhci_t* xhci = ctx;
    return xhci_get_current_frame(xhci);
}

static zx_status_t xhci_config_hub(void* ctx, uint32_t device_id, usb_speed_t speed,
                            usb_hub_descriptor_t* descriptor) {
    xhci_t* xhci = ctx;
    return xhci_configure_hub(xhci, device_id, speed, descriptor);
}

static zx_status_t xhci_hub_device_added(void* ctx, uint32_t hub_address, int port,
                                  usb_speed_t speed) {
    xhci_t* xhci = ctx;
    return xhci_enumerate_device(xhci, hub_address, port, speed);
}

static zx_status_t xhci_hub_device_removed(void* ctx, uint32_t hub_address, int port) {
    xhci_t* xhci = ctx;
    xhci_device_disconnected(xhci, hub_address, port);
    return ZX_OK;
}

static zx_status_t xhci_reset_ep(void* ctx, uint32_t device_id, uint8_t ep_address) {
    xhci_t* xhci = ctx;
    return xhci_reset_endpoint(xhci, device_id, ep_address);
}

static size_t xhci_get_max_transfer_size(void* ctx, uint32_t device_id, uint8_t ep_address) {
    if (ep_address == 0) {
        // control requests have uint16 length field so we need to support UINT16_MAX
        // we require one setup, status and data event TRB in addition to data transfer TRBs
        // and subtract one more to account for the link TRB
        static_assert(PAGE_SIZE * (TRANSFER_RING_SIZE - 4) >= UINT16_MAX, "TRANSFER_RING_SIZE too small");
        return UINT16_MAX;
    }
    // non-control transfers consist of normal transfer TRBs plus one data event TRB
    // Subtract 2 to reserve a TRB for data event and to account for the link TRB
    return PAGE_SIZE * (TRANSFER_RING_SIZE - 2);
}

static zx_status_t xhci_cancel_all(void* ctx, uint32_t device_id, uint8_t ep_address) {
    xhci_t* xhci = ctx;
    return xhci_cancel_transfers(xhci, device_id, ep_address);
}

static zx_status_t xhci_get_bti(void* ctx, zx_handle_t* out_handle) {
    xhci_t* xhci = ctx;
    *out_handle = xhci->bti_handle;
    return ZX_OK;
}

usb_hci_protocol_ops_t xhci_hci_protocol = {
    .request_queue = xhci_hci_request_queue,
    .set_bus_interface = xhci_set_bus_interface,
    .get_max_device_count = xhci_get_max_device_count,
    .enable_endpoint = xhci_enable_ep,
    .get_current_frame = xhci_get_frame,
    .configure_hub = xhci_config_hub,
    .hub_device_added = xhci_hub_device_added,
    .hub_device_removed = xhci_hub_device_removed,
    .reset_endpoint = xhci_reset_ep,
    .get_max_transfer_size = xhci_get_max_transfer_size,
    .cancel_all = xhci_cancel_all,
    .get_bti = xhci_get_bti,
};

void xhci_request_queue(xhci_t* xhci, usb_request_t* req) {
    zx_status_t status;

    if (req->header.length > xhci_get_max_transfer_size(xhci->zxdev, req->header.device_id,
                                                        req->header.ep_address)) {
        status = ZX_ERR_INVALID_ARGS;
    } else {
        status = xhci_queue_transfer(xhci, req);
    }

    if (status != ZX_OK && status != ZX_ERR_BUFFER_TOO_SMALL) {
        usb_request_complete(req, status, 0);
    }
}

static void xhci_shutdown(xhci_t* xhci) {
    // stop the controller and our device thread
    xhci_stop(xhci);
    atomic_store(&xhci->suspended, true);
    // stop our interrupt threads
    for (uint32_t i = 0; i < xhci->num_interrupts; i++) {
        zx_interrupt_signal(xhci->irq_handles[i], ZX_INTERRUPT_SLOT_USER, 0);
        thrd_join(xhci->completer_threads[i], NULL);
        zx_handle_close(xhci->irq_handles[i]);
    }
}

static zx_status_t xhci_suspend(void* ctx, uint32_t flags) {
    zxlogf(TRACE, "xhci_suspend %u\n", flags);
    xhci_t* xhci = ctx;

    // TODO(voydanoff) do different things based on the flags.
    // for now we shutdown the driver in preparation for mexec
    xhci_shutdown(xhci);

    return ZX_OK;
}

static void xhci_unbind(void* ctx) {
    zxlogf(INFO, "xhci_unbind\n");
    xhci_t* xhci = ctx;

    xhci_shutdown(xhci);
    device_remove(xhci->zxdev);
}

static void xhci_release(void* ctx) {
    zxlogf(INFO, "xhci_release\n");
    xhci_t* xhci = ctx;
    if (xhci->mmio) {
        zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)xhci->mmio, xhci->mmio_size);
        zx_handle_close(xhci->mmio_handle);
    }
    zx_handle_close(xhci->cfg_handle);
    xhci_free(xhci);
}

static zx_protocol_device_t xhci_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .suspend = xhci_suspend,
    .unbind = xhci_unbind,
    .release = xhci_release,
};

typedef struct completer {
    xhci_t *xhci;
    uint32_t interrupter;
    uint32_t priority;
} completer_t;

static int completer_thread(void *arg) {
    completer_t* completer = (completer_t*)arg;
    zx_handle_t irq_handle = completer->xhci->irq_handles[completer->interrupter];

    // TODO(johngro): See ZX-940.  Get rid of this.  For now we need thread
    // priorities so that realtime transactions use the completer which ends
    // up getting realtime latency guarantees.
    zx_thread_set_priority(completer->priority);

    while (1) {
        zx_status_t wait_res;
        uint64_t slots;
        wait_res = zx_interrupt_wait(irq_handle, &slots);

        if (wait_res != ZX_OK) {
            zxlogf(ERROR, "unexpected pci_wait_interrupt failure (%d)\n", wait_res);
            break;
        }
        if (slots & (1ul << ZX_INTERRUPT_SLOT_USER)) {
            break;
        }
        if (atomic_load(&completer->xhci->suspended)) {
            // TODO(ravoorir): Remove this hack once the interrupt signalling bug
            // is resolved.
            zxlogf(ERROR, "race in zx_interrupt_cancel triggered. Kick off workaround for now\n");
            break;
        }
        xhci_handle_interrupt(completer->xhci, completer->interrupter);
    }
    zxlogf(TRACE, "xhci completer %u thread done\n", completer->interrupter);
    free(completer);
    return 0;
}

static int xhci_start_thread(void* arg) {
    xhci_t* xhci = (xhci_t*)arg;
    zxlogf(TRACE, "xhci_start_thread start\n");

    zx_status_t status;
    completer_t* completers[xhci->num_interrupts];
    uint32_t num_completers_initialized = 0;
    for (uint32_t i = 0; i < xhci->num_interrupts; i++) {
        completer_t *completer = calloc(1, sizeof(completer_t));
        if (completer == NULL) {
            status = ZX_ERR_NO_MEMORY;
            goto error_return;
        }
        completers[i] = completer;
        completer->interrupter = i;
        completer->xhci = xhci;
        // We need a high priority thread for isochronous transfers.
        // If there is only one interrupt available, that thread will need
        // to be high priority.
        completer->priority = (i == ISOCH_INTERRUPTER || xhci->num_interrupts == 1) ?
                              HIGH_PRIORITY : DEFAULT_PRIORITY;
        num_completers_initialized++;
    }

    // xhci_start will block, so do this part here instead of in usb_xhci_bind
    status = xhci_start(xhci);
    if (status != ZX_OK) {
        goto error_return;
    }

    device_make_visible(xhci->zxdev);
    atomic_store(&xhci->suspended, false);
    for (uint32_t i = 0; i < xhci->num_interrupts; i++) {
        thrd_create_with_name(&xhci->completer_threads[i], completer_thread, completers[i],
                              "completer_thread");
    }

    zxlogf(TRACE, "xhci_start_thread done\n");
    return 0;

error_return:
    device_remove(xhci->zxdev);
    free(xhci);
    for (uint32_t i = 0; i < num_completers_initialized; i++) {
        free(completers[i]);
    }
    return status;
}

static zx_status_t xhci_finish_bind(xhci_t* xhci, zx_device_t* parent) {
   device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "xhci",
        .ctx = xhci,
        .ops = &xhci_device_proto,
        .proto_id = ZX_PROTOCOL_USB_HCI,
        .proto_ops = &xhci_hci_protocol,
        .flags = DEVICE_ADD_INVISIBLE
    };

    zx_status_t status = device_add(parent, &args, &xhci->zxdev);
    if (status != ZX_OK) {
        return status;
    }

    thrd_t thread;
    thrd_create_with_name(&thread, xhci_start_thread, xhci, "xhci_start_thread");
    thrd_detach(thread);

    return ZX_OK;
}

static zx_status_t usb_xhci_bind_pci(zx_device_t* parent, pci_protocol_t* pci) {
    zx_handle_t cfg_handle = ZX_HANDLE_INVALID;
    xhci_t* xhci = NULL;
    uint32_t num_irq_handles_initialized = 0;
    zx_status_t status;

    xhci = calloc(1, sizeof(xhci_t));
    if (!xhci) {
        status = ZX_ERR_NO_MEMORY;
        goto error_return;
    }

    status = pci_get_bti(pci, 0, &xhci->bti_handle);
    if (status != ZX_OK) {
        goto error_return;
    }

    /*
     * eXtensible Host Controller Interface revision 1.1, section 5, xhci
     * should only use BARs 0 and 1. 0 for 32 bit addressing, and 0+1 for 64 bit addressing.
     */
    status = pci_map_bar(pci, 0u, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                              &xhci->mmio, &xhci->mmio_size, &xhci->mmio_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_xhci_bind could not find bar\n");
        status = ZX_ERR_INTERNAL;
         goto error_return;
    }

    uint32_t irq_cnt = 0;
    status = pci_query_irq_mode(pci, ZX_PCIE_IRQ_MODE_MSI, &irq_cnt);
    if (status != ZX_OK) {
        zxlogf(ERROR, "pci_query_irq_mode failed %d\n", status);
        goto error_return;
    }

    // Cap IRQ count at the number of interrupters we want to use and
    // the number of interrupters supported by XHCI.
    irq_cnt = MIN(irq_cnt, INTERRUPTER_COUNT);
    irq_cnt = MIN(irq_cnt, xhci_get_max_interrupters(xhci));

    // select our IRQ mode
    xhci_mode_t mode = XHCI_PCI_MSI;
    status = pci_set_irq_mode(pci, ZX_PCIE_IRQ_MODE_MSI, irq_cnt);
    if (status < 0) {
        zxlogf(ERROR, "MSI interrupts not available, irq_cnt: %d, err: %d\n",
               irq_cnt, status);
        zx_status_t status_legacy = pci_set_irq_mode(pci, ZX_PCIE_IRQ_MODE_LEGACY, 1);

        if (status_legacy < 0) {
            zxlogf(ERROR, "usb_xhci_bind Failed to set IRQ mode to either MSI "
                   "(err = %d) or Legacy (err = %d)\n",
                   status, status_legacy);
            goto error_return;
        }

        mode = XHCI_PCI_LEGACY;
        irq_cnt = 1;
    }

    for (uint32_t i = 0; i < irq_cnt; i++) {
        // register for interrupts
        status = pci_map_interrupt(pci, i, &xhci->irq_handles[i]);
        if (status != ZX_OK) {
            zxlogf(ERROR, "usb_xhci_bind map_interrupt failed %d\n", status);
            goto error_return;
        }
        num_irq_handles_initialized++;
    }
    xhci->cfg_handle = cfg_handle;

    // used for enabling bus mastering
    memcpy(&xhci->pci, pci, sizeof(pci_protocol_t));

    status = xhci_init(xhci, mode, irq_cnt);
    if (status != ZX_OK) {
        goto error_return;
    }
    status = xhci_finish_bind(xhci, parent);
    if (status != ZX_OK) {
        goto error_return;
    }

    return ZX_OK;

error_return:
    zx_handle_close(xhci->bti_handle);
    free(xhci);
    for (uint32_t i = 0; i < num_irq_handles_initialized; i++) {
        zx_handle_close(xhci->irq_handles[i]);
    }
    if (xhci->mmio) {
        zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)xhci->mmio, xhci->mmio_size);
        zx_handle_close(xhci->mmio_handle);
    }
    zx_handle_close(xhci->cfg_handle);
    return status;
}


static zx_status_t usb_xhci_bind_pdev(zx_device_t* parent, platform_device_protocol_t* pdev) {
    zx_handle_t irq_handle = ZX_HANDLE_INVALID;
    xhci_t* xhci = NULL;
    zx_status_t status;

    xhci = calloc(1, sizeof(xhci_t));
    if (!xhci) {
        status = ZX_ERR_NO_MEMORY;
        goto error_return;
    }

    status = pdev_get_bti(pdev, 0, &xhci->bti_handle);
    if (status != ZX_OK) {
        goto error_return;
    }

    status = pdev_map_mmio(pdev, PDEV_MMIO_INDEX, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                           &xhci->mmio, &xhci->mmio_size, &xhci->mmio_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_xhci_bind_pdev: pdev_map_mmio failed\n");
        goto error_return;
    }

    status = pdev_map_interrupt(pdev, PDEV_IRQ_INDEX, &irq_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_xhci_bind_pdev: pdev_map_interrupt failed\n");
        goto error_return;
    }

    xhci->irq_handles[0] = irq_handle;

    status = xhci_init(xhci, XHCI_PDEV, 1);
    if (status != ZX_OK) {
        goto error_return;
    }
    status = xhci_finish_bind(xhci, parent);
    if (status != ZX_OK) {
        goto error_return;
    }

    return ZX_OK;

error_return:
    zx_handle_close(xhci->bti_handle);
    free(xhci);
    if (xhci->mmio) {
        zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)xhci->mmio, xhci->mmio_size);
        zx_handle_close(xhci->mmio_handle);
    }
    zx_handle_close(irq_handle);
    return status;
}

static zx_status_t usb_xhci_bind(void* ctx, zx_device_t* parent) {
    pci_protocol_t pci;
    platform_device_protocol_t pdev;
    zx_status_t status;

    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PCI, &pci)) == ZX_OK) {
        return usb_xhci_bind_pci(parent, &pci);
    }
    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &pdev)) == ZX_OK) {
        return usb_xhci_bind_pdev(parent, &pdev);
    }

    return status;
}

static zx_driver_ops_t xhci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_xhci_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(usb_xhci, xhci_driver_ops, "zircon", "0.1", 9)
    // PCI binding support
    BI_GOTO_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI, 0),
    BI_ABORT_IF(NE, BIND_PCI_CLASS, 0x0C),
    BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, 0x03),
    BI_MATCH_IF(EQ, BIND_PCI_INTERFACE, 0x30),

    // platform bus binding support
    BI_LABEL(0),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_XHCI),

    BI_ABORT(),
ZIRCON_DRIVER_END(usb_xhci)
