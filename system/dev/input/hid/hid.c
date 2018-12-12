// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hid-fifo.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/hidbus.h>
#include <zircon/input/c/fidl.h>

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

typedef uint8_t input_report_id_t;
typedef uint16_t input_report_size_t;

typedef struct hid_report_size {
    uint8_t id;
    input_report_size_t in_size;
    input_report_size_t out_size;
    input_report_size_t feat_size;
} hid_report_size_t;

typedef struct hid_reports {
    hid_report_size_t *sizes;
    size_t sizes_len;
    size_t num_reports;
    bool has_rpt_id;
} hid_reports_t;

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

    struct list_node node;
} hid_instance_t;

// Convenience functions for calling hidbus protocol functions

static inline zx_status_t hid_op_query(hid_device_t* hid, uint32_t options, hid_info_t* info) {
    return hid->hid.ops->query(hid->hid.ctx, options, info);
}

static inline zx_status_t hid_op_start(hid_device_t* hid, const hidbus_ifc_t* ifc) {
    return hidbus_start(&hid->hid, ifc);
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


static input_report_size_t hid_get_report_size_by_id(hid_device_t* hid,
                                                     input_report_id_t id,
                                                     zircon_input_ReportType type) {
    for (size_t i = 0; i < hid->num_reports; i++) {
        if ((hid->sizes[i].id == id) || (hid->num_reports == 1)) {
            switch (type) {
            case zircon_input_ReportType_INPUT:
                return bits_to_bytes(hid->sizes[i].in_size);
            case zircon_input_ReportType_OUTPUT:
                return bits_to_bytes(hid->sizes[i].out_size);
            case zircon_input_ReportType_FEATURE:
                return bits_to_bytes(hid->sizes[i].feat_size);
            }
        }
    }

    return 0;
}

static zircon_input_BootProtocol get_boot_protocol(hid_device_t* hid) {
    if (hid->info.device_class == HID_DEVICE_CLASS_KBD ||
        hid->info.device_class == HID_DEVICE_CLASS_KBD_POINTER) {
        return zircon_input_BootProtocol_KBD;
    } else if (hid->info.device_class == HID_DEVICE_CLASS_POINTER) {
        return zircon_input_BootProtocol_MOUSE;
    }
    return zircon_input_BootProtocol_NONE;
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

    xfer = hid_get_report_size_by_id(hid->base, rpt_id, zircon_input_ReportType_INPUT);
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
    return zircon_input_DeviceGetBootProtocol_reply(txn, get_boot_protocol(hid->base));
}

static zx_status_t fidl_GetReportDescSize(void* ctx, fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    return zircon_input_DeviceGetReportDescSize_reply(txn, hid->base->hid_report_desc_len);
}

static zx_status_t fidl_GetReportDesc(void* ctx, fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    return zircon_input_DeviceGetReportDesc_reply(txn, hid->base->hid_report_desc,
                                                  hid->base->hid_report_desc_len);
}

static zx_status_t fidl_GetNumReports(void* ctx, fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    return zircon_input_DeviceGetNumReports_reply(txn, hid->base->num_reports);
}

static zx_status_t fidl_GetReportIds(void* ctx, fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    uint8_t report[zircon_input_MAX_REPORT_IDS];
    for (size_t i = 0; i < hid->base->num_reports; i++) {
        report[i] = hid->base->sizes[i].id;
    }
    return zircon_input_DeviceGetReportIds_reply(txn, report, hid->base->num_reports);
}

static zx_status_t fidl_GetReportSize(void* ctx, zircon_input_ReportType type, uint8_t id,
                                      fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    input_report_size_t size = hid_get_report_size_by_id(hid->base, id, type);
    return zircon_input_DeviceGetReportSize_reply(txn, size == 0 ? ZX_ERR_NOT_FOUND : ZX_OK, size);
}

static zx_status_t fidl_GetMaxInputReportSize(void* ctx, fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    return zircon_input_DeviceGetMaxInputReportSize_reply(txn, get_max_input_reportsize(hid->base));
}

static zx_status_t fidl_GetReport(void* ctx, zircon_input_ReportType type, uint8_t id,
                                  fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    input_report_size_t needed = hid_get_report_size_by_id(hid->base, id, type);
    if (needed == 0) {
        return zircon_input_DeviceGetReport_reply(txn, ZX_ERR_NOT_FOUND, NULL, 0);
    }

    uint8_t report[needed];
    size_t actual = 0;
    zx_status_t status = hid_op_get_report(hid->base, type, id, report, needed, &actual);
    return zircon_input_DeviceGetReport_reply(txn, status, report, actual);
}

static zx_status_t fidl_SetReport(void* ctx, zircon_input_ReportType type, uint8_t id,
                                  const uint8_t* report, size_t report_len, fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    input_report_size_t needed = hid_get_report_size_by_id(hid->base, id, type);
    if (needed < report_len) {
        return zircon_input_DeviceSetReport_reply(txn, ZX_ERR_BUFFER_TOO_SMALL);
    }
    zx_status_t status = hid_op_set_report(hid->base, type, id, report, report_len);
    return zircon_input_DeviceSetReport_reply(txn, status);
}

static zircon_input_Device_ops_t fidl_ops = {
    .GetBootProtocol = fidl_GetBootProtocol,
    .GetReportDescSize = fidl_GetReportDescSize,
    .GetReportDesc = fidl_GetReportDesc,
    .GetNumReports = fidl_GetNumReports,
    .GetReportIds = fidl_GetReportIds,
    .GetReportSize = fidl_GetReportSize,
    .GetMaxInputReportSize = fidl_GetMaxInputReportSize,
    .GetReport = fidl_GetReport,
    .SetReport = fidl_SetReport,
};

static zx_status_t hid_message_instance(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
    hid_instance_t* hid = ctx;
    if (hid->flags & HID_FLAGS_DEAD) {
        return ZX_ERR_PEER_CLOSED;
    }

    return zircon_input_Device_dispatch(ctx, txn, msg, &fidl_ops);
}

zx_protocol_device_t hid_instance_proto = {
    .version = DEVICE_OPS_VERSION,
    .read = hid_read_instance,
    .close = hid_close_instance,
    .message = hid_message_instance,
    .release = hid_release_instance,
};

enum {
    HID_ITEM_TYPE_MAIN = 0,
    HID_ITEM_TYPE_GLOBAL = 1,
    HID_ITEM_TYPE_LOCAL = 2,
};

enum {
    HID_ITEM_MAIN_TAG_INPUT = 8,
    HID_ITEM_MAIN_TAG_OUTPUT = 9,
    HID_ITEM_MAIN_TAG_FEATURE = 11,
};

enum {
    HID_ITEM_GLOBAL_TAG_REPORT_SIZE = 7,
    HID_ITEM_GLOBAL_TAG_REPORT_ID = 8,
    HID_ITEM_GLOBAL_TAG_REPORT_COUNT = 9,
    HID_ITEM_GLOBAL_TAG_PUSH = 10,
    HID_ITEM_GLOBAL_TAG_POP = 11,
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

typedef struct hid_item {
    uint8_t bSize;
    uint8_t bType;
    uint8_t bTag;
    int64_t data;
} hid_item_t;

static const uint8_t* hid_parse_short_item(const uint8_t* buf, const uint8_t* end, hid_item_t* item) {
    switch (*buf & 0x3) {
    case 0:
        item->bSize = 0;
        break;
    case 1:
        item->bSize = 1;
        break;
    case 2:
        item->bSize = 2;
        break;
    case 3:
        item->bSize = 4;
        break;
    }
    item->bType = (*buf >> 2) & 0x3;
    item->bTag = (*buf >> 4) & 0x0f;
    if (buf + item->bSize >= end) {
        // Return a RESERVED item type, and point past the end of the buffer to
        // prevent further parsing.
        item->bType = 0x03;
        return end;
    }
    buf++;

    item->data = 0;
    for (uint8_t i = 0; i < item->bSize; i++) {
        item->data |= *buf << (8*i);
        buf++;
    }
    return buf;
}

static int hid_fetch_or_alloc_report_ndx(input_report_id_t report_id, hid_reports_t* reports) {
    ZX_DEBUG_ASSERT(reports->num_reports <= reports->sizes_len);
    for (size_t i = 0; i < reports->num_reports; i++) {
        if (reports->sizes[i].id == report_id)
            return i;
    }

    if (reports->num_reports < reports->sizes_len) {
        reports->sizes[reports->num_reports].id = report_id;
        ZX_DEBUG_ASSERT(reports->sizes[reports->num_reports].in_size == 0);
        ZX_DEBUG_ASSERT(reports->sizes[reports->num_reports].out_size == 0);
        ZX_DEBUG_ASSERT(reports->sizes[reports->num_reports].feat_size == 0);
        return reports->num_reports++;
    } else {
        return -1;
    }
}

typedef struct hid_global_state {
    uint32_t rpt_size;
    uint32_t rpt_count;
    input_report_id_t rpt_id;
    list_node_t node;
} hid_global_state_t;

static zx_status_t hid_push_global_state(list_node_t* stack, hid_global_state_t* state) {
    hid_global_state_t* entry = malloc(sizeof(*entry));
    if (entry == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    entry->rpt_size = state->rpt_size;
    entry->rpt_count = state->rpt_count;
    entry->rpt_id = state->rpt_id;
    list_add_tail(stack, &entry->node);
    return ZX_OK;
}

static zx_status_t hid_pop_global_state(list_node_t* stack, hid_global_state_t* state) {
    hid_global_state_t* entry = list_remove_tail_type(stack, hid_global_state_t, node);
    if (entry == NULL) {
        return ZX_ERR_BAD_STATE;
    }
    state->rpt_size = entry->rpt_size;
    state->rpt_count = entry->rpt_count;
    state->rpt_id = entry->rpt_id;
    free(entry);
    return ZX_OK;
}

static void hid_clear_global_state(list_node_t* stack) {
    hid_global_state_t* state, *tmp;
    list_for_every_entry_safe(stack, state, tmp, hid_global_state_t, node) {
        list_delete(&state->node);
        free(state);
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

zx_status_t hid_parse_reports(const uint8_t *buf, const size_t buf_len, hid_reports_t *reports) {
    const uint8_t* end = buf + buf_len;
    zx_status_t status = ZX_OK;
    hid_item_t item;

    hid_global_state_t state;
    memset(&state, 0, sizeof(state));
    list_node_t global_stack;
    list_initialize(&global_stack);
    while (buf < end) {
        buf = hid_parse_short_item(buf, end, &item);
        switch (item.bType) {
        case HID_ITEM_TYPE_MAIN: {
            input_report_size_t inc = state.rpt_size * state.rpt_count;
            int idx;
            switch (item.bTag) {
            case HID_ITEM_MAIN_TAG_INPUT:
                idx = hid_fetch_or_alloc_report_ndx(state.rpt_id, reports);
                if (idx < 0) {
                    status = ZX_ERR_NOT_SUPPORTED;
                    goto done;
                }
                reports->sizes[idx].in_size += inc;
                break;
            case HID_ITEM_MAIN_TAG_OUTPUT:
                idx = hid_fetch_or_alloc_report_ndx(state.rpt_id, reports);
                if (idx < 0) {
                    status = ZX_ERR_NOT_SUPPORTED;
                    goto done;
                }
                reports->sizes[idx].out_size += inc;
                break;
            case HID_ITEM_MAIN_TAG_FEATURE:
                idx = hid_fetch_or_alloc_report_ndx(state.rpt_id, reports);
                if (idx < 0) {
                    status = ZX_ERR_NOT_SUPPORTED;
                    goto done;
                }
                reports->sizes[idx].feat_size += inc;
                break;
            default:
                break;
            }
            break;  // case HID_ITEM_TYPE_MAIN
        }
        case HID_ITEM_TYPE_GLOBAL: {
            switch (item.bTag) {
            case HID_ITEM_GLOBAL_TAG_REPORT_SIZE:
                state.rpt_size = (uint32_t)item.data;
                break;
            case HID_ITEM_GLOBAL_TAG_REPORT_ID:
                state.rpt_id = (input_report_id_t)item.data;
                reports->has_rpt_id = true;
                break;
            case HID_ITEM_GLOBAL_TAG_REPORT_COUNT:
                state.rpt_count = (uint32_t)item.data;
                break;
            case HID_ITEM_GLOBAL_TAG_PUSH:
                status = hid_push_global_state(&global_stack, &state);
                if (status != ZX_OK) {
                    goto done;
                }
                break;
            case HID_ITEM_GLOBAL_TAG_POP:
                status = hid_pop_global_state(&global_stack, &state);
                if (status != ZX_OK) {
                    goto done;
                }
                break;
            default:
                break;
            }
            break;  // case HID_ITEM_TYPE_GLOBAL
        }
        default:
            break;
        }
    }
done:
    hid_clear_global_state(&global_stack);

    return status;
}


zx_status_t hid_process_hid_report_desc(hid_device_t* dev) {
    hid_reports_t reports;
    reports.num_reports = 0;
    reports.sizes_len = HID_MAX_REPORT_IDS;
    reports.sizes = dev->sizes;
    reports.has_rpt_id = false;

    zx_status_t status = hid_parse_reports(dev->hid_report_desc, dev->hid_report_desc_len, &reports);
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

        // If we saw a report ID, adjust the expected report sizes to reflect
        // the fact that we expect a report ID to be prepended to each report.
        ZX_DEBUG_ASSERT(dev->num_reports <= countof(dev->sizes));
        if (reports.has_rpt_id) {
            for (size_t i = 0; i < dev->num_reports; ++i) {
                if (dev->sizes[i].in_size)   dev->sizes[i].in_size   += 8;
                if (dev->sizes[i].out_size)  dev->sizes[i].out_size  += 8;
                if (dev->sizes[i].feat_size) dev->sizes[i].feat_size += 8;
            }
        }
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
            size_t rpt_sz = hid_get_report_size_by_id(hid, buf[0], zircon_input_ReportType_INPUT);

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

hidbus_ifc_ops_t hid_ifc_ops = {
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
    status = hid_op_start(hiddev, &(hidbus_ifc_t){&hid_ifc_ops, hiddev});
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
