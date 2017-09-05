// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/hidbus.h>

#include <magenta/assert.h>
#include <magenta/types.h>
#include <magenta/device/i2c.h>

#include <endian.h>
#include <stdbool.h>
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
    mx_device_t* i2cdev;

    mtx_t lock;
    hidbus_ifc_t* ifc;
    void* cookie;

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

static mx_status_t i2c_hid_query(void* ctx, uint32_t options, hid_info_t* info) {
    if (!info) {
        return MX_ERR_INVALID_ARGS;
    }
    info->dev_num = 0;
    info->dev_class = HID_DEV_CLASS_OTHER;
    info->boot_device = false;
    return MX_OK;
}

static mx_status_t i2c_hid_start(void* ctx, hidbus_ifc_t* ifc, void* cookie) {
    i2c_hid_device_t* hid = ctx;
    mtx_lock(&hid->lock);
    if (hid->ifc) {
        mtx_unlock(&hid->lock);
        return MX_ERR_ALREADY_BOUND;
    }
    hid->ifc = ifc;
    hid->cookie = cookie;
    mtx_unlock(&hid->lock);
    return MX_OK;
}

static void i2c_hid_stop(void* ctx) {
    i2c_hid_device_t* hid = ctx;
    mtx_lock(&hid->lock);
    hid->ifc = NULL;
    hid->cookie = NULL;
    mtx_unlock(&hid->lock);
}

static mx_status_t i2c_hid_get_descriptor(void* ctx, uint8_t desc_type,
        void** data, size_t* len) {
    if (desc_type != HID_DESC_TYPE_REPORT) {
        return MX_ERR_NOT_FOUND;
    }

    i2c_hid_device_t* hid = ctx;
    size_t desc_len = letoh16(hid->hiddesc->wReportDescLength);
    uint16_t desc_reg = letoh16(hid->hiddesc->wReportDescRegister);

    uint8_t buf[3 * sizeof(i2c_slave_ioctl_segment_t) + 2];
    uint8_t* bufdata = i2c_hid_prepare_write_read_buffer(buf, 2, desc_len);
    *bufdata++ = desc_reg & 0xff;
    *bufdata++ = (desc_reg >> 8 ) & 0xff;

    uint8_t* out = malloc(desc_len);
    if (out == NULL) {
        return MX_ERR_NO_MEMORY;
    }
    size_t actual = 0;
    mx_status_t status = device_ioctl(hid->i2cdev, IOCTL_I2C_SLAVE_TRANSFER,
                                      buf, sizeof(buf), out, desc_len, &actual);
    if (status < 0) {
        dprintf(ERROR, "i2c-hid: could not read HID report descriptor: %d\n", status);
        free(out);
        return MX_ERR_NOT_SUPPORTED;
    }

    *data = out;
    *len = actual;
    return MX_OK;
}

// TODO: implement the rest of the HID protocol
static mx_status_t i2c_hid_get_report(void* ctx, uint8_t rpt_type, uint8_t rpt_id,
        void* data, size_t len) {
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t i2c_hid_set_report(void* ctx, uint8_t rpt_type, uint8_t rpt_id,
        void* data, size_t len) {
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t i2c_hid_get_idle(void* ctx, uint8_t rpt_id, uint8_t* duration) {
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t i2c_hid_set_idle(void* ctx, uint8_t rpt_id, uint8_t duration) {
    return MX_OK;
}

static mx_status_t i2c_hid_get_protocol(void* ctx, uint8_t* protocol) {
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t i2c_hid_set_protocol(void* ctx, uint8_t protocol) {
    return MX_OK;
}


static hidbus_protocol_ops_t i2c_hidbus_ops = {
    .query = i2c_hid_query,
    .start = i2c_hid_start,
    .stop = i2c_hid_stop,
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

    mx_time_t last_timeout_warning = 0;
    const mx_duration_t kMinTimeBetweenWarnings = MX_SEC(10);

    // Until we have a way to map the GPIO associated with an i2c slave to an
    // IRQ, we just poll.
    while (true) {
        usleep(I2C_POLL_INTERVAL_USEC);
        size_t actual = 0;
        mx_status_t status = device_read(dev->i2cdev, buf, len, 0, &actual);
        if (status != MX_OK) {
            if (status == MX_ERR_TIMED_OUT) {
                mx_time_t now = mx_time_get(MX_CLOCK_MONOTONIC);
                if (now - last_timeout_warning > kMinTimeBetweenWarnings) {
                    dprintf(TRACE, "i2c-hid: device_read timed out\n");
                    last_timeout_warning = now;
                }
                continue;
            }
            dprintf(ERROR, "i2c-hid: fatal device_read failure %d\n", status);
            return status;
        }
        if (actual < 2) {
            dprintf(ERROR, "i2c-hid: short read (%zd < 2)!!!\n", actual);
            continue;
        }

        uint16_t report_len = letoh16(*(uint16_t*)buf);
        if ((report_len == 0xffff) || (report_len == 0x3fff) || (report_len == 0x0)) {
            // nothing to read
            continue;
        }
        if ((report_len > actual) || (report_len < 2)) {
            dprintf(ERROR, "i2c-hid: bad report len (rlen %hu, bytes read %zd)!!!\n",
                    report_len, actual);
            continue;
        }
        mtx_lock(&dev->lock);
        if (dev->ifc) {
            dev->ifc->io_queue(dev->cookie, buf + 2, report_len - 2);
        }
        mtx_unlock(&dev->lock);
    }

    // TODO: figure out how to clean up
    free(buf);
    return 0;
}

static void i2c_hid_release(void* ctx) {
    MX_PANIC("cannot release an i2c hid device yet!\n");
}

static mx_protocol_device_t i2c_hid_dev_ops = {
    .version = DEVICE_OPS_VERSION,
    .release = i2c_hid_release,
};

static mx_status_t i2c_hid_bind(void* ctx, mx_device_t* dev, void** cookie) {
    dprintf(TRACE, "i2c_hid_bind\n");

    // Read the i2c HID descriptor
    // TODO: get the address out of ACPI
    uint8_t buf[3 * sizeof(i2c_slave_ioctl_segment_t) + 2];
    uint8_t* data = i2c_hid_prepare_write_read_buffer(buf, 2, 4);
    *data++ = 0x01;
    *data++ = 0x00;
    uint8_t out[4];
    size_t actual = 0;
    mx_status_t ret = device_ioctl(dev, IOCTL_I2C_SLAVE_TRANSFER, buf, sizeof(buf), out, sizeof(out), &actual);
    if (ret < 0 || actual != sizeof(out)) {
        dprintf(ERROR, "i2c-hid: could not read HID descriptor: %d\n", ret);
        return MX_ERR_NOT_SUPPORTED;
    }
    i2c_hid_desc_t* i2c_hid_desc_hdr = (i2c_hid_desc_t*)out;
    uint16_t desc_len = letoh16(i2c_hid_desc_hdr->wHIDDescLength);

    i2c_hid_device_t* i2chid = calloc(1, sizeof(i2c_hid_device_t));
    if (i2chid == NULL) {
        return MX_ERR_NO_MEMORY;
    }
    i2chid->i2cdev = dev;
    i2chid->hiddesc = malloc(desc_len);

    i2c_hid_prepare_write_read_buffer(buf, 2, desc_len);
    actual = 0;
    ret = device_ioctl(dev, IOCTL_I2C_SLAVE_TRANSFER, buf, sizeof(buf), i2chid->hiddesc, desc_len, &actual);
    if (ret < 0 || actual != desc_len) {
        dprintf(ERROR, "i2c-hid: could not read HID descriptor: %d\n", ret);
        free(i2chid->hiddesc);
        free(i2chid);
        return MX_ERR_NOT_SUPPORTED;
    }

    dprintf(TRACE, "i2c-hid: desc:\n");
    dprintf(TRACE, "  report desc len: %u\n", letoh16(i2chid->hiddesc->wReportDescLength));
    dprintf(TRACE, "  report desc reg: %u\n", letoh16(i2chid->hiddesc->wReportDescRegister));
    dprintf(TRACE, "  input reg:       %u\n", letoh16(i2chid->hiddesc->wInputRegister));
    dprintf(TRACE, "  max input len:   %u\n", letoh16(i2chid->hiddesc->wMaxInputLength));
    dprintf(TRACE, "  output reg:      %u\n", letoh16(i2chid->hiddesc->wOutputRegister));
    dprintf(TRACE, "  max output len:  %u\n", letoh16(i2chid->hiddesc->wMaxOutputLength));
    dprintf(TRACE, "  command reg:     %u\n", letoh16(i2chid->hiddesc->wCommandRegister));
    dprintf(TRACE, "  data reg:        %u\n", letoh16(i2chid->hiddesc->wDataRegister));
    dprintf(TRACE, "  vendor id:       %x\n", i2chid->hiddesc->wVendorID);
    dprintf(TRACE, "  product id:      %x\n", i2chid->hiddesc->wProductID);
    dprintf(TRACE, "  version id:      %x\n", i2chid->hiddesc->wVersionID);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "i2c-hid",
        .ctx = i2chid,
        .ops = &i2c_hid_dev_ops,
        .proto_id = MX_PROTOCOL_HIDBUS,
        .proto_ops = &i2c_hidbus_ops,
    };

    mx_status_t status = device_add(i2chid->i2cdev, &args, NULL);
    if (status != MX_OK) {
        dprintf(ERROR, "i2c-hid: could not add device: %d\n", status);
        free(i2chid->hiddesc);
        free(i2chid);
        return status;
    }

    ret = thrd_create_with_name(&i2chid->irq_thread, i2c_hid_irq_thread, i2chid, "i2c-hid-irq");
    if (ret != thrd_success) {
        dprintf(ERROR, "i2c-hid: could not create irq thread: %d\n", ret);
        free(i2chid->hiddesc);
        free(i2chid);
        // TODO: map thrd_* status codes to MX_ERR_* status codes
        return MX_ERR_INTERNAL;
    }

    return MX_OK;
}

static mx_driver_ops_t i2c_hid_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = i2c_hid_bind,
};

MAGENTA_DRIVER_BEGIN(i2c_hid, i2c_hid_driver_ops, "magenta", "0.1", 9)
    BI_ABORT_IF(NE, BIND_PCI_VID, 0x8086),

    // Acer12
    BI_GOTO_IF(NE, BIND_PCI_DID, 0x9d61, 0),
    BI_MATCH_IF(EQ, BIND_I2C_ADDR, 0x0010),

    BI_LABEL(0),
    BI_GOTO_IF(NE, BIND_PCI_DID, 0x9d60, 0),
    BI_MATCH_IF(EQ, BIND_I2C_ADDR, 0x000A),

    BI_LABEL(0),
    BI_ABORT_IF(NE, BIND_PCI_DID, 0x9d62),
    BI_MATCH_IF(EQ, BIND_I2C_ADDR, 0x0049),

MAGENTA_DRIVER_END(i2c_hid)
