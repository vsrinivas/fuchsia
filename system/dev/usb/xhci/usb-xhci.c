// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb-hci.h>
#include <ddk/protocol/usb.h>

#include <hw/reg.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "xhci-device-manager.h"
#include "xhci-root-hub.h"
#include "xhci-util.h"
#include "xhci.h"

#define MAX_SLOTS 255

#define DEFAULT_PRIORITY 16
#define HIGH_PRIORITY    24

mx_status_t xhci_add_device(xhci_t* xhci, int slot_id, int hub_address, int speed) {
    dprintf(TRACE, "xhci_add_new_device\n");

    if (!xhci->bus.ops) {
        dprintf(ERROR, "no bus device in xhci_add_device\n");
        return MX_ERR_INTERNAL;
    }

    return usb_bus_add_device(&xhci->bus, slot_id, hub_address, speed);
}

void xhci_remove_device(xhci_t* xhci, int slot_id) {
    dprintf(TRACE, "xhci_remove_device %d\n", slot_id);

    if (!xhci->bus.ops) {
        dprintf(ERROR, "no bus device in xhci_remove_device\n");
        return;
    }

    usb_bus_remove_device(&xhci->bus, slot_id);
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

static mx_status_t xhci_enable_ep(void* ctx, uint32_t device_id,
                                  usb_endpoint_descriptor_t* ep_desc,
                                  usb_ss_ep_comp_descriptor_t* ss_comp_desc, bool enable) {
    xhci_t* xhci = ctx;
    return xhci_enable_endpoint(xhci, device_id, ep_desc, ss_comp_desc, enable);
}

static uint64_t xhci_get_frame(void* ctx) {
    xhci_t* xhci = ctx;
    return xhci_get_current_frame(xhci);
}

static mx_status_t xhci_config_hub(void* ctx, uint32_t device_id, usb_speed_t speed,
                            usb_hub_descriptor_t* descriptor) {
    xhci_t* xhci = ctx;
    return xhci_configure_hub(xhci, device_id, speed, descriptor);
}

static mx_status_t xhci_hub_device_added(void* ctx, uint32_t hub_address, int port,
                                  usb_speed_t speed) {
    xhci_t* xhci = ctx;
    return xhci_enumerate_device(xhci, hub_address, port, speed);
}

static mx_status_t xhci_hub_device_removed(void* ctx, uint32_t hub_address, int port) {
    xhci_t* xhci = ctx;
    xhci_device_disconnected(xhci, hub_address, port);
    return MX_OK;
}

static mx_status_t xhci_reset_ep(void* ctx, uint32_t device_id, uint8_t ep_address) {
    xhci_t* xhci = ctx;
    uint8_t ep_index = xhci_endpoint_index(ep_address);
    return xhci_reset_endpoint(xhci, device_id, ep_index);
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

static mx_status_t xhci_cancel_all(void* ctx, uint32_t device_id, uint8_t ep_address) {
    xhci_t* xhci = ctx;
    return xhci_cancel_transfers(xhci, device_id, ep_address);
}

usb_hci_protocol_ops_t xhci_hci_protocol = {
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
};

static void xhci_iotxn_queue(void* ctx, iotxn_t* txn) {
    xhci_t* xhci = ctx;
    usb_protocol_data_t* data = iotxn_pdata(txn, usb_protocol_data_t);
    mx_status_t status;

    if (txn->length > xhci_get_max_transfer_size(xhci->mxdev, data->device_id, data->ep_address)) {
        status = MX_ERR_INVALID_ARGS;
    } else {
        status = xhci_queue_transfer(xhci, txn);
    }

    if (status != MX_OK && status != MX_ERR_BUFFER_TOO_SMALL) {
        iotxn_complete(txn, status, 0);
    }
}

static void xhci_unbind(void* ctx) {
    xhci_t* xhci = ctx;
    dprintf(TRACE, "xhci_unbind\n");

    device_remove(xhci->mxdev);
}

static void xhci_release(void* ctx) {
     xhci_t* xhci = ctx;

   // FIXME(voydanoff) - there is a lot more work to do here
    free(xhci);
}

static mx_protocol_device_t xhci_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .iotxn_queue = xhci_iotxn_queue,
    .unbind = xhci_unbind,
    .release = xhci_release,
};

typedef struct completer {
    uint32_t interrupter;
    xhci_t *xhci;
    uint32_t priority;
} completer_t;

static int completer_thread(void *arg) {
    completer_t* completer = (completer_t*)arg;
    mx_handle_t irq_handle = completer->xhci->irq_handles[completer->interrupter];

    // TODO(johngro) : See MG-940.  Get rid of this.  For now we need thread
    // priorities so that realtime transactions use the completer which ends
    // up getting realtime latency guarantees.
    mx_thread_set_priority(completer->priority);

    while (1) {
        mx_status_t wait_res;

        wait_res = mx_interrupt_wait(irq_handle);
        if (wait_res != MX_OK) {
            dprintf(ERROR, "unexpected pci_wait_interrupt failure (%d)\n", wait_res);
            mx_interrupt_complete(irq_handle);
            break;
        }
        mx_interrupt_complete(irq_handle);
        xhci_handle_interrupt(completer->xhci, completer->xhci->legacy_irq_mode,
                              completer->interrupter);
    }
    dprintf(TRACE, "xhci completer %u thread done\n", completer->interrupter);
    free(completer);
    return 0;
}

static int xhci_start_thread(void* arg) {
    xhci_t* xhci = (xhci_t*)arg;
    dprintf(TRACE, "xhci_start_thread start\n");

    mx_status_t status;
    completer_t* completers[xhci->num_interrupts];
    uint32_t num_completers_initialized = 0;
    for (uint32_t i = 0; i < xhci->num_interrupts; i++) {
        completer_t *completer = calloc(1, sizeof(completer_t));
        if (completer == NULL) {
            status = MX_ERR_NO_MEMORY;
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
    if (status != MX_OK) {
        goto error_return;
    }

   device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "xhci",
        .ctx = xhci,
        .ops = &xhci_device_proto,
        .proto_id = MX_PROTOCOL_USB_HCI,
        .proto_ops = &xhci_hci_protocol,
    };

    status = device_add(xhci->parent, &args, &xhci->mxdev);
    if (status != MX_OK) {
        goto error_return;
    }

    for (uint32_t i = 0; i < xhci->num_interrupts; i++) {
        thrd_t thread;
        thrd_create_with_name(&thread, completer_thread, completers[i], "completer_thread");
        thrd_detach(thread);
    }

    dprintf(TRACE, "xhci_start_thread done\n");
    return 0;

error_return:
    free(xhci);
    for (uint32_t i = 0; i < num_completers_initialized; i++) {
        free(completers[i]);
    }
    return status;
}

static mx_status_t usb_xhci_bind(void* ctx, mx_device_t* dev, void** cookie) {
    mx_handle_t mmio_handle = MX_HANDLE_INVALID;
    mx_handle_t cfg_handle = MX_HANDLE_INVALID;
    xhci_t* xhci = NULL;
    uint32_t num_irq_handles_initialized = 0;
    mx_status_t status;

    pci_protocol_t pci;
    if (device_get_protocol(dev, MX_PROTOCOL_PCI, &pci)) {
        status = MX_ERR_NOT_SUPPORTED;
        goto error_return;
    }

    xhci = calloc(1, sizeof(xhci_t));
    if (!xhci) {
        status = MX_ERR_NO_MEMORY;
        goto error_return;
    }

    void* mmio;
    uint64_t mmio_len;
    /*
     * eXtensible Host Controller Interface revision 1.1, section 5, xhci
     * should only use BARs 0 and 1. 0 for 32 bit addressing, and 0+1 for 64 bit addressing.
     */
    status = pci_map_resource(&pci, PCI_RESOURCE_BAR_0, MX_CACHE_POLICY_UNCACHED_DEVICE,
                              &mmio, &mmio_len, &mmio_handle);
    if (status != MX_OK) {
        dprintf(ERROR, "usb_xhci_bind could not find bar\n");
        status = MX_ERR_INTERNAL;
         goto error_return;
    }

    uint32_t irq_cnt = 0;
    status = pci_query_irq_mode_caps(&pci, MX_PCIE_IRQ_MODE_MSI, &irq_cnt);
    if (status != MX_OK) {
        dprintf(ERROR, "pci_query_irq_mode_caps failed %d\n", status);
        goto error_return;
    }
    xhci_num_interrupts_init(xhci, mmio, irq_cnt);

    // select our IRQ mode
    status = pci_set_irq_mode(&pci, MX_PCIE_IRQ_MODE_MSI, xhci->num_interrupts);
    if (status < 0) {
        mx_status_t status_legacy = pci_set_irq_mode(&pci, MX_PCIE_IRQ_MODE_LEGACY, 1);

        if (status_legacy < 0) {
            dprintf(ERROR, "usb_xhci_bind Failed to set IRQ mode to either MSI "
                   "(err = %d) or Legacy (err = %d)\n",
                   status, status_legacy);
            goto error_return;
        }

        xhci->legacy_irq_mode = true;
        xhci->num_interrupts = 1;
    }

    for (uint32_t i = 0; i < xhci->num_interrupts; i++) {
        // register for interrupts
        status = pci_map_interrupt(&pci, i, &xhci->irq_handles[i]);
        if (status != MX_OK) {
            dprintf(ERROR, "usb_xhci_bind map_interrupt failed %d\n", status);
            goto error_return;
        }
        num_irq_handles_initialized++;
    }
    xhci->mmio_handle = mmio_handle;
    xhci->cfg_handle = cfg_handle;

    // stash this here for the startup thread to call device_add() with
    xhci->parent = dev;
    // used for enabling bus mastering
    memcpy(&xhci->pci, &pci, sizeof(pci_protocol_t));

    status = xhci_init(xhci, mmio);
    if (status != MX_OK) {
        goto error_return;
    }

    thrd_t thread;
    thrd_create_with_name(&thread, xhci_start_thread, xhci, "xhci_start_thread");
    thrd_detach(thread);

    return MX_OK;

error_return:
    if (xhci) {
        free(xhci);
    }
    for (uint32_t i = 0; i < num_irq_handles_initialized; i++) {
        mx_handle_close(xhci->irq_handles[i]);
    }
    if (mmio_handle != MX_HANDLE_INVALID) {
        mx_handle_close(mmio_handle);
    }
    if (cfg_handle != MX_HANDLE_INVALID) {
        mx_handle_close(cfg_handle);
    }
    return status;
}

static mx_driver_ops_t xhci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_xhci_bind,
};

// clang-format off
MAGENTA_DRIVER_BEGIN(usb_xhci, xhci_driver_ops, "magenta", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_CLASS, 0x0C),
    BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, 0x03),
    BI_MATCH_IF(EQ, BIND_PCI_INTERFACE, 0x30),
MAGENTA_DRIVER_END(usb_xhci)
