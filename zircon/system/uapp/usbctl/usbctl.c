// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/hardware/usb/peripheral/c/fidl.h>
#include <lib/fdio/util.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <lib/fdio/util.h>
#include <ddk/protocol/usb/modeswitch.h>
#include <fuchsia/usb/virtualbus/c/fidl.h>

#include <zircon/device/usb-peripheral.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/cdc.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <zircon/types.h>

#define DEV_USB_PERIPHERAL_DIR  "/dev/class/usb-peripheral"
#define DEV_VIRTUAL_USB "/dev/test/usb-virtual-bus"

#define MANUFACTURER_STRING "Zircon"
#define CDC_PRODUCT_STRING  "CDC Ethernet"
#define UMS_PRODUCT_STRING  "USB Mass Storage"
#define TEST_PRODUCT_STRING  "USB Function Test"
#define SERIAL_STRING       "12345678"

typedef fuchsia_hardware_usb_peripheral_FunctionDescriptor usb_function_descriptor_t;

const usb_function_descriptor_t cdc_function_desc = {
    .interface_class = USB_CLASS_COMM,
    .interface_subclass = USB_CDC_SUBCLASS_ETHERNET,
    .interface_protocol = 0,
};

const usb_function_descriptor_t ums_function_desc = {
    .interface_class = USB_CLASS_MSC,
    .interface_subclass = USB_SUBCLASS_MSC_SCSI,
    .interface_protocol = USB_PROTOCOL_MSC_BULK_ONLY,
};

const usb_function_descriptor_t test_function_desc = {
    .interface_class = USB_CLASS_VENDOR,
    .interface_subclass = 0,
    .interface_protocol = 0,
};

typedef struct {
    const usb_function_descriptor_t* desc;
    const char* product_string;
    uint16_t vid;
    uint16_t pid;
} usb_function_t;

static const usb_function_t cdc_function = {
    .desc = &cdc_function_desc,
    .product_string = CDC_PRODUCT_STRING,
    .vid = GOOGLE_USB_VID,
    .pid = GOOGLE_USB_CDC_PID,
};

static const usb_function_t ums_function = {
    .desc = &ums_function_desc,
    .product_string = UMS_PRODUCT_STRING,
    .vid = GOOGLE_USB_VID,
    .pid = GOOGLE_USB_UMS_PID,
};

static const usb_function_t test_function = {
    .desc = &test_function_desc,
    .product_string = TEST_PRODUCT_STRING,
    .vid = GOOGLE_USB_VID,
    .pid = GOOGLE_USB_PERIPHERAL_TEST_PID,
};

static fuchsia_hardware_usb_peripheral_DeviceDescriptor device_desc = {
    .bcdUSB = htole16(0x0200),
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = 64,
    //   idVendor and idProduct are filled in later
    .bcdDevice = htole16(0x0100),
    //    iManufacturer, iProduct and iSerialNumber are filled in later
    .bNumConfigurations = 1,
};

static int open_usb_device(int* out_fd, zx_handle_t* out_svc) {
    struct dirent* de;
    DIR* dir = opendir(DEV_USB_PERIPHERAL_DIR);
    if (!dir) {
        printf("Error opening %s\n", DEV_USB_PERIPHERAL_DIR);
        return -1;
    }

    while ((de = readdir(dir)) != NULL) {
       char devname[128];

        snprintf(devname, sizeof(devname), "%s/%s", DEV_USB_PERIPHERAL_DIR, de->d_name);
        int fd = open(devname, O_RDWR);
        if (fd < 0) {
            printf("Error opening %s\n", devname);
            continue;
        }

        closedir(dir);

        zx_status_t status = fdio_get_service_handle(fd, out_svc);
        if (status != ZX_OK) {
            close(fd);
            return status;
        }
        *out_fd = fd;
        return ZX_OK;
    }

    closedir(dir);
    return ZX_ERR_NOT_FOUND;
}

static int open_usb_virtual_bus(int* out_fd, zx_handle_t* out_svc) {
    int fd = open(DEV_VIRTUAL_USB, O_RDWR);
    if (fd < 0) {
        printf("Error opening %s\n", DEV_VIRTUAL_USB);
        return ZX_ERR_NOT_FOUND;
    }

    zx_status_t status = fdio_get_service_handle(fd, out_svc);
    if (status != ZX_OK) {
        close(fd);
        return status;
    }
    *out_fd = fd;
    return ZX_OK;
}

static void close_device(int fd, zx_handle_t svc) {
    zx_handle_close(svc);
    close(fd);
}

static zx_status_t device_init(zx_handle_t svc, const usb_function_t* function) {
    zx_status_t status2;

    device_desc.idVendor = htole16(function->vid);
    device_desc.idProduct = htole16(function->pid);

    // allocate string descriptors
    zx_status_t status = fuchsia_hardware_usb_peripheral_DeviceAllocStringDesc(
        svc, MANUFACTURER_STRING, strlen(MANUFACTURER_STRING) + 1, &status2,
        &device_desc.iManufacturer);
    if (status == ZX_OK) status = status2;
    if (status != ZX_OK) {
        fprintf(stderr, "fuchsia_hardware_usb_peripheral_DeviceAllocStringDesc failed: %d\n",
                status);
        return status;
    }
    status = fuchsia_hardware_usb_peripheral_DeviceAllocStringDesc(
        svc, function->product_string, strlen(function->product_string) + 1, &status2,
        &device_desc.iProduct);
    if (status == ZX_OK) status = status2;
    if (status != ZX_OK) {
        fprintf(stderr, "fuchsia_hardware_usb_peripheral_DeviceAllocStringDesc failed: %d\n",
                status);
        return status;
    }
    status = fuchsia_hardware_usb_peripheral_DeviceAllocStringDesc(
        svc, SERIAL_STRING, strlen(SERIAL_STRING) + 1, &status2, &device_desc.iSerialNumber);
    if (status == ZX_OK) status = status2;
    if (status != ZX_OK) {
        fprintf(stderr, "fuchsia_hardware_usb_peripheral_DeviceAllocStringDesc failed: %d\n",
                status);
        return status;
    }

    // set device descriptor
    status = fuchsia_hardware_usb_peripheral_DeviceSetDeviceDescriptor(svc, &device_desc, &status2);
    if (status == ZX_OK) status = status2;
    if (status != ZX_OK) {
        fprintf(stderr, "fuchsia_hardware_usb_peripheral_DeviceSetDeviceDescriptor failed: %d\n",
                status);
        return status;
    }

    status = fuchsia_hardware_usb_peripheral_DeviceAddFunction(svc, function->desc, &status2);
    if (status == ZX_OK) status = status2;
    if (status != ZX_OK) {
        fprintf(stderr, "fuchsia_hardware_usb_peripheral_DeviceAddFunction failed: %d\n", status);
        return status;
    }

    status = fuchsia_hardware_usb_peripheral_DeviceBindFunctions(svc, &status2);
    if (status == ZX_OK) status = status2;
    if (status != ZX_OK) {
        fprintf(stderr, "fuchsia_hardware_usb_peripheral_DeviceBindFunctions failed: %d\n", status);
    }

    return status;
}

static int ums_command(int argc, const char* argv[]) {
    int fd;
    zx_handle_t svc;
    zx_status_t status = open_usb_device(&fd, &svc);
    if (status != ZX_OK) {
        return status;
    }

    zx_status_t status2;

    status = fuchsia_hardware_usb_peripheral_DeviceClearFunctions(svc, &status2);
    if (status == ZX_OK) status = status2;
    if (status == ZX_OK) {
        status = device_init(svc, &ums_function);
    }

    close_device(fd, svc);
    return status == ZX_OK ? 0 : -1;
}

static int cdc_command(int argc, const char* argv[]) {
    int fd;
    zx_handle_t svc;
    zx_status_t status = open_usb_device(&fd, &svc);
    if (status != ZX_OK) {
        return status;
    }

    zx_status_t status2;

    status = fuchsia_hardware_usb_peripheral_DeviceClearFunctions(svc, &status2);
    if (status == ZX_OK) status = status2;
    if (status == ZX_OK) {
        status = device_init(svc, &cdc_function);
    }

    close_device(fd, svc);
    return status == ZX_OK ? 0 : -1;
}

static int test_command(int argc, const char* argv[]) {
    int fd;
    zx_handle_t svc;
    zx_status_t status = open_usb_device(&fd, &svc);
    if (status != ZX_OK) {
        return status;
    }

    zx_status_t status2;

    status = fuchsia_hardware_usb_peripheral_DeviceClearFunctions(svc, &status2);
    if (status == ZX_OK) status = status2;
    if (status == ZX_OK) {
        status = device_init(svc, &test_function);
    }

    close_device(fd, svc);
    return status == ZX_OK ? 0 : -1;
}

static int mode_command(int argc, const char* argv[]) {
    int fd;
    zx_handle_t svc;
    zx_status_t status = open_usb_device(&fd, &svc);
    if (status != ZX_OK) {
        return status;
    }

    zx_status_t status2;

    if (argc == 1) {
        // print current mode
        usb_mode_t mode;
        status = fuchsia_hardware_usb_peripheral_DeviceGetMode(svc, &status2, &mode);
        if (status == ZX_OK) status = status2;
        if (status != ZX_OK) {
            fprintf(stderr, "fuchsia_hardware_usb_peripheral_DeviceGetMode failed: %d\n", status);
        } else {
            switch (mode) {
            case USB_MODE_NONE:
                printf("NONE\n");
                break;
            case USB_MODE_HOST:
                printf("HOST\n");
                break;
            case USB_MODE_PERIPHERAL:
                printf("PERIPHERAL\n");
                break;
            case USB_MODE_OTG:
                printf("OTG\n");
                break;
            default:
                printf("unknown mode %d\n", mode);
                break;
            }
         }
    } else {
        usb_mode_t mode;
        if (strcasecmp(argv[1], "none") == 0) {
            mode = USB_MODE_NONE;
        } else if (strcasecmp(argv[1], "host") == 0) {
            mode = USB_MODE_HOST;
        } else if (strcasecmp(argv[1], "peripheral") == 0) {
            mode = USB_MODE_PERIPHERAL;
        } else if (strcasecmp(argv[1], "otg") == 0) {
            mode = USB_MODE_OTG;
        } else {
            fprintf(stderr, "unknown USB mode %s\n", argv[1]);
            status = ZX_ERR_INVALID_ARGS;
        }

        if (status == ZX_OK) {
            status = fuchsia_hardware_usb_peripheral_DeviceSetMode(svc, mode, &status2);
            if (status == ZX_OK) status = status2;
            if (status != ZX_OK) {
                fprintf(stderr, "fuchsia_hardware_usb_peripheral_DeviceSetMode failed: %d\n",
                        status);
            }
        }
    }

    close_device(fd, svc);
    return status;
}


static int virtual_command(int argc, const char* argv[]) {
    int fd;
    zx_handle_t svc;
    zx_status_t status = open_usb_virtual_bus(&fd, &svc);
    if (status != ZX_OK) {
        return status;
    }

    zx_status_t status2;

    if (argc != 2) {
        goto usage;
    }

    const char* command = argv[1];
    if (!strcmp(command, "enable")) {
        status = fuchsia_usb_virtualbus_BusEnable(svc, &status2);
    } else if (!strcmp(command, "disable")) {
        status = fuchsia_usb_virtualbus_BusDisable(svc, &status2);
    } else if (!strcmp(command, "connect")) {
        status = fuchsia_usb_virtualbus_BusConnect(svc, &status2);
    } else if (!strcmp(command, "disconnect")) {
        status = fuchsia_usb_virtualbus_BusDisconnect(svc, &status2);
    } else {
        goto usage;
    }

    if (status == ZX_OK) status = status2;

    close_device(fd, svc);
    return status;

usage:
    close_device(fd, svc);
    fprintf(stderr, "usage: usbctl virtual [enable|disable|connect|disconnect]\n");
    return -1;
}

typedef struct {
    const char* name;
    int (*command)(int argc, const char* argv[]);
    const char* description;
} usbctl_command_t;

static usbctl_command_t commands[] = {
    {
        "init-ums",
        ums_command,
        "init-ums - initializes the USB Mass Storage function"
    },
    {
        "init-cdc",
        cdc_command,
        "init-cdc - initializes the CDC Ethernet function"
    },
    {
        "init-test",
        test_command,
        "init-test - initializes the USB Peripheral Test function"
    },
    {
        "mode",
        mode_command,
        "mode [none|host|peripheral|otg] - sets the current USB mode. "
        "Returns the current mode if no additional arugment is provided."
    },
    {
        "virtual",
        virtual_command,
        "virtual [enable|disable|connect|disconnect] - controls USB virtual bus"
    },
    { NULL, NULL, NULL },
};

static void usage(void) {
    fprintf(stderr, "usage: \"usbctl <command>\", where command is one of:\n");

    usbctl_command_t* command = commands;
    while (command->name) {
        fprintf(stderr, "    %s\n", command->description);
        command++;
    }
}

int main(int argc, const char** argv) {
    if (argc < 2) {
        usage();
        return -1;
    }

    const char* command_name = argv[1];
    usbctl_command_t* command = commands;
    zx_status_t status;
    while (command->name) {
        if (!strcmp(command_name, command->name)) {
            status = command->command(argc - 1, argv + 1);
            goto done;
        }
        command++;
    }
    // if we fall through, print usage
    usage();
    status = ZX_ERR_INVALID_ARGS;

done:
    return status;
}
