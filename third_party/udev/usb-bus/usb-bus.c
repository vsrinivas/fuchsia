#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/usb-bus.h>
#include <ddk/protocol/usb-hci.h>
#include <stdlib.h>
#include <string.h>

#include "generic-hub.h"
#include "usb-device.h"
#include "usb-private.h"

typedef struct usb_bus {
    mx_device_t device;

    // device's HCI controller and protocol
    mx_device_t* hcidev;
    usb_hci_protocol_t* hci_protocol;

    // for root hub
    generic_hub_t generic_hub;
} usb_bus_t;
#define get_usb_bus(dev) containerof(dev, usb_bus_t, device)

static mx_device_t* usb_attach_device(mx_device_t* busdev, mx_device_t* hubdev, int hubaddress,
                                      int port, usb_speed speed) {
    static const char* speeds[] = {"full", "low", "high", "super"};
    usb_debug("%sspeed device\n", (speed < sizeof(speeds) / sizeof(char*))
                                      ? speeds[speed]
                                      : "invalid value - no");
    usb_bus_t* bus = get_usb_bus(busdev);

    int address = bus->hci_protocol->set_address(bus->hcidev, speed, port, hubaddress);
    if (address < 0) {
        return NULL;
    }

    mx_device_t* device = usb_create_device(bus->hcidev, address, speed);
    if (device) {
        device_add(device, &bus->device);
    }
    return device;
}

static void usb_detach_device(mx_device_t* busdev, mx_device_t* device) {
    usb_bus_t* bus = get_usb_bus(busdev);

    usb_device_protocol_t* device_protocol;
    device_get_protocol(device, MX_PROTOCOL_USB_DEVICE, (void**)&device_protocol);
    bus->hci_protocol->destroy_device(bus->hcidev, device_protocol->get_address(device));
}

void usb_root_hub_port_changed(mx_device_t* busdev, int port) {
    usb_bus_t* bus = get_usb_bus(busdev);
    generic_hub_scanport(&bus->generic_hub, port);
}

usb_bus_protocol_t _bus_protocol = {
    .attach_device = usb_attach_device,
    .detach_device = usb_detach_device,
    .root_hub_port_changed = usb_root_hub_port_changed,
};

static mx_protocol_device_t usb_bus_device_proto = {
};

static mx_status_t usb_bus_bind(mx_driver_t* driver, mx_device_t* device) {
    usb_hci_protocol_t* hci_protocol;
    usb_hub_protocol_t* hub_protocol;
    if (device_get_protocol(device, MX_PROTOCOL_USB_HCI, (void**)&hci_protocol)) {
        return ERR_NOT_SUPPORTED;
    }
    if (device_get_protocol(device, MX_PROTOCOL_USB_HUB, (void**)&hub_protocol)) {
        return ERR_NOT_SUPPORTED;
    }

    usb_bus_t* bus = calloc(1, sizeof(usb_bus_t));
    if (!bus) {
        printf("Not enough memory for usb_bus_t.\n");
        return ERR_NO_MEMORY;
    }

    bus->hcidev = device;
    bus->hci_protocol = hci_protocol;

    mx_status_t status = device_init(&bus->device, driver, "usb_bus", &usb_bus_device_proto);
    if (status != NO_ERROR) {
        free(bus);
        return status;
    }

    bus->device.protocol_id = MX_PROTOCOL_USB_BUS;
    bus->device.protocol_ops = &_bus_protocol;
    device_set_bindable(&bus->device, false);
    device_add(&bus->device, device);

    hci_protocol->set_bus_device(device, &bus->device);
    generic_hub_init(&bus->generic_hub, device, hub_protocol, &bus->device, 0);

    return NO_ERROR;
}

static mx_status_t usb_bus_unbind(mx_driver_t* drv, mx_device_t* dev) {
    usb_bus_t* bus = get_usb_bus(dev);
    bus->hci_protocol->set_bus_device(bus->hcidev, NULL);

    //TODO: should avoid using dev->childern
    mx_device_t* child = NULL;
    mx_device_t* temp = NULL;
    list_for_every_entry_safe (&dev->children, child, temp, mx_device_t, node) {
        device_remove(child);
    }
    return NO_ERROR;
}

static mx_bind_inst_t binding[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_USB_HCI),
};

mx_driver_t _driver_usb_bus BUILTIN_DRIVER = {
    .name = "usb_bus",
    .ops = {
        .bind = usb_bus_bind,
        .unbind = usb_bus_unbind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
