// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hid-fifo.h"
#include "hid-parser.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/hidbus.h>
#include <ddk/trace/event.h>
#include <fuchsia/hardware/input/c/fidl.h>

#include <zircon/assert.h>
#include <zircon/listnode.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define HID_FLAGS_DEAD         (1 << 0)
#define HID_FLAGS_WRITE_FAILED (1 << 1)

// TODO(johngro) : Get this from a standard header instead of defining our own.
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define foreach_instance(base, instance) \
    list_for_every_entry(&base->instance_list, instance, hid_instance_t, node)
#define bits_to_bytes(n) (((n) + 7) / 8)

// Until we do full HID parsing, we put mouse and keyboard devices into boot
// protocol mode. In particular, a mouse will always send 3 byte reports (see
// ddk/protocol/input.h for the format). This macro sets FIDL return values for
// boot mouse devices to reflect the boot protocol, rather than what the device
// itself reports.
// TODO: update this to include keyboards if we find a keyboard in the wild that
// needs a hack as well.
#define BOOT_MOUSE_HACK 1

#define HID_REPORT_TRACE_ID(instance_id, report_id) \
    (((uint64_t) (report_id) << 32) | (instance_id))

typedef struct hid_device {
    zx_device_t* zxdev;

    hid_info_t info;
    hidbus_protocol_t hid;

    // Reassembly buffer for input events too large to fit in a single interrupt
    // transaction.
    uint8_t* rbuf;
    size_t rbuf_size;
    size_t rbuf_filled;
    size_t rbuf_needed;

    size_t hid_report_desc_len;
    uint8_t* hid_report_desc;

    // TODO(johngro, tkilbourn: Do not hardcode this limit!)
#define HID_MAX_REPORT_IDS 32
    size_t num_reports;
    hid_report_size_t sizes[HID_MAX_REPORT_IDS];

    struct list_node instance_list;
    mtx_t instance_lock;

    char name[ZX_DEVICE_NAME_MAX + 1];
} hid_device_t;

typedef struct hid_instance {
    zx_device_t* zxdev;
    hid_device_t* base;

    uint32_t flags;

    zx_hid_fifo_t fifo;
    uint32_t trace_id;
    uint32_t reports_written;
    uint32_t reports_read;

    struct list_node node;
} hid_instance_t;

// Convenience functions for calling hidbus protocol functions

static inline zx_status_t hid_op_query(hid_device_t* hid, uint32_t options, hid_info_t* info) {
    return hid->hid.ops->query(hid->hid.ctx, options, info);
}

static inline zx_status_t hid_op_start(hid_device_t* hid, void* ctx, hidbus_ifc_protocol_ops_t* ops) {
    return hidbus_start(&hid->hid, ctx, ops);
}

static inline void hid_op_stop(hid_device_t* hid) {
    hidbus_stop(&hid->hid);
}

static inline zx_status_t hid_op_get_descriptor(hid_device_t* hid, uint8_t desc_type,
                                                void** data, size_t* len) {
    return hidbus_get_descriptor(&hid->hid, desc_type, data, len);
}

static inline zx_status_t hid_op_get_report(hid_device_t* hid, uint8_t rpt_type, uint8_t rpt_id,
                                            void* data, size_t len, size_t* out_len) {
    return hidbus_get_report(&hid->hid, rpt_type, rpt_id, data, len, out_len);
}

static inline zx_status_t hid_op_set_report(hid_device_t* hid, uint8_t rpt_type, uint8_t rpt_id,
                                            const void* data, size_t len) {
    return hidbus_set_report(&hid->hid, rpt_type, rpt_id, data, len);
}

static inline zx_status_t hid_op_get_idle(hid_device_t* hid, uint8_t rpt_id, uint8_t* duration) {
    return hidbus_get_idle(&hid->hid, rpt_id, duration);
}

static inline zx_status_t hid_op_set_idle(hid_device_t* hid, uint8_t rpt_id, uint8_t duration) {
    return hidbus_set_idle(&hid->hid, rpt_id, duration);
}

static inline zx_status_t hid_op_get_protocol(hid_device_t* hid, uint8_t* protocol) {
    return hidbus_get_protocol(&hid->hid, protocol);
}

static inline zx_status_t hid_op_set_protocol(hid_device_t* hid, uint8_t protocol) {
    return hidbus_set_protocol(&hid->hid, protocol);
}

static input_report_size_t hid_get_report_size_by_id(hid_device_t* hid, input_report_id_t id,
                                                     fuchsia_hardware_input_ReportType type) {
    for (size_t i = 0; i < hid->num_reports; i++) {
        if ((hid->sizes[i].id == id) || (hid->num_reports == 1)) {
            switch (type) {
            case fuchsia_hardware_input_ReportType_INPUT:
                return bits_to_bytes(hid->sizes[i].in_size);
            case fuchsia_hardware_input_ReportType_OUTPUT:
                return bits_to_bytes(hid->sizes[i].out_size);
            case fuchsia_hardware_input_ReportType_FEATURE:
                return bits_to_bytes(hid->sizes[i].feat_size);
            }
        }
    }

    return 0;
}

static fuchsia_hardware_input_BootProtocol get_boot_protocol(hid_device_t* hid) {
    if (hid->info.device_class == HID_DEVICE_CLASS_KBD ||
        hid->info.device_class == HID_DEVICE_CLASS_KBD_POINTER) {
        return fuchsia_hardware_input_BootProtocol_KBD;
    } else if (hid->info.device_class == HID_DEVICE_CLASS_POINTER) {
        return fuchsia_hardware_input_BootProtocol_MOUSE;
    }
    return fuchsia_hardware_input_BootProtocol_NONE;
}

static input_report_size_t get_max_input_reportsize(hid_device_t* hid) {
    size_t size = 0;
    for (size_t i = 0; i < hid->num_reports; i++) {
        if (hid->sizes[i].in_size > size)
            size = hid->sizes[i].in_size;
    }

    return bits_to_bytes(size);
}

static zx_status_t hid_read_instance(void* ctx, void* buf, size_t count, zx_off_t off,
                                     size_t* actual) {
    TRACE_DURATION("input", "HID Read Instance");

    hid_instance_t* hid = ctx;

    if (hid->flags & HID_FLAGS_DEAD) {
        return ZX_ERR_PEER_CLOSED;
    }

    size_t left;
    mtx_lock(&hid->fifo.lock);
    size_t xfer;
    uint8_t rpt_id;
    ssize_t r = zx_hid_fifo_peek(&hid->fifo, &rpt_id);
    if (r < 1) {
        // fifo is empty
        mtx_unlock(&hid->fifo.lock);
        return ZX_ERR_SHOULD_WAIT;
    }

    xfer = hid_get_report_size_by_id(hid->base, rpt_id, fuchsia_hardware_input_ReportType_INPUT);
    if (xfer == 0) {
        zxlogf(ERROR, "error reading hid device: unknown report id (%u)!\n", rpt_id);
        mtx_unlock(&hid->fifo.lock);
        return ZX_ERR_BAD_STATE;
    }

    if (xfer > count) {
        zxlogf(SPEW, "next report: %zd, read count: %zd\n", xfer, count);
        mtx_unlock(&hid->fifo.lock);
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    r = zx_hid_fifo_read(&hid->fifo, buf, xfer);
    left = zx_hid_fifo_size(&hid->fifo);
    if (left == 0) {
        device_state_clr(hid->zxdev, DEV_STATE_READABLE);
    }
    mtx_unlock(&hid->fifo.lock);
    if (r > 0) {
        TRACE_FLOW_STEP("input", "hid_report",
                        HID_REPORT_TRACE_ID(hid->trace_id,
                                            hid->reports_read));
        ++hid->reports_read;
        *actual = r;
        r = ZX_OK;
    } else if (r == 0) {
        r = ZX_ERR_SHOULD_WAIT;
    }
    return r;
}

static zx_status_t hid_close_instance(void* ctx, uint32_t flags) {
    hid_instance_t* hid = ctx;
    hid->flags |= HID_FLAGS_DEAD;
    mtx_lock(&hid->base->instance_lock);
    // TODO: refcount the base device and call stop if no instances are open
    list_delete(&hid->node);
    mtx_unlock(&hid->base->instance_lock);
    return ZX_OK;
}

static void hid_release_reassembly_buffer(hid_device_t* dev);

static void hid_release_instance(void* ctx) {
    hid_instance_t* hid = ctx;
    free(hid);
}

static zx_status_t fidl_GetBootProtocol(void* ctx, fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    return fuchsia_hardware_input_DeviceGetBootProtocol_reply(txn, get_boot_protocol(hid->base));
}

static zx_status_t fidl_GetReportDescSize(void* ctx, fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    return fuchsia_hardware_input_DeviceGetReportDescSize_reply(txn,
                                                                hid->base->hid_report_desc_len);
}

static zx_status_t fidl_GetReportDesc(void* ctx, fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    return fuchsia_hardware_input_DeviceGetReportDesc_reply(txn, hid->base->hid_report_desc,
                                                            hid->base->hid_report_desc_len);
}

static zx_status_t fidl_GetNumReports(void* ctx, fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    return fuchsia_hardware_input_DeviceGetNumReports_reply(txn, hid->base->num_reports);
}

static zx_status_t fidl_GetReportIds(void* ctx, fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    uint8_t report[fuchsia_hardware_input_MAX_REPORT_IDS];
    for (size_t i = 0; i < hid->base->num_reports; i++) {
        report[i] = hid->base->sizes[i].id;
    }
    return fuchsia_hardware_input_DeviceGetReportIds_reply(txn, report, hid->base->num_reports);
}

static zx_status_t fidl_GetReportSize(void* ctx, fuchsia_hardware_input_ReportType type, uint8_t id,
                                      fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    input_report_size_t size = hid_get_report_size_by_id(hid->base, id, type);
    return fuchsia_hardware_input_DeviceGetReportSize_reply(
        txn, size == 0 ? ZX_ERR_NOT_FOUND : ZX_OK, size);
}

static zx_status_t fidl_GetMaxInputReportSize(void* ctx, fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    return fuchsia_hardware_input_DeviceGetMaxInputReportSize_reply(
        txn, get_max_input_reportsize(hid->base));
}

static zx_status_t fidl_GetReport(void* ctx, fuchsia_hardware_input_ReportType type, uint8_t id,
                                  fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    input_report_size_t needed = hid_get_report_size_by_id(hid->base, id, type);
    if (needed == 0) {
        return fuchsia_hardware_input_DeviceGetReport_reply(txn, ZX_ERR_NOT_FOUND, NULL, 0);
    }

    uint8_t report[needed];
    size_t actual = 0;
    zx_status_t status = hid_op_get_report(hid->base, type, id, report, needed, &actual);
    return fuchsia_hardware_input_DeviceGetReport_reply(txn, status, report, actual);
}

static zx_status_t fidl_SetReport(void* ctx, fuchsia_hardware_input_ReportType type, uint8_t id,
                                  const uint8_t* report, size_t report_len, fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    input_report_size_t needed = hid_get_report_size_by_id(hid->base, id, type);
    if (needed < report_len) {
        return fuchsia_hardware_input_DeviceSetReport_reply(txn, ZX_ERR_BUFFER_TOO_SMALL);
    }
    zx_status_t status = hid_op_set_report(hid->base, type, id, report, report_len);
    return fuchsia_hardware_input_DeviceSetReport_reply(txn, status);
}

static zx_status_t fidl_SetTraceId(void* ctx, uint32_t id) {
    hid_instance_t* hid = ctx;
    hid->trace_id = id;
    return ZX_OK;
}

static fuchsia_hardware_input_Device_ops_t fidl_ops = {
    .GetBootProtocol = fidl_GetBootProtocol,
    .GetReportDescSize = fidl_GetReportDescSize,
    .GetReportDesc = fidl_GetReportDesc,
    .GetNumReports = fidl_GetNumReports,
    .GetReportIds = fidl_GetReportIds,
    .GetReportSize = fidl_GetReportSize,
    .GetMaxInputReportSize = fidl_GetMaxInputReportSize,
    .GetReport = fidl_GetReport,
    .SetReport = fidl_SetReport,
    .SetTraceId = fidl_SetTraceId,
};

static zx_status_t hid_message_instance(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    if (hid->flags & HID_FLAGS_DEAD) {
        return ZX_ERR_PEER_CLOSED;
    }

    return fuchsia_hardware_input_Device_dispatch(ctx, txn, msg, &fidl_ops);
}

zx_protocol_device_t hid_instance_proto = {
    .version = DEVICE_OPS_VERSION,
    .read = hid_read_instance,
    .close = hid_close_instance,
    .message = hid_message_instance,
    .release = hid_release_instance,
};

static void hid_dump_hid_report_desc(hid_device_t* dev) {
    zxlogf(TRACE, "hid: dev %p HID report descriptor\n", dev);
    for (size_t c = 0; c < dev->hid_report_desc_len; c++) {
        zxlogf(TRACE, "%02x ", dev->hid_report_desc[c]);
        if (c % 16 == 15) zxlogf(ERROR, "\n");
    }
    zxlogf(TRACE, "\n");
    zxlogf(TRACE, "hid: num reports: %zd\n", dev->num_reports);
    for (size_t i = 0; i < dev->num_reports; i++) {
        zxlogf(TRACE, "  report id: %u  sizes: in %u out %u feat %u\n",
                dev->sizes[i].id, dev->sizes[i].in_size, dev->sizes[i].out_size,
                dev->sizes[i].feat_size);
    }
}

void hid_reports_set_boot_mode(hid_reports_t *reports) {
    reports->num_reports = 1;
    reports->sizes[0].id = 0;
    reports->sizes[0].in_size = 24;
    reports->sizes[0].out_size = 0;
    reports->sizes[0].feat_size = 0;
    reports->has_rpt_id = false;
}

zx_status_t hid_process_hid_report_desc(hid_device_t* dev) {
    hid_reports_t reports;
    reports.num_reports = 0;
    reports.sizes_len = HID_MAX_REPORT_IDS;
    reports.sizes = dev->sizes;
    reports.has_rpt_id = false;

    zx_status_t status = hid_lib_parse_reports(dev->hid_report_desc, dev->hid_report_desc_len, &reports);
    if (status == ZX_OK) {

#if BOOT_MOUSE_HACK
        // Ignore the HID report descriptor from the device, since we're putting
        // the device into boot protocol mode.
        if (dev->info.device_class == HID_DEVICE_CLASS_POINTER) {
            if (dev->info.boot_device) {
                zxlogf(INFO, "hid: boot mouse hack for \"%s\":  "
                       "report count (%zu->1), "
                       "inp sz (%d->24), "
                       "out sz (%d->0), "
                       "feat sz (%d->0)\n",
                       dev->name, dev->num_reports, dev->sizes[0].in_size,
                       dev->sizes[0].out_size, dev->sizes[0].feat_size);
                hid_reports_set_boot_mode(&reports);
            } else {
                zxlogf(INFO,
                    "hid: boot mouse hack skipped for \"%s\": does not support protocol.\n",
                    dev->name);
            }
        }
#endif

        dev->num_reports = reports.num_reports;
        ZX_DEBUG_ASSERT(dev->num_reports <= countof(dev->sizes));
    }

    return status;
}

static void hid_release_reassembly_buffer(hid_device_t* dev) {
    if (dev->rbuf != NULL) {
        free(dev->rbuf);
    }

    dev->rbuf = NULL;
    dev->rbuf_size =  0;
    dev->rbuf_filled =  0;
    dev->rbuf_needed =  0;
}

static zx_status_t hid_init_reassembly_buffer(hid_device_t* dev) {
    ZX_DEBUG_ASSERT(dev->rbuf == NULL);
    ZX_DEBUG_ASSERT(dev->rbuf_size == 0);
    ZX_DEBUG_ASSERT(dev->rbuf_filled == 0);
    ZX_DEBUG_ASSERT(dev->rbuf_needed == 0);

    // TODO(johngro) : Take into account the underlying transport's ability to
    // deliver payloads.  For example, if this is a USB HID device operating at
    // full speed, we can expect it to deliver up to 64 bytes at a time.  If the
    // maximum HID input report size is only 60 bytes, we should not need a
    // reassembly buffer.
    input_report_size_t max_report_size = get_max_input_reportsize(dev);
    dev->rbuf = malloc(max_report_size);
    if (dev->rbuf == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    dev->rbuf_size = max_report_size;
    return ZX_OK;
}

static void hid_release_device(void* ctx) {
    hid_device_t* hid = ctx;

    if (hid->hid_report_desc) {
        free(hid->hid_report_desc);
        hid->hid_report_desc = NULL;
        hid->hid_report_desc_len = 0;
    }
    hid_release_reassembly_buffer(hid);
    free(hid);
}

static zx_status_t hid_open_device(void* ctx, zx_device_t** dev_out, uint32_t flags) {
    hid_device_t* hid = ctx;

    hid_instance_t* inst = calloc(1, sizeof(hid_instance_t));
    if (inst == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    zx_hid_fifo_init(&inst->fifo);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "hid",
        .ctx = inst,
        .ops = &hid_instance_proto,
        .proto_id = ZX_PROTOCOL_INPUT,
        .flags = DEVICE_ADD_INSTANCE,
    };

    zx_status_t status = status = device_add(hid->zxdev, &args, &inst->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "hid: error creating instance %d\n", status);
        free(inst);
        return status;
    }
    inst->base = hid;

    mtx_lock(&hid->instance_lock);
    list_add_tail(&hid->instance_list, &inst->node);
    mtx_unlock(&hid->instance_lock);

    *dev_out = inst->zxdev;
    return ZX_OK;
}

static void hid_unbind_device(void* ctx) {
    hid_device_t* hid = ctx;
    mtx_lock(&hid->instance_lock);
    hid_instance_t* instance;
    foreach_instance(hid, instance) {
        instance->flags |= HID_FLAGS_DEAD;
        device_state_set(instance->zxdev, DEV_STATE_READABLE);
    }
    mtx_unlock(&hid->instance_lock);
    device_remove(hid->zxdev);
}

zx_protocol_device_t hid_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .open = hid_open_device,
    .unbind = hid_unbind_device,
    .release = hid_release_device,
};

void hid_io_queue(void* cookie, const void* _buf, size_t len) {
    const uint8_t* buf = _buf;
    hid_device_t* hid = cookie;

    TRACE_DURATION("input", "HID IO Queue");

    mtx_lock(&hid->instance_lock);

    while (len) {
        // Start by figuring out if this payload either completes a partially
        // assembled input report or represents an entire input buffer report on
        // its own.
        const uint8_t* rbuf;
        size_t rlen;
        size_t consumed;

        if (hid->rbuf_needed) {
            // Reassembly is in progress, just continue the process.
            consumed = MIN(len, hid->rbuf_needed);
            ZX_DEBUG_ASSERT (hid->rbuf_size >= hid->rbuf_filled);
            ZX_DEBUG_ASSERT((hid->rbuf_size - hid->rbuf_filled) >= consumed);

            memcpy(hid->rbuf + hid->rbuf_filled, buf, consumed);

            if (consumed == hid->rbuf_needed) {
                // reassembly finished.  Reset the bookkeeping and deliver the
                // payload.
                rbuf = hid->rbuf;
                rlen = hid->rbuf_filled + consumed;
                hid->rbuf_filled = 0;
                hid->rbuf_needed = 0;
            } else {
                // We have not finished the process yet.  Update the bookkeeping
                // and get out.
                hid->rbuf_filled += consumed;
                hid->rbuf_needed -= consumed;
                break;
            }
        } else {
            // No reassembly is in progress.  Start by identifying this report's
            // size.
            size_t rpt_sz =
                hid_get_report_size_by_id(hid, buf[0], fuchsia_hardware_input_ReportType_INPUT);

            // If we don't recognize this report ID, we are in trouble.  Drop
            // the rest of this payload and hope that the next one gets us back
            // on track.
            if (!rpt_sz) {
                zxlogf(ERROR, "%s: failed to find input report size (report id %u)\n",
                        hid->name, buf[0]);
                break;
            }

            // Is the entire report present in this payload?  If so, just go
            // ahead an deliver it directly from the input buffer.
            if (len >= rpt_sz) {
                rbuf = buf;
                consumed = rlen = rpt_sz;
            } else {
                // Looks likes our report is fragmented over multiple buffers.
                // Start the process of reassembly and get out.
                ZX_DEBUG_ASSERT(hid->rbuf != NULL);
                ZX_DEBUG_ASSERT(hid->rbuf_size >= rpt_sz);
                memcpy(hid->rbuf, buf, len);
                hid->rbuf_filled = len;
                hid->rbuf_needed = rpt_sz - len;
                break;
            }
        }

        ZX_DEBUG_ASSERT(rbuf != NULL);
        ZX_DEBUG_ASSERT(consumed <= len);
        buf += consumed;
        len -= consumed;

        hid_instance_t* instance;
        foreach_instance(hid, instance) {
            mtx_lock(&instance->fifo.lock);
            bool was_empty = zx_hid_fifo_size(&instance->fifo) == 0;
            ssize_t wrote = zx_hid_fifo_write(&instance->fifo, rbuf, rlen);

            if (wrote <= 0) {
                if (!(instance->flags & HID_FLAGS_WRITE_FAILED)) {
                    zxlogf(ERROR, "%s: could not write to hid fifo (ret=%zd)\n",
                            hid->name, wrote);
                    instance->flags |= HID_FLAGS_WRITE_FAILED;
                }
            } else {
                TRACE_FLOW_BEGIN(
                    "input", "hid_report",
                    HID_REPORT_TRACE_ID(instance->trace_id,
                                        instance->reports_written));
                ++instance->reports_written;
                instance->flags &= ~HID_FLAGS_WRITE_FAILED;
                if (was_empty) {
                    device_state_set(instance->zxdev, DEV_STATE_READABLE);
                }
            }
            mtx_unlock(&instance->fifo.lock);
        }
    }

    mtx_unlock(&hid->instance_lock);
}

hidbus_ifc_protocol_ops_t hid_ifc_ops = {
    .io_queue = hid_io_queue,
};

static zx_status_t hid_bind(void* ctx, zx_device_t* parent) {
    hid_device_t* hiddev;
    if ((hiddev = calloc(1, sizeof(hid_device_t))) == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status;
    if (device_get_protocol(parent, ZX_PROTOCOL_HIDBUS, &hiddev->hid)) {
        zxlogf(ERROR, "hid: bind: no hidbus protocol\n");
        status = ZX_ERR_INTERNAL;
        goto fail;
    }

    if ((status = hid_op_query(hiddev, 0, &hiddev->info)) < 0) {
        zxlogf(ERROR, "hid: bind: hidbus query failed: %d\n", status);
        goto fail;
    }

    mtx_init(&hiddev->instance_lock, mtx_plain);
    list_initialize(&hiddev->instance_list);

    snprintf(hiddev->name, sizeof(hiddev->name), "hid-device-%03d", hiddev->info.dev_num);
    hiddev->name[ZX_DEVICE_NAME_MAX] = 0;

    if (hiddev->info.boot_device) {
        status = hid_op_set_protocol(hiddev, HID_PROTOCOL_BOOT);
        if (status != ZX_OK) {
            zxlogf(ERROR, "hid: could not put HID device into boot protocol: %d\n", status);
            goto fail;
        }

        // Disable numlock
        if (hiddev->info.device_class == HID_DEVICE_CLASS_KBD) {
            uint8_t zero = 0;
            hid_op_set_report(hiddev, HID_REPORT_TYPE_OUTPUT, 0, &zero, sizeof(zero));
            // ignore failure for now
        }
    }

    status = hid_op_get_descriptor(hiddev, HID_DESCRIPTION_TYPE_REPORT,
            (void**)&hiddev->hid_report_desc, &hiddev->hid_report_desc_len);
    if (status != ZX_OK) {
        zxlogf(ERROR, "hid: could not retrieve HID report descriptor: %d\n", status);
        goto fail;
    }

    status = hid_process_hid_report_desc(hiddev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "hid: could not parse hid report descriptor: %d\n", status);
        goto fail;
    }
    hid_dump_hid_report_desc(hiddev);

    status = hid_init_reassembly_buffer(hiddev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "hid: failed to initialize reassembly buffer: %d\n", status);
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = hiddev->name,
        .ctx = hiddev,
        .ops = &hid_device_proto,
        .proto_id = ZX_PROTOCOL_INPUT,
    };

    status = device_add(parent, &args, &hiddev->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "hid: device_add failed for HID device: %d\n", status);
        goto fail;
    }

    // TODO: delay calling start until we've been opened by someone
    status = hid_op_start(hiddev, hiddev, &hid_ifc_ops);
    if (status != ZX_OK) {
        zxlogf(ERROR, "hid: could not start hid device: %d\n", status);
        device_remove(hiddev->zxdev);
        // Don't fail, since we've been added. Need to let devmgr clean us up.
        return status;
    }

    status = hid_op_set_idle(hiddev, 0, 0);
    if (status != ZX_OK) {
        zxlogf(TRACE, "hid: [W] set_idle failed for %s: %d\n", hiddev->name, status);
        // continue anyway
    }
    return ZX_OK;

fail:
    hid_release_reassembly_buffer(hiddev);
    free(hiddev);
    return status;
}

static zx_driver_ops_t hid_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = hid_bind,
};

ZIRCON_DRIVER_BEGIN(hid, hid_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_HIDBUS),
ZIRCON_DRIVER_END(hid)
