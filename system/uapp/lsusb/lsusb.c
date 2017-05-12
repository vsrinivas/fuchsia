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

#include <magenta/hw/usb.h>
#include <magenta/hw/usb-hid.h>
#include <magenta/device/usb.h>
#include <pretty/hexdump.h>

#define DEV_USB "/dev/class/usb"

static void get_string_desc(int fd, int index, char* buf, int buflen) {
    buf[0] = 0;
    ioctl_usb_get_string_desc(fd, &index, buf, buflen);
}

static const char* usb_speeds[] = {
    "<unknown>",
    "FULL",
    "LOW",
    "HIGH",
    "SUPER",
};

static int do_list_device(int fd, int configuration, bool verbose, const char* devname, int depth,
                          int max_depth) {
    usb_device_descriptor_t device_desc;
    char manufacturer[256];
    char product[256];
    manufacturer[0] = 0;
    product[0] = 0;
    ssize_t ret = 0;

    int device_type;
    ret = ioctl_usb_get_device_type(fd, &device_type);
    if (ret != sizeof(device_type)) {
        printf("IOCTL_USB_GET_DEVICE_TYPE failed for %s/%s\n", DEV_USB, devname);
        return ret;
    }
    if (device_type != USB_DEVICE_TYPE_DEVICE) {
        return ret;
    }

    ret = ioctl_usb_get_device_desc(fd, &device_desc);
    if (ret != sizeof(device_desc)) {
        printf("IOCTL_USB_GET_DEVICE_DESC failed for %s/%s\n", DEV_USB, devname);
        return ret;
    }

    int speed;
    ret = ioctl_usb_get_device_speed(fd, &speed);
    if (ret != sizeof(speed) || speed < 0 || (size_t)speed >= countof(usb_speeds)) {
        printf("IOCTL_USB_GET_DEVICE_SPEED failed for %s/%s\n", DEV_USB, devname);
        return ret;
    }

    get_string_desc(fd, device_desc.iManufacturer, manufacturer, sizeof(manufacturer));
    get_string_desc(fd, device_desc.iProduct, product, sizeof(product));

    int left_pad = depth * 4;
    int right_pad = (max_depth - depth) * 4;
    printf("%*s%3s  %*s%04X:%04X  %-5s  %s %s\n", left_pad, "", devname, right_pad, "",
           le16toh(device_desc.idVendor), le16toh(device_desc.idProduct), usb_speeds[speed],
           manufacturer, product);

    if (verbose) {
        char string_buf[256];

        // print device descriptor
        printf("Device Descriptor:\n");
        printf("  bLength                         %d\n", device_desc.bLength);
        printf("  bDescriptorType                 %d\n", device_desc.bDescriptorType);
        printf("  bcdUSB                          %x.%x\n", le16toh(device_desc.bcdUSB) >> 8,
                                                      le16toh(device_desc.bcdUSB) & 0xFF);
        printf("  bDeviceClass                    %d\n", device_desc.bDeviceClass);
        printf("  bDeviceSubClass                 %d\n", device_desc.bDeviceSubClass);
        printf("  bDeviceProtocol                 %d\n", device_desc.bDeviceProtocol);
        printf("  bMaxPacketSize0                 %d\n", device_desc.bMaxPacketSize0);
        printf("  idVendor                        0x%04X\n", le16toh(device_desc.idVendor));
        printf("  idProduct                       0x%04X\n", le16toh(device_desc.idProduct));
        printf("  bcdDevice                       %x.%x\n", le16toh(device_desc.bcdDevice) >> 8,
                                                            le16toh(device_desc.bcdDevice) & 0xFF);
        printf("  iManufacturer                   %d %s\n", device_desc.iManufacturer, manufacturer);
        printf("  iProduct                        %d %s\n", device_desc.iProduct, product);
        get_string_desc(fd, device_desc.iSerialNumber, string_buf, sizeof(string_buf));
        printf("  iSerialNumber                   %d %s\n", device_desc.iSerialNumber, string_buf);
        printf("  bNumConfigurations              %d\n", device_desc.bNumConfigurations);

        if (configuration == -1) {
            ret = ioctl_usb_get_configuration(fd, &configuration);
            if (ret != sizeof(configuration)) return ret;
        }

        int desc_size;
        ret = ioctl_usb_get_config_desc_size(fd, &configuration, &desc_size);
        if (ret != sizeof(desc_size)) {
            printf("IOCTL_USB_GET_CONFIG_DESC_SIZE failed for %s/%s\n", DEV_USB, devname);
            return ret;
        }

        uint8_t* desc = malloc(desc_size);
        if (!desc) {
            ret = -1;
            return ret;
        }

        ret = ioctl_usb_get_config_desc(fd, &configuration, desc, desc_size);
        if (ret != desc_size) {
            printf("IOCTL_USB_GET_CONFIG_DESC failed for %s/%s\n", DEV_USB, devname);
            goto free_out;
        }

        usb_descriptor_header_t* desc_end = (usb_descriptor_header_t *)(desc + desc_size);

        // print configuration descriptor
        usb_configuration_descriptor_t* config_desc = (usb_configuration_descriptor_t *)desc;
        printf("  Configuration Descriptor:\n");
        printf("    bLength                       %d\n", config_desc->bLength);
        printf("    bDescriptorType               %d\n", config_desc->bDescriptorType);
        printf("    wTotalLength                  %d\n", le16toh(config_desc->wTotalLength));
        printf("    bNumInterfaces                %d\n", config_desc->bNumInterfaces);
        printf("    bConfigurationValue           %d\n", config_desc->bConfigurationValue);
        get_string_desc(fd, config_desc->iConfiguration, string_buf, sizeof(string_buf));
        printf("    iConfiguration                %d %s\n", config_desc->iConfiguration, string_buf);
        printf("    bmAttributes                  0x%02X\n", config_desc->bmAttributes);
        printf("    bMaxPower                     %d\n", config_desc->bMaxPower);

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
                printf("      bLength                     %d\n", desc->bLength);
                printf("      bDescriptorType             %d\n", desc->bDescriptorType);
                printf("      bInterfaceNumber            %d\n", desc->bInterfaceNumber);
                printf("      bAlternateSetting           %d\n", desc->bAlternateSetting);
                printf("      bNumEndpoints               %d\n", desc->bNumEndpoints);
                printf("      bInterfaceClass             %d\n", desc->bInterfaceClass);
                printf("      bInterfaceSubClass          %d\n", desc->bInterfaceSubClass);
                printf("      bInterfaceProtocol          %d\n", desc->bInterfaceProtocol);
                get_string_desc(fd, desc->iInterface, string_buf, sizeof(string_buf));
                printf("      iInterface                  %d %s\n", desc->iInterface, string_buf);
            } else if (header->bDescriptorType == USB_DT_ENDPOINT) {
                usb_endpoint_descriptor_t* desc = (usb_endpoint_descriptor_t *)header;
                printf("      Endpoint Descriptor:\n");
                printf("        bLength                   %d\n", desc->bLength);
                printf("        bDescriptorType           %d\n", desc->bDescriptorType);
                printf("        bEndpointAddress          0x%02X\n", desc->bEndpointAddress);
                printf("        bmAttributes              0x%02X\n", desc->bmAttributes);
                printf("        wMaxPacketSize            %d\n", le16toh(desc->wMaxPacketSize));
                printf("        bInterval                 %d\n", desc->bInterval);
            } else if (header->bDescriptorType == USB_DT_HID) {
                usb_hid_descriptor_t* desc = (usb_hid_descriptor_t *)header;
                printf("      HID Descriptor:\n");
                printf("        bLength                   %d\n", desc->bLength);
                printf("        bDescriptorType           %d\n", desc->bDescriptorType);
                printf("        bcdHID                    %x.%x\n", le16toh(desc->bcdHID) >> 8,
                                                                    le16toh(desc->bcdHID) & 0xFF);
                printf("        bCountryCode              %d\n", desc->bCountryCode);
                printf("        bNumDescriptors           %d\n", desc->bNumDescriptors);
                for (int i = 0; i < desc->bNumDescriptors; i++) {
                    usb_hid_descriptor_entry_t* entry = &desc->descriptors[i];
                    printf("          bDescriptorType         %d\n", entry->bDescriptorType);
                    printf("          wDescriptorLength       %d\n", le16toh(entry->wDescriptorLength));
                }
            } else if (header->bDescriptorType == USB_DT_SS_EP_COMPANION) {
                usb_ss_ep_comp_descriptor_t* desc = (usb_ss_ep_comp_descriptor_t *)header;
                printf("        SuperSpeed Endpoint Companion Descriptor:\n");
                printf("          bLength                 %d\n", desc->bLength);
                printf("          bDescriptorType         %d\n", desc->bDescriptorType);
                printf("          bMaxBurst               0x%02X\n", desc->bMaxBurst);
                printf("          bmAttributes            0x%02X\n", desc->bmAttributes);
                printf("          wBytesPerInterval       %d\n", le16toh(desc->wBytesPerInterval));
            } else if (header->bDescriptorType == USB_DT_SS_ISOCH_EP_COMPANION) {
                usb_ss_isoch_ep_comp_descriptor_t* desc = (usb_ss_isoch_ep_comp_descriptor_t *)header;
                printf("        SuperSpeed Isochronous Endpoint Companion Descriptor:\n");
                printf("          bLength                 %d\n", desc->bLength);
                printf("          bDescriptorType         %d\n", desc->bDescriptorType);
                printf("          wReserved               %d\n", le16toh(desc->wReserved));
                printf("          wBytesPerInterval       %d\n", le16toh(desc->wBytesPerInterval));
            } else if (header->bDescriptorType == USB_DT_INTERFACE_ASSOCIATION) {
                usb_interface_assoc_descriptor_t* desc = (usb_interface_assoc_descriptor_t *)header;
                printf("    Interface Association Descriptor:\n");
                printf("      bLength                     %d\n", desc->bLength);
                printf("      bDescriptorType             %d\n", desc->bDescriptorType);
                printf("      bFirstInterface             %d\n", desc->bFirstInterface);
                printf("      bInterfaceCount             %d\n", desc->bInterfaceCount);
                printf("      bFunctionClass              %d\n", desc->bFunctionClass);
                printf("      bFunctionSubClass           %d\n", desc->bFunctionSubClass);
                printf("      bFunctionProtocol           %d\n", desc->bFunctionProtocol);
                printf("      iFunction                   %d\n", desc->iFunction);
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

    return 0;
}

static int list_device(const char* device_id, int configuration, bool verbose) {
    char devname[128];

    snprintf(devname, sizeof(devname), "%s/%s", DEV_USB, device_id);
    int fd = open(devname, O_RDONLY);
    if (fd < 0) {
        printf("Error opening %s\n", devname);
        return fd;
    }

    int ret = do_list_device(fd, configuration, verbose, device_id, 0, 0);
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
        if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) {
            list_device(de->d_name, -1, verbose);
        }
    }

    closedir(dir);
    return 0;
}
struct device_node {
    int fd;
    char devname[4];
    uint64_t device_id;
    uint64_t hub_id;
    struct device_node* next;
    int depth;  // depth in tree, or -1 if not computed yet
};

static int get_node_depth(struct device_node* node, struct device_node* devices) {
    if (node->depth >= 0) return node->depth;
    if (node->hub_id == 0) return 0;

    struct device_node* test_node = devices;
    while (test_node) {
        if (node->hub_id == test_node->device_id) {
            return get_node_depth(test_node, devices) + 1;
        }
        test_node = test_node->next;
    }
    // shouldn't get here
    return -1;
}

static void do_list_tree(struct device_node* devices, uint64_t hub_id, int max_depth) {
    struct device_node* node = devices;
    while (node) {
        if (node->hub_id == hub_id) {
            do_list_device(node->fd, -1, false, node->devname, node->depth, max_depth);
            do_list_tree(devices, node->device_id, max_depth);
        }
        node = node->next;
    }
}

static int list_tree(void) {
    struct dirent* de;
    DIR* dir = opendir(DEV_USB);
    if (!dir) {
        printf("Error opening %s\n", DEV_USB);
        return -1;
    }

    struct device_node* devices = NULL;
    struct device_node* tail = NULL;

    while ((de = readdir(dir)) != NULL) {
        char devname[30];

        snprintf(devname, sizeof(devname), "%s/%s", DEV_USB, de->d_name);
        int fd = open(devname, O_RDONLY);
        if (fd < 0) {
            printf("Error opening %s\n", devname);
            continue;
        }
        int device_type = -1;
        ioctl_usb_get_device_type(fd, &device_type);
        if (device_type != USB_DEVICE_TYPE_DEVICE) {
            close(fd);
            continue;
        }

        struct device_node* node = (struct device_node *)malloc(sizeof(struct device_node));
        if (!node) return -1;

        int ret = ioctl_usb_get_device_id(fd, &node->device_id);
        if (ret < 0) {
            printf("ioctl_usb_get_device_id failed for %s\n", devname);
            free(node);
            close(fd);
            continue;
        }
        ret = ioctl_usb_get_device_hub_id(fd, &node->hub_id);
        if (ret < 0) {
            printf("ioctl_usb_get_device_hub_id failed for %s\n", devname);
            free(node);
            close(fd);
            continue;
        }
        node->fd = fd;
        node->depth = -1;
        strlcpy(node->devname, de->d_name, sizeof(node->devname));
        if (devices == NULL) {
            devices = node;
        } else {
            tail->next = node;
        }
        tail = node;
        node->next = NULL;
    }
    closedir(dir);

    int max_depth = 0;
    // compute depths for all device_nodes and compute maximum depth
    struct device_node* node = devices;
    while (node) {
        int depth = get_node_depth(node, devices);
        if (depth > max_depth) max_depth = depth;
        node->depth = depth;
        node = node->next;
    }

    // print header
    printf("ID   ");
    for (int i = 0; i < max_depth; i++) {
        printf("    ");
    }
    printf(" VID:PID   SPEED  MANUFACTURER PRODUCT\n");

    // print device tree recursively
    do_list_tree(devices, 0, max_depth);

     node = devices;
    while (node) {
        struct device_node* next = node->next;
        close(node->fd);
        free(node);
        node = next;
    }

    return 0;
}

int main(int argc, const char** argv) {
    int result = 0;
    bool verbose = false;
    bool tree = false;
    const char* device_id = NULL;
    int configuration = -1;

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (!strcmp(arg, "-v")) {
            verbose = true;
        } else if (!strcmp(arg, "-t")) {
            tree = true;
        } else if (!strcmp(arg, "-c")) {
            if (++i == argc) {
                printf("configuration required after -c option\n");
                result = -1;
                goto usage;
            }
            configuration = atoi(argv[i]);
        } else if (!strcmp(arg, "-d")) {
            if (++i == argc) {
                printf("device ID required after -d option\n");
                result = -1;
                goto usage;
            }
            device_id = argv[i];
        } else {
            printf("unknown option \"%s\"\n", arg);
            result = -1;
            goto usage;
        }
    }

    if (tree) {
        return list_tree();
    } else {
        printf("ID    VID:PID   SPEED  MANUFACTURER PRODUCT\n");
        if (device_id) {
            return list_device(device_id, configuration, verbose);
        } else {
            return list_devices(verbose);
        }
    }

usage:
    printf("Usage:\n");
    printf("%s [-c <configuration>] [-d <device ID>] [-t] [-v]", argv[0]);
    printf("  -c   Prints configuration descriptor for specified configuration (rather than current configuration)\n");
    printf("  -d   Prints only specified device\n");
    printf("  -t   Prints USB device tree\n");
    printf("  -v   Verbose output (prints descriptors\n");
    return result;
}
