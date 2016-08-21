// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-alloc.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/usb-hci.h>

#include <hw/reg.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <runtime/thread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xhci.h"
#include "xhci-util.h"

//#define TRACE 1
#include "xhci-debug.h"

#define MAX_SLOTS 255

typedef struct usb_xhci {
    xhci_t xhci;
    // the device we implement
    mx_device_t device;

    // USB devices we created
    mx_device_t* devices[MAX_SLOTS];

    io_alloc_t* io_alloc;
    pci_protocol_t* pci_proto;
    bool legacy_irq_mode;
    mx_handle_t irq_handle;
    mx_handle_t mmio_handle;
    mx_handle_t cfg_handle;
    mxr_thread_t* irq_thread;
} usb_xhci_t;
#define xhci_to_usb_xhci(dev) containerof(dev, usb_xhci_t, xhci)
#define dev_to_usb_xhci(dev) containerof(dev, usb_xhci_t, device)

mx_status_t xhci_add_device(xhci_t* xhci, int slot_id, int speed,
                            usb_device_descriptor_t* device_descriptor,
                            usb_configuration_descriptor_t** config_descriptors) {
    usb_xhci_t* uxhci = xhci_to_usb_xhci(xhci);
    xprintf("xhci_add_new_device\n");

    mx_status_t result = usb_add_device(&uxhci->device, slot_id, speed,
                                        device_descriptor, config_descriptors,
                                        &uxhci->devices[slot_id - 1]);
    return result;
}

void xhci_remove_device(xhci_t* xhci, int slot_id) {
    usb_xhci_t* uxhci = xhci_to_usb_xhci(xhci);
    xprintf("xhci_remove_device %d\n", slot_id);
    int index = slot_id - 1;
    if (uxhci->devices[index]) {
        device_remove(uxhci->devices[index]);
        uxhci->devices[index] = NULL;
    }
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

    while (1) {
        mx_status_t wait_res;

        wait_res = uxhci->pci_proto->pci_wait_interrupt(uxhci->irq_handle);
        if (wait_res != NO_ERROR) {
            if (wait_res != ERR_CANCELLED)
                printf("unexpected pci_wait_interrupt failure (%d)\n", wait_res);
            break;
        }
        xhci_handle_interrupt(&uxhci->xhci, uxhci->legacy_irq_mode);
    }
    xprintf("xhci_irq_thread done\n");
    return 0;
}

static void xhci_transfer_callback(mx_status_t result, void* data) {
    usb_request_t* request = (usb_request_t*)data;
    if (result > 0) {
        request->transfer_length = result;
        request->status = NO_ERROR;
    } else {
        request->transfer_length = 0;
        request->status = result;
    }
    request->complete_cb(request);
}

usb_request_t* xhci_alloc_request(mx_device_t* device, uint16_t length) {
    usb_xhci_t* uxhci = dev_to_usb_xhci(device);
    usb_request_t* request = calloc(1, sizeof(usb_request_t));
    if (!request)
        return NULL;

    xhci_transfer_context_t* context = malloc(sizeof(xhci_transfer_context_t));
    if (!context) {
        free(request);
        return NULL;
    }
    context->callback = xhci_transfer_callback;
    context->data = request;
    request->driver_data = context;

    // buffers need not be aligned, but 64 byte alignment gives better performance
    request->buffer = (uint8_t*)xhci_memalign(&uxhci->xhci, 64, length);
    if (!request->buffer) {
        free(request->driver_data);
        free(request);
        return NULL;
    }
    request->buffer_length = length;
    return request;
}

void xhci_free_request(mx_device_t* device, usb_request_t* request) {
    usb_xhci_t* uxhci = dev_to_usb_xhci(device);
    if (request) {
        if (request->buffer) {
            xhci_free(&uxhci->xhci, request->buffer);
        }
        free(request->driver_data);
        free(request);
    }
}

int xhci_queue_request(mx_device_t* hci_device, int devaddr, usb_request_t* request) {
    usb_xhci_t* uxhci = dev_to_usb_xhci(hci_device);
    xhci_t* xhci = &uxhci->xhci;
    usb_endpoint_descriptor_t* ep = request->endpoint->descriptor;

    return xhci_queue_transfer(xhci, devaddr, NULL, request->buffer, request->transfer_length,
                               xhci_endpoint_index(ep), ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK,
                               (xhci_transfer_context_t*)request->driver_data);
}

int xhci_control(mx_device_t* hci_device, int devaddr, usb_setup_t* devreq, int data_length,
                 uint8_t* data) {
    usb_xhci_t* uxhci = dev_to_usb_xhci(hci_device);
    xhci_t* xhci = &uxhci->xhci;
    bool out = ((devreq->bmRequestType & USB_DIR_MASK) == USB_DIR_OUT);

    uint8_t* dma_buffer = NULL;
    if (data_length) {
        if (!data || data_length < 0)
            return ERR_INVALID_ARGS;
        dma_buffer = xhci_malloc(xhci, data_length);
        if (!dma_buffer)
            return ERR_NO_MEMORY;
        if (out) {
            memcpy(dma_buffer, data, data_length);
        }
    }

    int result = xhci_control_request(xhci, devaddr, devreq->bmRequestType, devreq->bRequest,
                                      devreq->wValue, devreq->wIndex, dma_buffer, data_length);
    if (result > 0 && !out) {
        if (result > data_length)
            result = data_length;
        memcpy(data, dma_buffer, result);
    }
    if (dma_buffer)
        xhci_free(xhci, dma_buffer);

    return result;
}

mx_status_t xhci_config_hub(mx_device_t* hci_device, int slot_id, usb_speed_t speed,
                            usb_hub_descriptor_t* descriptor) {
    usb_xhci_t* uxhci = dev_to_usb_xhci(hci_device);
    return xhci_configure_hub(&uxhci->xhci, slot_id, speed, descriptor);
}

mx_status_t xhci_hub_device_added(mx_device_t* hci_device, int hub_address, int port,
                                  usb_speed_t speed) {
    usb_xhci_t* uxhci = dev_to_usb_xhci(hci_device);
    return xhci_enumerate_device(&uxhci->xhci, hub_address, port, speed);
}

mx_status_t xhci_hub_device_removed(mx_device_t* hci_device, int hub_address, int port) {
    usb_xhci_t* uxhci = dev_to_usb_xhci(hci_device);
    xhci_device_disconnected(&uxhci->xhci, hub_address, port);
    return NO_ERROR;
}

usb_hci_protocol_t xhci_hci_protocol = {
    .alloc_request = xhci_alloc_request,
    .free_request = xhci_free_request,
    .queue_request = xhci_queue_request,
    .control = xhci_control,
    .configure_hub = xhci_config_hub,
    .hub_device_added = xhci_hub_device_added,
    .hub_device_removed = xhci_hub_device_removed,
};

static mx_status_t xhci_release(mx_device_t* device) {
    // FIXME - do something here
    return NO_ERROR;
}

static mx_protocol_device_t xhci_device_proto = {
    .release = xhci_release,
};

static int usb_xhci_start_thread(void* arg) {
    usb_xhci_t* uxhci = (usb_xhci_t*)arg;

    xhci_start(&uxhci->xhci);
    device_add(&uxhci->device, &uxhci->device);

    mxr_thread_create(xhci_irq_thread, uxhci, "xhci_irq_thread", &uxhci->irq_thread);
    return 0;
}

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

    const pci_config_t* pci_config;
    cfg_handle = pci_proto->get_config(dev, &pci_config);
    if (cfg_handle < 0) {
        printf("usb_xhci_bind failed to fetch PCI config (err %d)\n", cfg_handle);
        status = cfg_handle;
        goto error_return;
    }

    // create an IO memory allocator
    io_alloc = io_alloc_init(10 * 1024 * 1024);

    //printf("VID %04X %04X\n", pci_config->vendor_id, pci_config->device_id);

    // find our bar
    int bar = -1;
    for (size_t i = 0; i < countof(pci_config->base_addresses); i++) {
        if (pci_config->base_addresses[i]) {
            bar = i;
            break;
        }
    }
    if (bar == -1) {
        printf("usb_xhci_bind could not find bar\n");
        status = ERR_NOT_VALID;
        goto error_return;
    }

    // map our MMIO
    void* mmio;
    uint64_t mmio_len;
    mmio_handle = pci_proto->map_mmio(dev, bar, MX_CACHE_POLICY_UNCACHED_DEVICE, &mmio, &mmio_len);
    if (mmio_handle < 0) {
        status = mmio_handle;
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

    status = device_init(&uxhci->device, drv, "usb-xhci", &xhci_device_proto);
    if (status < 0)
        goto error_return;

    status = xhci_init(&uxhci->xhci, mmio);
    if (status < 0)
        goto error_return;

    uxhci->device.protocol_id = MX_PROTOCOL_USB_HCI;
    uxhci->device.protocol_ops = &xhci_hci_protocol;

    // start driver on a separate thread to avoid unnecessary blocking here
    mxr_thread_t* thread;
    mxr_thread_create(usb_xhci_start_thread, uxhci, "usb_xhci_start_thread", &thread);
    mxr_thread_detach(thread);

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

static mx_status_t usb_xhci_unbind(mx_driver_t* drv, mx_device_t* dev) {
    xprintf("usb_xhci_unbind\n");
    usb_xhci_t* uxhci = dev_to_usb_xhci(dev);

    for (int i = 0; i < MAX_SLOTS; i++) {
        if (uxhci->devices[i]) {
            device_remove(uxhci->devices[i]);
        }
    }
    return NO_ERROR;
}

static mx_bind_inst_t binding[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_CLASS, 0x0C),
    BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, 0x03),
    BI_MATCH_IF(EQ, BIND_PCI_INTERFACE, 0x30),
};

mx_driver_t _driver_usb_xhci BUILTIN_DRIVER = {
    .name = "usb-xhci",
    .ops = {
        .bind = usb_xhci_bind,
        .unbind = usb_xhci_unbind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
