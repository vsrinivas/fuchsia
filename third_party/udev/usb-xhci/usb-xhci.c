#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/usb-hci.h>

#include <ddk/io-alloc.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xhci-private.h"

/**
 * Driver for USB XHCI controller.
 */

static void* xhci_irq_thread(void* arg) {
    usb_xhci_t* xhci = (usb_xhci_t*)arg;
    printf("xhci_irq_thread start\n");

    while (xhci->pci->pci_wait_interrupt(xhci->irq_handle) == NO_ERROR) {
        printf("got interrupt\n");
    }
    printf("xhci_irq_thread done\n");
    return NULL;
}

static mx_status_t xhci_open(mx_device_t* dev, uint32_t flags) {
    printf("xhci_open\n");
    return NO_ERROR;
}

static mx_status_t xhci_close(mx_device_t* dev) {
    printf("xhci_close\n");
    return NO_ERROR;
}

static mx_status_t xhci_release(mx_device_t* dev) {
    printf("xhci_release\n");
    return NO_ERROR;
}

mx_status_t xhci_get_protocol(mx_device_t* dev, uint32_t proto_id, void** proto) {
    if (proto_id == MX_PROTOCOL_USB_HCI) {
        *proto = &_xhci_protocol;
        return NO_ERROR;
    }
    if (proto_id == MX_PROTOCOL_USB_HUB) {
        *proto = &xhci_rh_hub_protocol;
        return NO_ERROR;
    }
    return ERR_NOT_SUPPORTED;
}

static mx_protocol_device_t xhci_device_proto = {
    .get_protocol = xhci_get_protocol,
    .open = xhci_open,
    .close = xhci_close,
    .release = xhci_release,
};

static mx_status_t usb_xhci_probe(mx_driver_t* drv, mx_device_t* dev) {
    pci_protocol_t* pci;
    if (device_get_protocol(dev, MX_PROTOCOL_PCI, (void**)&pci)) {
        return ERR_NOT_SUPPORTED;
    }

    const pci_config_t* pci_config;
    mx_status_t status;
    mx_handle_t cfg_handle = pci->get_config(dev, &pci_config);
    if (cfg_handle < 0) {
        printf("usb_xhci_probe failed to map config (err = %d)\n", cfg_handle);
        status = cfg_handle;
    } else {
        if (pci_config->base_class == 0x0c &&
            pci_config->sub_class == 0x03 &&
            pci_config->program_interface == 0x30) {
            printf("probe found XHCI\n");
            status = NO_ERROR;
        } else {
            status = ERR_NOT_SUPPORTED;
        }

        _magenta_handle_close(cfg_handle);
    }

    return status;
}

static mx_status_t usb_xhci_bind(mx_driver_t* drv, mx_device_t* dev) {
    mx_handle_t irq_handle = MX_HANDLE_INVALID;
    mx_handle_t mmio_handle = MX_HANDLE_INVALID;
    mx_handle_t cfg_handle = MX_HANDLE_INVALID;
    io_alloc_t* io_alloc = NULL;
    usb_xhci_t* xhci = NULL;

    pci_protocol_t* pci;
    if (device_get_protocol(dev, MX_PROTOCOL_PCI, (void**)&pci)) {
        return ERR_NOT_SUPPORTED;
    }

    mx_status_t status = pci->claim_device(dev);
    if (status < 0) {
        printf("usb_xhci_bind claim_device failed %d\n", status);
        return status;
    }

    const pci_config_t* pci_config;
    cfg_handle = pci->get_config(dev, &pci_config);
    if (cfg_handle < 0) {
        printf("usb_xhci_bind failed to fetch PCI config (err %d)\n", cfg_handle);
        status = cfg_handle;
        goto error_return;
    }

    // create an IO memory allocator
    io_alloc = io_alloc_init(1024 * 1024);

    // find our bar
    int bar = -1;
    for (uint i = 0; i < countof(pci_config->base_addresses); i++) {
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
    mmio_handle = pci->map_mmio(dev, bar, MX_CACHE_POLICY_UNCACHED_DEVICE, &mmio, &mmio_len);
    if (mmio_handle < 0) {
        status = mmio_handle;
        goto error_return;
    }

    // enable bus master
    status = pci->enable_bus_master(dev, true);
    if (status < 0) {
        printf("usb_xhci_bind enable_bus_master failed %d\n", status);
        goto error_return;
    }

    // register for interrupts
    status = pci->map_interrupt(dev, 0);
    printf("map_interrupt returned %d\n", status);
    if (status < 0) {
        printf("usb_xhci_bind map_interrupt failed %d\n", status);
        goto error_return;
    }
    irq_handle = status;

    xhci = malloc(sizeof(usb_xhci_t));
    if (!xhci) {
        status = ERR_NO_MEMORY;
        goto error_return;
    }

    xhci->io_alloc = io_alloc;
    xhci->mmio = mmio;
    xhci->mmio_len = mmio_len;
    xhci->irq_handle = irq_handle;
    xhci->mmio_handle = mmio_handle;
    xhci->cfg_handle = cfg_handle;
    xhci->pci = pci;
    status = xhci_startup(xhci);
    if (status < 0)
        goto error_return;

    mx_device_t* hcidev = &xhci->hcidev;
    status = device_init(hcidev, drv, "xhci_usb", &xhci_device_proto);
    hcidev->protocol_id = MX_PROTOCOL_USB_HCI;
    if (status < 0)
        goto error_return;

    device_add(hcidev, dev);

    pthread_create(&xhci->irq_thread, NULL, xhci_irq_thread, xhci);
    usb_poll_start();

    return NO_ERROR;

error_return:
    if (xhci)
        free(xhci);
    if (io_alloc)
        io_alloc_free(io_alloc);
    if (irq_handle != MX_HANDLE_INVALID)
        _magenta_handle_close(irq_handle);
    if (mmio_handle != MX_HANDLE_INVALID)
        _magenta_handle_close(mmio_handle);
    if (cfg_handle != MX_HANDLE_INVALID)
        _magenta_handle_close(cfg_handle);
    return status;
}

static mx_status_t usb_xhci_unbind(mx_driver_t* drv, mx_device_t* dev) {
    mx_device_t* child = NULL;
    mx_device_t* temp = NULL;
    list_for_every_entry_safe (&dev->device_list, child, temp, mx_device_t, node) {
        device_remove(child);
    }
    return NO_ERROR;
}

static mx_driver_binding_t binding = {
    .protocol_id = MX_PROTOCOL_PCI,
};

mx_driver_t _driver_usb_xhci BUILTIN_DRIVER = {
    .name = "usb_xhci",
    .ops = {
        .probe = usb_xhci_probe,
        .bind = usb_xhci_bind,
        .unbind = usb_xhci_unbind,
    },
    .binding = &binding,
    .binding_count = 1,
};
