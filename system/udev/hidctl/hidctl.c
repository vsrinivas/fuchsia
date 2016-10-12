// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/common/hid.h>

#include <magenta/device/hidctl.h>
#include <magenta/types.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define from_mx_device(d)  containerof(d, hidctl_instance_t, dev)
#define from_hid_device(d) containerof(d, hidctl_instance_t, hiddev)

mx_driver_t _driver_hidctl;
static mx_device_t* hidctl_dev;

typedef struct hidctl_instance {
    mx_device_t dev;
    mx_hid_device_t hiddev;

    uint8_t* hid_report_desc;
    size_t hid_report_desc_len;
} hidctl_instance_t;

mx_status_t hidctl_get_descriptor(mx_hid_device_t* dev, uint8_t desc_type, void** data, size_t* len) {
    if (desc_type != HID_DESC_TYPE_REPORT) {
        return ERR_NOT_SUPPORTED;
    }
    hidctl_instance_t* inst = from_hid_device(dev);
    *data = malloc(inst->hid_report_desc_len);
    memcpy(*data, inst->hid_report_desc, inst->hid_report_desc_len);
    *len = inst->hid_report_desc_len;
    return NO_ERROR;
}

mx_status_t hidctl_get_report(mx_hid_device_t* dev, uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t hidctl_set_report(mx_hid_device_t* dev, uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t hidctl_get_idle(mx_hid_device_t* dev, uint8_t rpt_id, uint8_t* duration) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t hidctl_set_idle(mx_hid_device_t* dev, uint8_t rpt_id, uint8_t duration) {
    return NO_ERROR;
}

mx_status_t hidctl_get_protocol(mx_hid_device_t* dev, uint8_t* protocol) {
    return ERR_NOT_SUPPORTED;
}

mx_status_t hidctl_set_protocol(mx_hid_device_t* dev, uint8_t protocol) {
    return NO_ERROR;
}

static hid_bus_ops_t hidctl_hid_ops = {
    .get_descriptor = hidctl_get_descriptor,
    .get_report = hidctl_get_report,
    .set_report = hidctl_set_report,
    .get_idle = hidctl_get_idle,
    .set_idle = hidctl_set_idle,
    .get_protocol = hidctl_get_protocol,
    .set_protocol = hidctl_set_protocol,
};

static ssize_t hidctl_set_config(hidctl_instance_t* dev, const void* in_buf, size_t in_len) {
    const hid_ioctl_config_t* cfg = in_buf;
    if (in_len < sizeof(hid_ioctl_config_t) || in_len != sizeof(hid_ioctl_config_t) + cfg->rpt_desc_len) {
        return ERR_INVALID_ARGS;
    }

    if (cfg->dev_class > HID_DEV_CLASS_LAST) {
        return ERR_INVALID_ARGS;
    }

    hid_init_device(&dev->hiddev, &hidctl_hid_ops, cfg->dev_num, cfg->boot_device, cfg->dev_class);

    dev->hid_report_desc_len = cfg->rpt_desc_len;
    dev->hid_report_desc = malloc(cfg->rpt_desc_len);
    memcpy(dev->hid_report_desc, cfg->rpt_desc, cfg->rpt_desc_len);

    mx_status_t status = hid_add_device(&_driver_hidctl, &dev->hiddev, hidctl_dev);
    if (status != NO_ERROR) {
        hid_release_device(&dev->hiddev);
        free(dev->hid_report_desc);
        dev->hid_report_desc = NULL;
    } else {
        printf("hidctl: device added\n");
    }
    return status;
}

static ssize_t hidctl_read(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
    return 0;
}

static ssize_t hidctl_write(mx_device_t* dev, const void* buf, size_t count, mx_off_t off) {
    hidctl_instance_t* inst = from_mx_device(dev);
    hid_io_queue(&inst->hiddev, buf, count);
    return count;
}

static ssize_t hidctl_ioctl(mx_device_t* dev, uint32_t op,
        const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    hidctl_instance_t* inst = from_mx_device(dev);
    switch (op) {
    case IOCTL_HID_CTL_CONFIG:
        return hidctl_set_config(inst, in_buf, in_len);
        break;
    }
    return ERR_NOT_SUPPORTED;
}

static mx_status_t hidctl_release(mx_device_t* dev) {
    hidctl_instance_t* inst = from_mx_device(dev);
    hid_release_device(&inst->hiddev);
    if (inst->hid_report_desc) {
        free(inst->hid_report_desc);
        device_remove(&inst->hiddev.dev);
    }
    free(inst);
    return NO_ERROR;
}

mx_protocol_device_t hidctl_instance_proto = {
    .read = hidctl_read,
    .write = hidctl_write,
    .ioctl = hidctl_ioctl,
    .release = hidctl_release,
};

static mx_status_t hidctl_open(mx_device_t* dev, mx_device_t** dev_out, uint32_t flags) {
    hidctl_instance_t* inst = calloc(1, sizeof(hidctl_instance_t));
    if (inst == NULL) {
        return ERR_NO_MEMORY;
    }

    device_init(&inst->dev, &_driver_hidctl, "hidctl-inst", &hidctl_instance_proto);
    mx_status_t status = device_add_instance(&inst->dev, dev);
    if (status != NO_ERROR) {
        printf("hidctl: could not open instance: %d\n", status);
        return status;
    }
    *dev_out = &inst->dev;
    return NO_ERROR;
}

static mx_protocol_device_t hidctl_device_proto = {
    .open = hidctl_open,
};

static mx_status_t hidctl_init(mx_driver_t* driver) {
    if (device_create(&hidctl_dev, driver, "hidctl", &hidctl_device_proto) == NO_ERROR) {
        if (device_add(hidctl_dev, driver_get_misc_device()) < 0) {
            free(hidctl_dev);
        }
    }
    return NO_ERROR;
}

mx_driver_t _driver_hidctl = {
    .ops = {
        .init = hidctl_init,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_hidctl, "hidctl", "magenta", "0.1", 0)
MAGENTA_DRIVER_END(_driver_hidctl)
