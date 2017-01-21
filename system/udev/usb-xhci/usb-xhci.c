// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-alloc.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/usb.h>
#include <ddk/protocol/usb-bus.h>
#include <ddk/protocol/usb-hci.h>

#include <hw/reg.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xhci.h"
#include "xhci-device-manager.h"
#include "xhci-root-hub.h"
#include "xhci-util.h"

//#define TRACE 1
#include "xhci-debug.h"

#define MAX_SLOTS 255

typedef struct usb_xhci {
    xhci_t xhci;
    // the device we implement
    mx_device_t device;

    mx_device_t* bus_device;
    usb_bus_protocol_t* bus_protocol;

    io_alloc_t* io_alloc;
    pci_protocol_t* pci_proto;
    bool legacy_irq_mode;
    mx_handle_t irq_handle;
    mx_handle_t mmio_handle;
    mx_handle_t cfg_handle;
    thrd_t irq_thread;

    // used by the start thread
    mx_device_t* parent;
} usb_xhci_t;
#define xhci_to_usb_xhci(dev) containerof(dev, usb_xhci_t, xhci)
#define dev_to_usb_xhci(dev) containerof(dev, usb_xhci_t, device)

mx_status_t xhci_add_device(xhci_t* xhci, int slot_id, int hub_address, int speed) {
    usb_xhci_t* uxhci = xhci_to_usb_xhci(xhci);
    xprintf("xhci_add_new_device\n");

    if (!uxhci->bus_device || !uxhci->bus_protocol) {
        printf("no bus device in xhci_add_device\n");
        return ERR_INTERNAL;
    }

    return uxhci->bus_protocol->add_device(uxhci->bus_device, slot_id, hub_address, speed);
}

void xhci_remove_device(xhci_t* xhci, int slot_id) {
    usb_xhci_t* uxhci = xhci_to_usb_xhci(xhci);
    xprintf("xhci_remove_device %d\n", slot_id);

    if (!uxhci->bus_device || !uxhci->bus_protocol) {
        printf("no bus device in xhci_remove_device\n");
        return;
    }

    uxhci->bus_protocol->remove_device(uxhci->bus_device, slot_id);
}

void* xhci_malloc(xhci_t* xhci, size_t size) {
    usb_xhci_t* uxhci = xhci_to_usb_xhci(xhci);
    return io_malloc(uxhci->io_alloc, size);
}

void* xhci_memalign(xhci_t* xhci, size_t alignment, size_t size) {
    usb_xhci_t* uxhci = xhci_to_usb_xhci(xhci);
    void* result = io_memalign(uxhci->io_alloc, alignment, size);
    if (result) {
        memset(result, 0, size);
    }
    return result;
}

void xhci_free(xhci_t* xhci, void* addr) {
    usb_xhci_t* uxhci = xhci_to_usb_xhci(xhci);
    io_free(uxhci->io_alloc, addr);
}

void xhci_free_phys(xhci_t* xhci, mx_paddr_t addr) {
    usb_xhci_t* uxhci = xhci_to_usb_xhci(xhci);
    io_free(uxhci->io_alloc, (void*)io_phys_to_virt(uxhci->io_alloc, addr));
}

mx_paddr_t xhci_virt_to_phys(xhci_t* xhci, mx_vaddr_t addr) {
    usb_xhci_t* uxhci = xhci_to_usb_xhci(xhci);
    return io_virt_to_phys(uxhci->io_alloc, addr);
}

mx_vaddr_t xhci_phys_to_virt(xhci_t* xhci, mx_paddr_t addr) {
    usb_xhci_t* uxhci = xhci_to_usb_xhci(xhci);
    return io_phys_to_virt(uxhci->io_alloc, addr);
}

static int xhci_irq_thread(void* arg) {
    usb_xhci_t* uxhci = (usb_xhci_t*)arg;
    xprintf("xhci_irq_thread start\n");

    // xhci_start will block, so do this part here instead of in usb_xhci_bind
    xhci_start(&uxhci->xhci);

    device_add(&uxhci->device, uxhci->parent);
    uxhci->parent = NULL;

    while (1) {
        mx_status_t wait_res;

        wait_res = mx_interrupt_wait(uxhci->irq_handle);
        if (wait_res != NO_ERROR) {
            if (wait_res != ERR_HANDLE_CLOSED) {
                printf("unexpected pci_wait_interrupt failure (%d)\n", wait_res);
            }
            mx_interrupt_complete(uxhci->irq_handle);
            break;
        }

        mx_interrupt_complete(uxhci->irq_handle);
        xhci_handle_interrupt(&uxhci->xhci, uxhci->legacy_irq_mode);
    }
    xprintf("xhci_irq_thread done\n");
    return 0;
}

static void xhci_set_bus_device(mx_device_t* device, mx_device_t* busdev) {
    usb_xhci_t* uxhci = dev_to_usb_xhci(device);
    uxhci->bus_device = busdev;
    if (busdev) {
        device_get_protocol(busdev, MX_PROTOCOL_USB_BUS, (void**)&uxhci->bus_protocol);
        // wait until bus driver has started before doing this
        xhci_queue_start_root_hubs(&uxhci->xhci);
    } else {
        uxhci->bus_protocol = NULL;
    }
}

static size_t xhci_get_max_device_count(mx_device_t* device) {
    usb_xhci_t* uxhci = dev_to_usb_xhci(device);
    // add one to allow device IDs to be 1-based
    return uxhci->xhci.max_slots + XHCI_RH_COUNT + 1;
}

static mx_status_t xhci_enable_ep(mx_device_t* hci_device, uint32_t device_id,
                                  usb_endpoint_descriptor_t* ep_desc, bool enable) {
    usb_xhci_t* uxhci = dev_to_usb_xhci(hci_device);
    return xhci_enable_endpoint(&uxhci->xhci, device_id, ep_desc, enable);
}

static uint64_t xhci_get_frame(mx_device_t* hci_device) {
    usb_xhci_t* uxhci = dev_to_usb_xhci(hci_device);
    return xhci_get_current_frame(&uxhci->xhci);
}

mx_status_t xhci_config_hub(mx_device_t* hci_device, uint32_t device_id, usb_speed_t speed,
                            usb_hub_descriptor_t* descriptor) {
    usb_xhci_t* uxhci = dev_to_usb_xhci(hci_device);
    return xhci_configure_hub(&uxhci->xhci, device_id, speed, descriptor);
}

mx_status_t xhci_hub_device_added(mx_device_t* hci_device, uint32_t hub_address, int port,
                                  usb_speed_t speed) {
    usb_xhci_t* uxhci = dev_to_usb_xhci(hci_device);
    return xhci_enumerate_device(&uxhci->xhci, hub_address, port, speed);
}

mx_status_t xhci_hub_device_removed(mx_device_t* hci_device, uint32_t hub_address, int port) {
    usb_xhci_t* uxhci = dev_to_usb_xhci(hci_device);
    xhci_device_disconnected(&uxhci->xhci, hub_address, port);
    return NO_ERROR;
}

usb_hci_protocol_t xhci_hci_protocol = {
    .set_bus_device = xhci_set_bus_device,
    .get_max_device_count = xhci_get_max_device_count,
    .enable_endpoint = xhci_enable_ep,
    .get_current_frame = xhci_get_frame,
    .configure_hub = xhci_config_hub,
    .hub_device_added = xhci_hub_device_added,
    .hub_device_removed = xhci_hub_device_removed,
};

static void xhci_iotxn_callback(mx_status_t result, void* cookie) {
    iotxn_t* txn = (iotxn_t *)cookie;
    mx_status_t status;
    size_t actual;

    if (result > 0) {
        actual = result;
        status = NO_ERROR;
    } else {
        actual = 0;
        status = result;
    }
    free(txn->context);
    txn->context = NULL;

    txn->ops->complete(txn, status, actual);
}

static mx_status_t xhci_do_iotxn_queue(xhci_t* xhci, iotxn_t* txn) {
    usb_protocol_data_t* data = iotxn_pdata(txn, usb_protocol_data_t);
    int rh_index = xhci_get_root_hub_index(xhci, data->device_id);
    if (rh_index >= 0) {
        return xhci_rh_iotxn_queue(xhci, txn, rh_index);
    }
    if (data->device_id > xhci->max_slots) {
         return ERR_INVALID_ARGS;
     }
    uint8_t ep_index = xhci_endpoint_index(data->ep_address);
    if (ep_index >= XHCI_NUM_EPS) {
         return ERR_INVALID_ARGS;
    }
    mx_paddr_t phys_addr;
    txn->ops->physmap(txn, &phys_addr);

    xhci_transfer_context_t* context = malloc(sizeof(xhci_transfer_context_t));
    if (!context) {
        return ERR_NO_MEMORY;
    }

    context->callback = xhci_iotxn_callback;
    context->data = txn;
    txn->context = context;
    usb_setup_t* setup = (ep_index == 0 ? &data->setup : NULL);
    uint8_t direction;
    if (setup) {
        direction = setup->bmRequestType & USB_ENDPOINT_DIR_MASK;
    } else {
        direction = data->ep_address & USB_ENDPOINT_DIR_MASK;
    }
    return xhci_queue_transfer(xhci, data->device_id, setup, phys_addr, txn->length,
                                 ep_index, direction, data->frame, context, &txn->node);
}

void xhci_process_deferred_txns(xhci_t* xhci, xhci_transfer_ring_t* ring, bool closed) {
    list_node_t list;
    list_node_t* node;
    iotxn_t* txn;

    list_initialize(&list);

    mtx_lock(&ring->mutex);
    // make a copy of deferred_txns list so we can operate on it safely outside of the mutex
    while ((node = list_remove_head(&ring->deferred_txns)) != NULL) {
        list_add_tail(&list, node);
    }
    list_initialize(&ring->deferred_txns);
    mtx_unlock(&ring->mutex);

    if (closed) {
        while ((txn = list_remove_head_type(&list, iotxn_t, node)) != NULL) {
            txn->ops->complete(txn, ERR_REMOTE_CLOSED, 0);
        }
        return;
    }

    // requeue all deferred transactions
    // this will either add them to the transfer ring or put them back on deferred_txns list
    while ((txn = list_remove_head_type(&list, iotxn_t, node)) != NULL) {
        mx_status_t status = xhci_do_iotxn_queue(xhci, txn);
        if (status != NO_ERROR && status != ERR_BUFFER_TOO_SMALL) {
            txn->ops->complete(txn, status, 0);
        }
    }
}

static void xhci_iotxn_queue(mx_device_t* hci_device, iotxn_t* txn) {
    usb_xhci_t* uxhci = dev_to_usb_xhci(hci_device);
    xhci_t* xhci = &uxhci->xhci;

    mx_status_t status = xhci_do_iotxn_queue(xhci, txn);
    if (status != NO_ERROR && status != ERR_BUFFER_TOO_SMALL) {
        txn->ops->complete(txn, status, 0);
    }
}

static void xhci_unbind(mx_device_t* dev) {
    xprintf("usb_xhci_unbind\n");
    usb_xhci_t* uxhci = dev_to_usb_xhci(dev);

    if (uxhci->bus_device) {
        device_remove(uxhci->bus_device);
    }
}

static mx_status_t xhci_release(mx_device_t* device) {
    // FIXME - do something here
    return NO_ERROR;
}

static mx_protocol_device_t xhci_device_proto = {
    .iotxn_queue = xhci_iotxn_queue,
    .unbind = xhci_unbind,
    .release = xhci_release,
};

static mx_status_t usb_xhci_bind(mx_driver_t* drv, mx_device_t* dev) {
    mx_handle_t irq_handle = MX_HANDLE_INVALID;
    mx_handle_t mmio_handle = MX_HANDLE_INVALID;
    mx_handle_t cfg_handle = MX_HANDLE_INVALID;
    io_alloc_t* io_alloc = NULL;
    usb_xhci_t* uxhci = NULL;
    mx_status_t status;

    pci_protocol_t* pci_proto;
    if (device_get_protocol(dev, MX_PROTOCOL_PCI, (void**)&pci_proto)) {
        status = ERR_NOT_SUPPORTED;
        goto error_return;
    }

    uxhci = calloc(1, sizeof(usb_xhci_t));
    if (!uxhci) {
        status = ERR_NO_MEMORY;
        goto error_return;
    }

    status = pci_proto->claim_device(dev);
    if (status < 0) {
        printf("usb_xhci_bind claim_device failed %d\n", status);
        goto error_return;
    }

    // create an IO memory allocator
    io_alloc = io_alloc_init(10 * 1024 * 1024);

    //printf("VID %04X %04X\n", pci_config->vendor_id, pci_config->device_id);

    // map our MMIO
    int bar = -1;
    void* mmio;
    uint64_t mmio_len;
    for (size_t i = 0; i < PCI_MAX_BAR_COUNT; i++) {
        mmio_handle = pci_proto->map_mmio(dev, i, MX_CACHE_POLICY_UNCACHED_DEVICE, &mmio, &mmio_len);
        if (mmio_handle >= 0) {
            bar = i;
            break;
        }
    }
    if (bar == -1) {
        printf("usb_xhci_bind could not find bar\n");
        status = ERR_INTERNAL;
        goto error_return;
    }

    // enable bus master
    status = pci_proto->enable_bus_master(dev, true);
    if (status < 0) {
        printf("usb_xhci_bind enable_bus_master failed %d\n", status);
        goto error_return;
    }

    // select our IRQ mode
    status = pci_proto->set_irq_mode(dev, MX_PCIE_IRQ_MODE_MSI, 1);
    if (status < 0) {
        mx_status_t status_legacy = pci_proto->set_irq_mode(dev, MX_PCIE_IRQ_MODE_LEGACY, 1);

        if (status_legacy < 0) {
            printf("usb_xhci_bind Failed to set IRQ mode to either MSI "
                   "(err = %d) or Legacy (err = %d)\n",
                   status, status_legacy);
            goto error_return;
        }

        uxhci->legacy_irq_mode = true;
    }

    // register for interrupts
    status = pci_proto->map_interrupt(dev, 0);
    if (status < 0) {
        printf("usb_xhci_bind map_interrupt failed %d\n", status);
        goto error_return;
    }
    irq_handle = status;

    uxhci->io_alloc = io_alloc;
    uxhci->irq_handle = irq_handle;
    uxhci->mmio_handle = mmio_handle;
    uxhci->cfg_handle = cfg_handle;
    uxhci->pci_proto = pci_proto;

    // stash this here for the startup thread to call device_add() with
    uxhci->parent = dev;

    device_init(&uxhci->device, drv, "usb-xhci", &xhci_device_proto);

    status = xhci_init(&uxhci->xhci, mmio);
    if (status < 0)
        goto error_return;

    uxhci->device.protocol_id = MX_PROTOCOL_USB_HCI;
    uxhci->device.protocol_ops = &xhci_hci_protocol;

    thrd_t thread;
    thrd_create_with_name(&thread, xhci_irq_thread, uxhci, "xhci_irq_thread");
    thrd_detach(thread);

    return NO_ERROR;

error_return:
    if (uxhci)
        free(uxhci);
    if (io_alloc)
        io_alloc_free(io_alloc);
    if (irq_handle != MX_HANDLE_INVALID)
        mx_handle_close(irq_handle);
    if (mmio_handle != MX_HANDLE_INVALID)
        mx_handle_close(mmio_handle);
    if (cfg_handle != MX_HANDLE_INVALID)
        mx_handle_close(cfg_handle);
    return status;
}

mx_driver_t _driver_usb_xhci = {
    .ops = {
        .bind = usb_xhci_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_usb_xhci, "usb-xhci", "magenta", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_CLASS, 0x0C),
    BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, 0x03),
    BI_MATCH_IF(EQ, BIND_PCI_INTERFACE, 0x30),
MAGENTA_DRIVER_END(_driver_usb_xhci)
