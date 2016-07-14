#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
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

    while (1) {
        mx_status_t wait_res;

        wait_res = xhci->pci->pci_wait_interrupt(xhci->irq_handle);
        if (wait_res != NO_ERROR) {
            if (wait_res != ERR_CANCELLED)
                printf("unexpected pci_wait_interrupt failure (%d)\n", wait_res);
            break;
        }

        xhci_poll(&xhci->xhci);

        // acknowledge everything
        uint32_t tmp = xhci->xhci.opreg->usbsts;
        tmp &= USBSTS_HCH
             | USBSTS_HSE
             | USBSTS_EINT
             | USBSTS_PCD
             | USBSTS_SSS
             | USBSTS_RSS
             | USBSTS_SRE
             | USBSTS_CNR
             | USBSTS_HCE
             | USBSTS_PRSRV_MASK;
        xhci->xhci.opreg->usbsts = tmp;

        // If we are in legacy IRQ mode, clear the IP (Interrupt Pending) bit
        // from the IMAN register of our one-and-only interrupter.
        if (xhci->legacy_irq_mode)
            xhci->xhci.hcrreg->intrrs[0].iman |= IMAN_IP;
    }
    printf("xhci_irq_thread done\n");
    return NULL;
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
};

static mx_status_t usb_xhci_bind(mx_driver_t* drv, mx_device_t* dev) {
    mx_handle_t irq_handle = MX_HANDLE_INVALID;
    mx_handle_t mmio_handle = MX_HANDLE_INVALID;
    mx_handle_t cfg_handle = MX_HANDLE_INVALID;
    io_alloc_t* io_alloc = NULL;
    usb_xhci_t* xhci = NULL;
    mx_status_t status;

    pci_protocol_t* pci;
    if (device_get_protocol(dev, MX_PROTOCOL_PCI, (void**)&pci)) {
        status = ERR_NOT_SUPPORTED;
        goto error_return;
    }

    xhci = calloc(1, sizeof(usb_xhci_t));
    if (!xhci) {
        status = ERR_NO_MEMORY;
        goto error_return;
    }

    status = pci->claim_device(dev);
    if (status < 0) {
        printf("usb_xhci_bind claim_device failed %d\n", status);
        goto error_return;
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

    // select our IRQ mode
    status = pci->set_irq_mode(dev, MX_PCIE_IRQ_MODE_MSI, 1);
    if (status < 0) {
        mx_status_t status_legacy = pci->set_irq_mode(dev, MX_PCIE_IRQ_MODE_LEGACY, 1);

        if (status_legacy < 0) {
            printf("usb_xhci_bind Failed to set IRQ mode to either MSI "
                   "(err = %d) or Legacy (err = %d)\n",
                   status, status_legacy);
            goto error_return;
        }

        xhci->legacy_irq_mode = true;
    }

    // register for interrupts
    status = pci->map_interrupt(dev, 0);
    if (status < 0) {
        printf("usb_xhci_bind map_interrupt failed %d\n", status);
        goto error_return;
    }
    irq_handle = status;

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
    if (status < 0)
        goto error_return;

    hcidev->protocol_id = MX_PROTOCOL_USB_HCI;
    hcidev->protocol_ops = &_xhci_protocol;

    device_add(hcidev, dev);

    pthread_create(&xhci->irq_thread, NULL, xhci_irq_thread, xhci);

    return NO_ERROR;

error_return:
    if (xhci)
        free(xhci);
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
    //TODO: should avoid using dev->childern
    mx_device_t* child = NULL;
    mx_device_t* temp = NULL;
    list_for_every_entry_safe (&dev->children, child, temp, mx_device_t, node) {
        device_remove(child);
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
    .name = "usb_xhci",
    .ops = {
        .bind = usb_xhci_bind,
        .unbind = usb_xhci_unbind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
