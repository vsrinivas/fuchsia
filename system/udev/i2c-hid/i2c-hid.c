// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/common/hid.h>

#include <magenta/types.h>
#include <magenta/device/i2c.h>

#include <endian.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#define I2C_HID_DEBUG 0

// Poll interval: 10 ms
#define I2C_POLL_INTERVAL_USEC 10000

#define to_i2c_hid(d) containerof(d, i2c_hid_device_t, hiddev)

typedef struct i2c_hid_desc {
    uint16_t wHIDDescLength;
    uint16_t bcdVersion;
    uint16_t wReportDescLength;
    uint16_t wReportDescRegister;
    uint16_t wInputRegister;
    uint16_t wMaxInputLength;
    uint16_t wOutputRegister;
    uint16_t wMaxOutputLength;
    uint16_t wCommandRegister;
    uint16_t wDataRegister;
    uint16_t wVendorID;
    uint16_t wProductID;
    uint16_t wVersionID;
    uint8_t RESERVED[4];
} __PACKED i2c_hid_desc_t;

typedef struct i2c_hid_device {
    mx_hid_device_t hiddev;
    mx_device_t* i2cdev;

    i2c_hid_desc_t* hiddesc;
    thrd_t irq_thread;
} i2c_hid_device_t;

static uint8_t* i2c_hid_prepare_write_read_buffer(uint8_t* buf, int wlen, int rlen) {
    i2c_slave_ioctl_segment_t* segments = (i2c_slave_ioctl_segment_t*)buf;
    segments[0].type = I2C_SEGMENT_TYPE_WRITE;
    segments[0].len = wlen;
    segments[1].type = I2C_SEGMENT_TYPE_READ;
    segments[1].len = rlen;
    segments[2].type = I2C_SEGMENT_TYPE_END;
    segments[2].len = 0;
    return buf + 3 * sizeof(i2c_slave_ioctl_segment_t);
}

static mx_status_t i2c_hid_get_descriptor(mx_hid_device_t* dev, uint8_t desc_type,
        void** data, size_t* len) {
    if (desc_type != HID_DESC_TYPE_REPORT) {
        return ERR_NOT_FOUND;
    }

    i2c_hid_device_t* hid = to_i2c_hid(dev);
    size_t desc_len = letoh16(hid->hiddesc->wReportDescLength);
    uint16_t desc_reg = letoh16(hid->hiddesc->wReportDescRegister);

    uint8_t buf[3 * sizeof(i2c_slave_ioctl_segment_t) + 2];
    uint8_t* bufdata = i2c_hid_prepare_write_read_buffer(buf, 2, desc_len);
    *bufdata++ = desc_reg & 0xff;
    *bufdata++ = (desc_reg >> 8 ) & 0xff;

    uint8_t* out = malloc(desc_len);
    if (out == NULL) {
        return ERR_NO_MEMORY;
    }
    ssize_t ret = hid->i2cdev->ops->ioctl(hid->i2cdev, IOCTL_I2C_SLAVE_TRANSFER,
            buf, sizeof(buf), out, desc_len);
    if (ret < 0) {
        printf("i2c-hid: could not read HID report descriptor: %zd\n", ret);
        free(out);
        return ERR_NOT_SUPPORTED;
    }

    *data = out;
    *len = desc_len;
    return NO_ERROR;
}

// TODO: implement the rest of the HID protocol
static mx_status_t i2c_hid_get_report(mx_hid_device_t* dev, uint8_t rpt_type, uint8_t rpt_id,
        void* data, size_t len) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t i2c_hid_set_report(mx_hid_device_t* dev, uint8_t rpt_type, uint8_t rpt_id,
        void* data, size_t len) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t i2c_hid_get_idle(mx_hid_device_t* dev, uint8_t rpt_id, uint8_t* duration) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t i2c_hid_set_idle(mx_hid_device_t* dev, uint8_t rpt_id, uint8_t duration) {
    return NO_ERROR;
}

static mx_status_t i2c_hid_get_protocol(mx_hid_device_t* dev, uint8_t* protocol) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t i2c_hid_set_protocol(mx_hid_device_t* dev, uint8_t protocol) {
    return NO_ERROR;
}


static hid_bus_ops_t i2c_hid_bus_ops = {
    .get_descriptor = i2c_hid_get_descriptor,
    .get_report = i2c_hid_get_report,
    .set_report = i2c_hid_set_report,
    .get_idle = i2c_hid_get_idle,
    .set_idle = i2c_hid_set_idle,
    .get_protocol = i2c_hid_get_protocol,
    .set_protocol = i2c_hid_set_protocol,
};

static inline size_t bcdtoa(uint16_t val, char str[static 6], bool pad) {
    memset(str, 0, 6);
    size_t idx = 0;
    if (val >> 12) {
        str[idx++] = (val >> 12) + '0';
    }
    str[idx++] = ((val >> 8) & 0xf) + '0';
    str[idx++] = '.';
    str[idx++] = ((val >> 4) & 0xf) + '0';
    str[idx++] = (val & 0xf) + '0';
    return idx;
}

static int i2c_hid_irq_thread(void* arg) {
    i2c_hid_device_t* dev = (i2c_hid_device_t*)arg;
    uint16_t len = letoh16(dev->hiddesc->wMaxInputLength);
    uint8_t* buf = malloc(len);

    // Until we have a way to map the GPIO associated with an i2c slave to an
    // IRQ, we just poll.
    while (true) {
        usleep(I2C_POLL_INTERVAL_USEC);
        while (true) {
            ssize_t ret = dev->i2cdev->ops->read(dev->i2cdev, buf, len, 0);
            if (ret < 2) {
                printf("i2c-hid: short read (%zd < 2)!!!\n", ret);
                break;
            }
            uint16_t report_len = letoh16(*(uint16_t*)buf);
            if (report_len == 0xffff || report_len == 0x3fff) {
                // nothing to read
                break;
            }
            if (ret < report_len) {
                printf("i2c-hid: short read (%zd < %u)!!!\n", ret, report_len);
                break;
            }
            hid_io_queue(&dev->hiddev, buf + 2, report_len - 2);
        }
    }

    // TODO: figure out how to clean up
    free(buf);
    return 0;
}

static mx_status_t i2c_hid_bind(mx_driver_t* drv, mx_device_t* dev) {
    printf("i2c_hid_bind\n");

    // Read the i2c HID descriptor
    // TODO: get the address out of ACPI
    uint8_t buf[3 * sizeof(i2c_slave_ioctl_segment_t) + 2];
    uint8_t* data = i2c_hid_prepare_write_read_buffer(buf, 2, 4);
    *data++ = 0x01;
    *data++ = 0x00;
    uint8_t out[4];
    ssize_t ret = dev->ops->ioctl(dev, IOCTL_I2C_SLAVE_TRANSFER, buf, sizeof(buf), out, sizeof(out));
    if (ret < 0) {
        printf("i2c-hid: could not read HID descriptor: %zd\n", ret);
        return ERR_NOT_SUPPORTED;
    }
    i2c_hid_desc_t* i2c_hid_desc_hdr = (i2c_hid_desc_t*)out;
    uint16_t desc_len = letoh16(i2c_hid_desc_hdr->wHIDDescLength);

    i2c_hid_device_t* i2chid = calloc(1, sizeof(i2c_hid_device_t));
    if (i2chid == NULL) {
        return ERR_NO_MEMORY;
    }
    i2chid->i2cdev = dev;
    i2chid->hiddesc = malloc(desc_len);

    i2c_hid_prepare_write_read_buffer(buf, 2, desc_len);
    ret = dev->ops->ioctl(dev, IOCTL_I2C_SLAVE_TRANSFER, buf, sizeof(buf), i2chid->hiddesc, desc_len);
    if (ret < 0) {
        printf("i2c-hid: could not read HID descriptor: %zd\n", ret);
        free(i2chid->hiddesc);
        free(i2chid);
        return ERR_NOT_SUPPORTED;
    }

#if I2C_HID_DEBUG
    printf("i2c-hid: desc:\n");
    printf("  report desc len: %u\n", letoh16(i2chid->hiddesc->wReportDescLength));
    printf("  report desc reg: %u\n", letoh16(i2chid->hiddesc->wReportDescRegister));
    printf("  input reg:       %u\n", letoh16(i2chid->hiddesc->wInputRegister));
    printf("  max input len:   %u\n", letoh16(i2chid->hiddesc->wMaxInputLength));
    printf("  output reg:      %u\n", letoh16(i2chid->hiddesc->wOutputRegister));
    printf("  max output len:  %u\n", letoh16(i2chid->hiddesc->wMaxOutputLength));
    printf("  command reg:     %u\n", letoh16(i2chid->hiddesc->wCommandRegister));
    printf("  data reg:        %u\n", letoh16(i2chid->hiddesc->wDataRegister));
    printf("  vendor id:       %x\n", i2chid->hiddesc->wVendorID);
    printf("  product id:      %x\n", i2chid->hiddesc->wProductID);
    printf("  version id:      %x\n", i2chid->hiddesc->wVersionID);
#endif

    hid_init_device(&i2chid->hiddev, &i2c_hid_bus_ops, 0, false, HID_DEV_CLASS_OTHER);
    mx_status_t status = hid_add_device(drv, &i2chid->hiddev, dev);
    if (status != NO_ERROR) {
        free(i2chid->hiddesc);
        hid_release_device(&i2chid->hiddev);
        free(i2chid);
        return status;
    }

    ret = thrd_create_with_name(&i2chid->irq_thread, i2c_hid_irq_thread, i2chid, "i2c-hid-irq");
    if (ret != thrd_success) {
        free(i2chid->hiddesc);
        hid_release_device(&i2chid->hiddev);
        free(i2chid);
        // TODO: map thrd_* status codes to ERR_* status codes
        return ERR_INTERNAL;
    }

    return NO_ERROR;
}

mx_driver_t _driver_i2c_hid = {
    .ops = {
        .bind = i2c_hid_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_i2c_hid, "i2c-hid", "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x8086),
    BI_ABORT_IF(NE, BIND_PCI_DID, 0x9d61),
    BI_MATCH_IF(EQ, BIND_I2C_ADDR, 0x0010),
MAGENTA_DRIVER_END(_driver_i2c_hid)
