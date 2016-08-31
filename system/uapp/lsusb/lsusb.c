// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/types.h>
#include <mxio/io.h>
#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include <hexdump/hexdump.h>
#include <magenta/hw/usb.h>
#include <magenta/device/usb.h>

#define DEV_USB "/dev/class/usb"

static void get_string_desc(int fd, int index, char* buf, int buflen) {
    buf[0] = 0;
    mxio_ioctl(fd, IOCTL_USB_GET_STRING_DESC, &index, sizeof(index), buf, buflen);
}

static int list_device(const char* device_id, bool verbose) {
    char devname[128];
    usb_device_descriptor_t device_desc;
    char manufacturer[256];
    char product[256];
    manufacturer[0] = 0;
    product[0] = 0;
    int ret = 0;

    snprintf(devname, sizeof(devname), "%s/%s", DEV_USB, device_id);
    int fd = open(devname, O_RDONLY);
    if (fd < 0) {
        printf("Error opening %s\n", devname);
        return fd;
    }

    int device_type;
    ret = mxio_ioctl(fd, IOCTL_USB_GET_DEVICE_TYPE, NULL, 0, &device_type, sizeof(device_type));
    if (ret != sizeof(device_type)) {
        printf("IOCTL_USB_GET_DEVICE_TYPE failed for %s\n", devname);
        goto out;
    }
    if (device_type != USB_DEVICE_TYPE_DEVICE) {
        goto out;
    }

    ret = mxio_ioctl(fd, IOCTL_USB_GET_DEVICE_DESC, NULL, 0, &device_desc, sizeof(device_desc));
    if (ret != sizeof(device_desc)) {
        printf("IOCTL_USB_GET_DEVICE_DESC failed for %s\n", devname);
        goto out;
    }

    get_string_desc(fd, device_desc.iManufacturer, manufacturer, sizeof(manufacturer));
    get_string_desc(fd, device_desc.iProduct, product, sizeof(product));

    printf("%s %04X:%04X %s %s\n", devname, le16toh(device_desc.idVendor), le16toh(device_desc.idProduct),
           manufacturer, product);

    if (verbose) {
        char string_buf[256];

        // print device descriptor
        printf("Device Descriptor:\n");
        printf("  bLength                   %d\n", device_desc.bLength);
        printf("  bDescriptorType           %d\n", device_desc.bDescriptorType);
        printf("  bcdUSB                    %x.%x\n", le16toh(device_desc.bcdUSB) >> 8,
                                                      le16toh(device_desc.bcdUSB) & 0xFF);
        printf("  bDeviceClass              %d\n", device_desc.bDeviceClass);
        printf("  bDeviceSubClass           %d\n", device_desc.bDeviceSubClass);
        printf("  bDeviceProtocol           %d\n", device_desc.bDeviceProtocol);
        printf("  bMaxPacketSize0           %d\n", device_desc.bMaxPacketSize0);
        printf("  idVendor                  0x%04X\n", le16toh(device_desc.idVendor));
        printf("  idProduct                 0x%04X\n", le16toh(device_desc.idProduct));
        printf("  bcdDevice                 %x.%x\n", le16toh(device_desc.bcdDevice) >> 8,
                                                      le16toh(device_desc.bcdDevice) & 0xFF);
        printf("  iManufacturer             %d %s\n", device_desc.iManufacturer, manufacturer);
        printf("  iProduct                  %d %s\n", device_desc.iProduct, product);
        get_string_desc(fd, device_desc.iSerialNumber, string_buf, sizeof(string_buf));
        printf("  iSerialNumber             %d %s\n", device_desc.iSerialNumber, string_buf);
        printf("  bNumConfigurations        %d\n", device_desc.bNumConfigurations);

        int desc_size;
        ret = mxio_ioctl(fd, IOCTL_USB_GET_CONFIG_DESC_SIZE, NULL, 0, &desc_size, sizeof(desc_size));
        if (ret != sizeof(desc_size)) {
            printf("IOCTL_USB_GET_CONFIG_DESC_SIZE failed for %s\n", devname);
            goto out;
        }

        uint8_t* desc = malloc(desc_size);
        if (!desc) {
            ret = -1;
            goto out;
        }
        ret = mxio_ioctl(fd, IOCTL_USB_GET_CONFIG_DESC, NULL, 0, desc, desc_size);
        if (ret != desc_size) {
            printf("IOCTL_USB_GET_CONFIG_DESC failed for %s\n", devname);
            goto free_out;
        }

        usb_descriptor_header_t* desc_end = (usb_descriptor_header_t *)(desc + desc_size);

        // print configuration descriptor
        usb_configuration_descriptor_t* config_desc = (usb_configuration_descriptor_t *)desc;
        printf("  Configuration Descriptor:\n");
        printf("    bLength                   %d\n", config_desc->bLength);
        printf("    bDescriptorType           %d\n", config_desc->bDescriptorType);
        printf("    wTotalLength              %d\n", le16toh(config_desc->wTotalLength));
        printf("    bNumInterfaces            %d\n", config_desc->bNumInterfaces);
        printf("    bConfigurationValue       %d\n", config_desc->bConfigurationValue);
        get_string_desc(fd, config_desc->bConfigurationValue, string_buf, sizeof(string_buf));
        printf("    iConfiguration            %d %s\n", config_desc->bConfigurationValue, string_buf);
        printf("    bmAttributes              0x%02X\n", config_desc->bmAttributes);
        printf("    bMaxPower                 %d\n", config_desc->bMaxPower);

        // print remaining descriptors
        usb_descriptor_header_t* header = (usb_descriptor_header_t *)(desc + sizeof(usb_configuration_descriptor_t));
        while (header < desc_end) {
            if (header->bLength == 0) {
                printf("zero length header, bailing\n");
                break;
            }
            if (header->bDescriptorType == USB_DT_INTERFACE) {
                usb_interface_descriptor_t* desc = (usb_interface_descriptor_t *)header;
                printf("    Interface Descriptor:\n");
                printf("      bLength                   %d\n", desc->bLength);
                printf("      bDescriptorType           %d\n", desc->bDescriptorType);
                printf("      bInterfaceNumber          %d\n", desc->bInterfaceNumber);
                printf("      bAlternateSetting         %d\n", desc->bAlternateSetting);
                printf("      bNumEndpoints             %d\n", desc->bNumEndpoints);
                printf("      bInterfaceClass           %d\n", desc->bInterfaceClass);
                printf("      bInterfaceSubClass        %d\n", desc->bInterfaceSubClass);
                printf("      bInterfaceProtocol        %d\n", desc->bInterfaceProtocol);
                get_string_desc(fd, desc->iInterface, string_buf, sizeof(string_buf));
                printf("      iInterface                %d %s\n", desc->iInterface, string_buf);
            } else if (header->bDescriptorType == USB_DT_ENDPOINT) {
                usb_endpoint_descriptor_t* desc = (usb_endpoint_descriptor_t *)header;
                printf("      Endpoint Descriptor:\n");
                printf("        bLength                   %d\n", desc->bLength);
                printf("        bDescriptorType           %d\n", desc->bDescriptorType);
                printf("        bEndpointAddress          0x%02X\n", desc->bEndpointAddress);
                printf("        bmAttributes              0x%02X\n", desc->bmAttributes);
                printf("        wMaxPacketSize            %d\n", le16toh(desc->wMaxPacketSize));
                printf("        bInterval                 %d\n", desc->bInterval);
            } else {
                // FIXME - support other descriptor types
                printf("      Unknown Descriptor:\n");
                printf("        bLength                   %d\n", header->bLength);
                printf("        bDescriptorType           %d\n", header->bDescriptorType);
                hexdump8_ex(header, header->bLength, 0);
            }

            header = (usb_descriptor_header_t *)((uint8_t *)header + header->bLength);
        }

free_out:
        free(desc);
    }

out:
    close(fd);
    return ret;
}

static int list_devices(bool verbose) {
    struct dirent* de;
    DIR* dir = opendir(DEV_USB);
    if (!dir) {
        printf("Error opening %s\n", DEV_USB);
        return -1;
    }

    while ((de = readdir(dir)) != NULL) {
        list_device(de->d_name, verbose);
    }

    closedir(dir);
    return 0;
}

int main(int argc, const char** argv) {
    int result = 0;
    bool verbose = false;
    const char* device_id = NULL;

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (!strcmp(arg, "-v")) {
            verbose = true;
        } else if (!strcmp(arg, "-d")) {
            if (++i == argc) {
                printf("device ID required after -d option\n");
                result = -1;
                goto usage;
            }
            device_id = argv[i];
        } else if (!strcmp(arg, "-d")) {
            goto usage;
        } else {
            printf("unknown option \"%s\"\n", arg);
            result = -1;
            goto usage;
        }
    }

    if (device_id) {
        return list_device(device_id, verbose);
    } else {
        return list_devices(verbose);
    }

usage:
    printf("Usage:\n");
    printf("%s [-v] [-d <device ID>]", argv[0]);
    printf("  -v   Verbose output (prints descriptors\n");
    printf("  -d   Prints only specified device\n");
    return result;
}
