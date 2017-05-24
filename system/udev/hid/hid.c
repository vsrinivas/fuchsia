// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/common/hid-fifo.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/hidbus.h>
#include <ddk/protocol/input.h>

#include <magenta/assert.h>
#include <magenta/listnode.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HID_FLAGS_DEAD         (1 << 0)
#define HID_FLAGS_WRITE_FAILED (1 << 1)

#define USB_HID_DEBUG 0

// TODO(johngro) : Get this from a standard header instead of defining our own.
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define foreach_instance(base, instance) \
    list_for_every_entry(&base->instance_list, instance, hid_instance_t, node)
#define bits_to_bytes(n) (((n) + 7) / 8)

// Until we do full HID parsing, we put mouse and keyboard devices into boot
// protocol mode. In particular, a mouse will always send 3 byte reports (see
// ddk/protocol/input.h for the format). This macro sets ioctl return values for
// boot mouse devices to reflect the boot protocol, rather than what the device
// itself reports.
// TODO: update this to include keyboards if we find a keyboard in the wild that
// needs a hack as well.
#define BOOT_MOUSE_HACK 1

typedef struct hid_report_size {
    int16_t id;
    input_report_size_t in_size;
    input_report_size_t out_size;
    input_report_size_t feat_size;
} hid_report_size_t;

typedef struct hid_device {
    mx_device_t* mxdev;

    mx_device_t* hid_mxdev;
    hid_info_t info;
    hidbus_protocol_t* hid_ops;

    // Reassembly buffer for input events too large to fit in a single interrupt
    // transaction.
    uint8_t* rbuf;
    size_t rbuf_size;
    size_t rbuf_filled;
    size_t rbuf_needed;

    size_t hid_report_desc_len;
    uint8_t* hid_report_desc;

#define HID_MAX_REPORT_IDS 16
    size_t num_reports;
    hid_report_size_t sizes[HID_MAX_REPORT_IDS];

    struct list_node instance_list;
    mtx_t instance_lock;

    char name[MX_DEVICE_NAME_MAX + 1];
} hid_device_t;

typedef struct hid_instance {
    mx_device_t* mxdev;
    hid_device_t* base;

    uint32_t flags;

    mx_hid_fifo_t fifo;

    struct list_node node;
} hid_instance_t;

// Convenience functions for calling hidbus protocol functions

static inline mx_status_t hid_op_query(hid_device_t* hid, uint32_t options, hid_info_t* info) {
    return hid->hid_ops->query(hid->hid_mxdev, options, info);
}

static inline mx_status_t hid_op_start(hid_device_t* hid, hidbus_ifc_t* ifc, void* cookie) {
    return hid->hid_ops->start(hid->hid_mxdev, ifc, cookie);
}

static inline void hid_op_stop(hid_device_t* hid) {
    hid->hid_ops->stop(hid->hid_mxdev);
}

static inline mx_status_t hid_op_get_descriptor(hid_device_t* hid, uint8_t desc_type,
                                                void** data, size_t* len) {
    return hid->hid_ops->get_descriptor(hid->hid_mxdev, desc_type, data, len);
}

static inline mx_status_t hid_op_get_report(hid_device_t* hid, uint8_t rpt_type, uint8_t rpt_id,
                                            void* data, size_t len) {
    return hid->hid_ops->get_report(hid->hid_mxdev, rpt_type, rpt_id, data, len);
}

static inline mx_status_t hid_op_set_report(hid_device_t* hid, uint8_t rpt_type, uint8_t rpt_id,
                                            void* data, size_t len) {
    return hid->hid_ops->set_report(hid->hid_mxdev, rpt_type, rpt_id, data, len);
}

static inline mx_status_t hid_op_get_idle(hid_device_t* hid, uint8_t rpt_id, uint8_t* duration) {
    return hid->hid_ops->get_idle(hid->hid_mxdev, rpt_id, duration);
}

static inline mx_status_t hid_op_set_idle(hid_device_t* hid, uint8_t rpt_id, uint8_t duration) {
    return hid->hid_ops->set_idle(hid->hid_mxdev, rpt_id, duration);
}

static inline mx_status_t hid_op_get_protocol(hid_device_t* hid, uint8_t* protocol) {
    return hid->hid_ops->get_protocol(hid->hid_mxdev, protocol);
}

static inline mx_status_t hid_op_set_protocol(hid_device_t* hid, uint8_t protocol) {
    return hid->hid_ops->set_protocol(hid->hid_mxdev, protocol);
}


static input_report_size_t hid_get_report_size_by_id(hid_device_t* hid,
                                                     input_report_id_t id,
                                                     input_report_type_t type) {
    for (size_t i = 0; i < hid->num_reports; i++) {
        if ((hid->sizes[i].id == id) || (hid->num_reports == 1)) {
            switch (type) {
            case INPUT_REPORT_INPUT:
                return bits_to_bytes(hid->sizes[i].in_size);
            case INPUT_REPORT_OUTPUT:
                return bits_to_bytes(hid->sizes[i].out_size);
            case INPUT_REPORT_FEATURE:
                return bits_to_bytes(hid->sizes[i].feat_size);
            }
        }
    }

    return 0;
}

static mx_status_t hid_get_protocol(hid_device_t* hid, void* out_buf, size_t out_len,
                                    size_t* out_actual) {
    if (out_len < sizeof(int)) return ERR_INVALID_ARGS;

    int* reply = out_buf;
    *reply = INPUT_PROTO_NONE;
    if (hid->info.dev_class == HID_DEV_CLASS_KBD || hid->info.dev_class == HID_DEV_CLASS_KBD_POINTER) {
        *reply = INPUT_PROTO_KBD;
    } else if (hid->info.dev_class == HID_DEV_CLASS_POINTER) {
        *reply = INPUT_PROTO_MOUSE;
    }
    *out_actual = sizeof(*reply);
    return NO_ERROR;
}

static mx_status_t hid_get_hid_desc_size(hid_device_t* hid, void* out_buf, size_t out_len,
                                         size_t* out_actual) {
    if (out_len < sizeof(size_t)) return ERR_INVALID_ARGS;

    size_t* reply = out_buf;
    *reply = hid->hid_report_desc_len;
    *out_actual = sizeof(*reply);
    return NO_ERROR;
}

static mx_status_t hid_get_hid_desc(hid_device_t* hid, void* out_buf, size_t out_len,
                                    size_t* out_actual) {
    if (out_len < hid->hid_report_desc_len) return ERR_INVALID_ARGS;

    memcpy(out_buf, hid->hid_report_desc, hid->hid_report_desc_len);
    *out_actual = hid->hid_report_desc_len;
    return NO_ERROR;
}

static mx_status_t hid_get_num_reports(hid_device_t* hid, void* out_buf, size_t out_len,
                                       size_t* out_actual) {
    if (out_len < sizeof(size_t)) return ERR_INVALID_ARGS;

    size_t* reply = out_buf;
    *reply = hid->num_reports;
    *out_actual = sizeof(*reply);
    return NO_ERROR;
}

static mx_status_t hid_get_report_ids(hid_device_t* hid, void* out_buf, size_t out_len,
                                      size_t* out_actual) {
    if (out_len < hid->num_reports * sizeof(input_report_id_t))
        return ERR_INVALID_ARGS;

    input_report_id_t* reply = out_buf;
    for (size_t i = 0; i < hid->num_reports; i++) {
        *reply++ = (input_report_id_t)hid->sizes[i].id;
    }
    *out_actual =  hid->num_reports * sizeof(input_report_id_t);
    return NO_ERROR;
}

static mx_status_t hid_get_report_size(hid_device_t* hid, const void* in_buf, size_t in_len,
                                       void* out_buf, size_t out_len, size_t* out_actual) {
    if (in_len < sizeof(input_get_report_size_t)) return ERR_INVALID_ARGS;
    if (out_len < sizeof(input_report_size_t)) return ERR_INVALID_ARGS;

    const input_get_report_size_t* inp = in_buf;

    input_report_size_t* reply = out_buf;
    *reply = hid_get_report_size_by_id(hid, inp->id, inp->type);
    if (*reply == 0) {
        return ERR_INVALID_ARGS;
    }

    *out_actual = sizeof(*reply);
    return NO_ERROR;
}

static ssize_t hid_get_max_input_reportsize(hid_device_t* hid, void* out_buf, size_t out_len,
                                            size_t* out_actual) {
    if (out_len < sizeof(input_report_size_t)) return ERR_INVALID_ARGS;

    input_report_size_t* reply = out_buf;

    *reply = 0;
    for (size_t i = 0; i < hid->num_reports; i++) {
        if (hid->sizes[i].in_size > *reply)
            *reply = hid->sizes[i].in_size;
    }

    *reply = bits_to_bytes(*reply);
    *out_actual = sizeof(*reply);
    return NO_ERROR;
}

static mx_status_t hid_get_report(hid_device_t* hid, const void* in_buf, size_t in_len,
                                  void* out_buf, size_t out_len, size_t* out_actual) {
    if (in_len < sizeof(input_get_report_t)) return ERR_INVALID_ARGS;
    const input_get_report_t* inp = in_buf;

    input_report_size_t needed = hid_get_report_size_by_id(hid, inp->id, inp->type);
    if (needed == 0) return ERR_INVALID_ARGS;
    if (out_len < (size_t)needed) return ERR_BUFFER_TOO_SMALL;

    mx_status_t status = hid_op_get_report(hid, inp->type, inp->id, out_buf, out_len);
    if (status >= 0) {
        *out_actual = status;
        status = NO_ERROR;
    }
    return status;
}

static mx_status_t hid_set_report(hid_device_t* hid, const void* in_buf, size_t in_len) {

    if (in_len < sizeof(input_set_report_t)) return ERR_INVALID_ARGS;
    const input_set_report_t* inp = in_buf;

    input_report_size_t needed = hid_get_report_size_by_id(hid, inp->id, inp->type);
    if (needed == 0) return ERR_INVALID_ARGS;
    if (in_len - sizeof(input_set_report_t) < (size_t)needed) return ERR_INVALID_ARGS;

    return hid_op_set_report(hid, inp->type, inp->id, (void*)inp->data,
                             in_len - sizeof(input_set_report_t));
}


static mx_status_t hid_read_instance(void* ctx, void* buf, size_t count, mx_off_t off,
                                     size_t* actual) {
    hid_instance_t* hid = ctx;

    if (hid->flags & HID_FLAGS_DEAD) {
        return ERR_PEER_CLOSED;
    }

    size_t left;
    mtx_lock(&hid->fifo.lock);
    size_t xfer;
    uint8_t rpt_id;
    ssize_t r = mx_hid_fifo_peek(&hid->fifo, &rpt_id);
    if (r < 1) {
        // fifo is empty
        mtx_unlock(&hid->fifo.lock);
        return ERR_SHOULD_WAIT;
    }

    xfer = hid_get_report_size_by_id(hid->base, rpt_id, INPUT_REPORT_INPUT);
    if (xfer == 0) {
        printf("error reading hid device: unknown report id (%u)!\n", rpt_id);
        mtx_unlock(&hid->fifo.lock);
        return ERR_BAD_STATE;
    }

    if (xfer > count) {
        printf("next report: %zd, read count: %zd\n", xfer, count);
        mtx_unlock(&hid->fifo.lock);
        return ERR_BUFFER_TOO_SMALL;
    }

    r = mx_hid_fifo_read(&hid->fifo, buf, xfer);
    left = mx_hid_fifo_size(&hid->fifo);
    if (left == 0) {
        device_state_clr(hid->mxdev, DEV_STATE_READABLE);
    }
    mtx_unlock(&hid->fifo.lock);
    if (r > 0) {
        *actual = r;
        r = NO_ERROR;
    } else if (r == 0) {
        r = ERR_SHOULD_WAIT;
    }
    return r;
}

static mx_status_t hid_ioctl_instance(void* ctx, uint32_t op,
        const void* in_buf, size_t in_len, void* out_buf, size_t out_len, size_t* out_actual) {
    hid_instance_t* hid = ctx;
    if (hid->flags & HID_FLAGS_DEAD) return ERR_PEER_CLOSED;

    switch (op) {
    case IOCTL_INPUT_GET_PROTOCOL:
        return hid_get_protocol(hid->base, out_buf, out_len, out_actual);
    case IOCTL_INPUT_GET_REPORT_DESC_SIZE:
        return hid_get_hid_desc_size(hid->base, out_buf, out_len, out_actual);
    case IOCTL_INPUT_GET_REPORT_DESC:
        return hid_get_hid_desc(hid->base, out_buf, out_len, out_actual);
    case IOCTL_INPUT_GET_NUM_REPORTS:
        return hid_get_num_reports(hid->base, out_buf, out_len, out_actual);
    case IOCTL_INPUT_GET_REPORT_IDS:
        return hid_get_report_ids(hid->base, out_buf, out_len, out_actual);
    case IOCTL_INPUT_GET_REPORT_SIZE:
        return hid_get_report_size(hid->base, in_buf, in_len, out_buf, out_len, out_actual);
    case IOCTL_INPUT_GET_MAX_REPORTSIZE:
        return hid_get_max_input_reportsize(hid->base, out_buf, out_len, out_actual);
    case IOCTL_INPUT_GET_REPORT:
        return hid_get_report(hid->base, in_buf, in_len, out_buf, out_len, out_actual);
    case IOCTL_INPUT_SET_REPORT:
        return hid_set_report(hid->base, in_buf, in_len);
    }
    return ERR_NOT_SUPPORTED;
}

static mx_status_t hid_close_instance(void* ctx, uint32_t flags) {
    hid_instance_t* hid = ctx;
    hid->flags |= HID_FLAGS_DEAD;
    mtx_lock(&hid->base->instance_lock);
    // TODO: refcount the base device and call stop if no instances are open
    list_delete(&hid->node);
    mtx_unlock(&hid->base->instance_lock);
    return NO_ERROR;
}

static void hid_release_reassembly_buffer(hid_device_t* dev);

static void hid_release_instance(void* ctx) {
    hid_instance_t* hid = ctx;
    free(hid);
}

mx_protocol_device_t hid_instance_proto = {
    .version = DEVICE_OPS_VERSION,
    .read = hid_read_instance,
    .ioctl = hid_ioctl_instance,
    .close = hid_close_instance,
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
    printf("hid: dev %p HID report descriptor\n", dev);
    for (size_t c = 0; c < dev->hid_report_desc_len; c++) {
        printf("%02x ", dev->hid_report_desc[c]);
        if (c % 16 == 15) printf("\n");
    }
    printf("\n");
    printf("hid: num reports: %zd\n", dev->num_reports);
    for (size_t i = 0; i < dev->num_reports; i++) {
        printf("  report id: %u  sizes: in %u out %u feat %u\n",
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

static int hid_fetch_or_alloc_report_ndx(input_report_id_t report_id, hid_device_t* dev) {
    MX_DEBUG_ASSERT(dev->num_reports <= countof(dev->sizes));
    for (size_t i = 0; i < dev->num_reports; i++) {
        if (dev->sizes[i].id == report_id)
            return i;
    }

    if (dev->num_reports < countof(dev->sizes)) {
        dev->sizes[dev->num_reports].id = report_id;
        MX_DEBUG_ASSERT(dev->sizes[dev->num_reports].in_size == 0);
        MX_DEBUG_ASSERT(dev->sizes[dev->num_reports].out_size == 0);
        MX_DEBUG_ASSERT(dev->sizes[dev->num_reports].feat_size == 0);
        return dev->num_reports++;
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

static mx_status_t hid_push_global_state(list_node_t* stack, hid_global_state_t* state) {
    hid_global_state_t* entry = malloc(sizeof(*entry));
    if (entry == NULL) {
        return ERR_NO_MEMORY;
    }
    entry->rpt_size = state->rpt_size;
    entry->rpt_count = state->rpt_count;
    entry->rpt_id = state->rpt_id;
    list_add_tail(stack, &entry->node);
    return NO_ERROR;
}

static mx_status_t hid_pop_global_state(list_node_t* stack, hid_global_state_t* state) {
    hid_global_state_t* entry = list_remove_tail_type(stack, hid_global_state_t, node);
    if (entry == NULL) {
        return ERR_BAD_STATE;
    }
    state->rpt_size = entry->rpt_size;
    state->rpt_count = entry->rpt_count;
    state->rpt_id = entry->rpt_id;
    free(entry);
    return NO_ERROR;
}

static void hid_clear_global_state(list_node_t* stack) {
    hid_global_state_t* state, *tmp;
    list_for_every_entry_safe(stack, state, tmp, hid_global_state_t, node) {
        list_delete(&state->node);
        free(state);
    }
}

static mx_status_t hid_process_hid_report_desc(hid_device_t* dev) {
    const uint8_t* buf = dev->hid_report_desc;
    const uint8_t* end = buf + dev->hid_report_desc_len;
    mx_status_t status = NO_ERROR;
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
                idx = hid_fetch_or_alloc_report_ndx(state.rpt_id, dev);
                if (idx < 0) {
                    status = ERR_NOT_SUPPORTED;
                    goto done;
                }
                dev->sizes[idx].in_size += inc;
                break;
            case HID_ITEM_MAIN_TAG_OUTPUT:
                idx = hid_fetch_or_alloc_report_ndx(state.rpt_id, dev);
                if (idx < 0) {
                    status = ERR_NOT_SUPPORTED;
                    goto done;
                }
                dev->sizes[idx].out_size += inc;
                break;
            case HID_ITEM_MAIN_TAG_FEATURE:
                idx = hid_fetch_or_alloc_report_ndx(state.rpt_id, dev);
                if (idx < 0) {
                    status = ERR_NOT_SUPPORTED;
                    goto done;
                }
                dev->sizes[idx].feat_size += inc;
                break;
            default:
                break;
            }
            break;
        }
        case HID_ITEM_TYPE_GLOBAL:
            switch (item.bTag) {
            case HID_ITEM_GLOBAL_TAG_REPORT_SIZE:
                state.rpt_size = (uint32_t)item.data;
                break;
            case HID_ITEM_GLOBAL_TAG_REPORT_ID:
                state.rpt_id = (input_report_id_t)item.data;
                break;
            case HID_ITEM_GLOBAL_TAG_REPORT_COUNT:
                state.rpt_count = (uint32_t)item.data;
                break;
            case HID_ITEM_GLOBAL_TAG_PUSH:
                status = hid_push_global_state(&global_stack, &state);
                if (status != NO_ERROR) {
                    goto done;
                }
                break;
            case HID_ITEM_GLOBAL_TAG_POP:
                status = hid_pop_global_state(&global_stack, &state);
                if (status != NO_ERROR) {
                    goto done;
                }
                break;
            default:
                break;
            }
        default:
            break;
        }
    }
done:
    hid_clear_global_state(&global_stack);

    if (status == NO_ERROR) {
#if BOOT_MOUSE_HACK
        // Ignore the HID report descriptor from the device, since we're putting
        // the device into boot protocol mode.
        if (dev->info.dev_class == HID_DEV_CLASS_POINTER) {
            printf("hid: Applying boot mouse hack to hid device \"%s\".  "
                   "Altering report count (%zu -> 1)\n",
                   dev->name, dev->num_reports);
            dev->num_reports = 1;
            dev->sizes[0].id = 0;
            dev->sizes[0].in_size = 24;
            dev->sizes[0].out_size = 0;
            dev->sizes[0].feat_size = 0;
        }
#endif
        // If we have more than one defined report ID, adjust the expected
        // report sizes to reflect the fact that we expect a report ID to be
        // prepended to each report.
        MX_DEBUG_ASSERT(dev->num_reports <= countof(dev->sizes));
        if (dev->num_reports > 1) {
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

static mx_status_t hid_init_reassembly_buffer(hid_device_t* dev) {
    MX_DEBUG_ASSERT(dev->rbuf == NULL);
    MX_DEBUG_ASSERT(dev->rbuf_size == 0);
    MX_DEBUG_ASSERT(dev->rbuf_filled == 0);
    MX_DEBUG_ASSERT(dev->rbuf_needed == 0);

    // TODO(johngro) : Take into account the underlying transport's ability to
    // deliver payloads.  For example, if this is a USB HID device operating at
    // full speed, we can expect it to deliver up to 64 bytes at a time.  If the
    // maximum HID input report size is only 60 bytes, we should not need a
    // reassembly buffer.
    input_report_size_t max_report_size = 0;
    size_t actual = 0;
    mx_status_t res = hid_get_max_input_reportsize(dev, &max_report_size, sizeof(max_report_size),
                                                   &actual);
    if (res < 0) {
        return res;
    } else if (!max_report_size || actual != sizeof(max_report_size)) {
        return ERR_INTERNAL;
    }

    dev->rbuf = malloc(max_report_size);
    if (dev->rbuf == NULL) {
        return ERR_NO_MEMORY;
    }

    dev->rbuf_size = max_report_size;
    return NO_ERROR;
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

static mx_status_t hid_open_device(void* ctx, mx_device_t** dev_out, uint32_t flags) {
    hid_device_t* hid = ctx;

    hid_instance_t* inst = calloc(1, sizeof(hid_instance_t));
    if (inst == NULL) {
        return ERR_NO_MEMORY;
    }
    mx_hid_fifo_init(&inst->fifo);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "hid",
        .ctx = inst,
        .ops = &hid_instance_proto,
        .proto_id = MX_PROTOCOL_INPUT,
        .flags = DEVICE_ADD_INSTANCE,
    };

    mx_status_t status = status = device_add(hid->mxdev, &args, &inst->mxdev);
    if (status != NO_ERROR) {
        printf("hid: error creating instance %d\n", status);
        free(inst);
        return status;
    }
    inst->base = hid;

    mtx_lock(&hid->instance_lock);
    list_add_tail(&hid->instance_list, &inst->node);
    mtx_unlock(&hid->instance_lock);

    *dev_out = inst->mxdev;
    return NO_ERROR;
}

static void hid_unbind_device(void* ctx) {
    hid_device_t* hid = ctx;
    mtx_lock(&hid->instance_lock);
    hid_instance_t* instance;
    foreach_instance(hid, instance) {
        instance->flags |= HID_FLAGS_DEAD;
        device_state_set(instance->mxdev, DEV_STATE_READABLE);
    }
    mtx_unlock(&hid->instance_lock);
    device_remove(hid->mxdev);
}

mx_protocol_device_t hid_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .open = hid_open_device,
    .unbind = hid_unbind_device,
    .release = hid_release_device,
};

void hid_io_queue(void* cookie, const uint8_t* buf, size_t len) {
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
            MX_DEBUG_ASSERT (hid->rbuf_size >= hid->rbuf_filled);
            MX_DEBUG_ASSERT((hid->rbuf_size - hid->rbuf_filled) >= consumed);

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
            size_t  rpt_sz = hid_get_report_size_by_id(hid, buf[0], INPUT_REPORT_INPUT);

            // If we don't recognize this report ID, we are in trouble.  Drop
            // the rest of this payload and hope that the next one gets us back
            // on track.
            if (!rpt_sz) {
                printf("%s: failed to find input report size (report id %u)\n",
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
                MX_DEBUG_ASSERT(hid->rbuf != NULL);
                MX_DEBUG_ASSERT(hid->rbuf_size >= rpt_sz);
                memcpy(hid->rbuf, buf, len);
                hid->rbuf_filled = len;
                hid->rbuf_needed = rpt_sz - len;
                break;
            }
        }

        MX_DEBUG_ASSERT(rbuf != NULL);
        MX_DEBUG_ASSERT(consumed <= len);
        buf += consumed;
        len -= consumed;

        hid_instance_t* instance;
        foreach_instance(hid, instance) {
            mtx_lock(&instance->fifo.lock);
            bool was_empty = mx_hid_fifo_size(&instance->fifo) == 0;
            ssize_t wrote = mx_hid_fifo_write(&instance->fifo, rbuf, rlen);

            if (wrote <= 0) {
                if (!(instance->flags & HID_FLAGS_WRITE_FAILED)) {
                    printf("%s: could not write to hid fifo (ret=%zd)\n",
                            hid->name, wrote);
                    instance->flags |= HID_FLAGS_WRITE_FAILED;
                }
            } else {
                instance->flags &= ~HID_FLAGS_WRITE_FAILED;
                if (was_empty) {
                    device_state_set(instance->mxdev, DEV_STATE_READABLE);
                }
            }
            mtx_unlock(&instance->fifo.lock);
        }
    }

    mtx_unlock(&hid->instance_lock);
}

hidbus_ifc_t hid_ifc = {
    .io_queue = hid_io_queue,
};

static mx_status_t hid_bind(void* ctx, mx_device_t* parent, void** cookie) {
    hid_device_t* hiddev;
    if ((hiddev = calloc(1, sizeof(hid_device_t))) == NULL) {
        return ERR_NO_MEMORY;
    }

    mx_status_t status;
    if (device_op_get_protocol(parent, MX_PROTOCOL_HIDBUS, (void**)&hiddev->hid_ops)) {
        printf("hid: bind: no hidbus protocol\n");
        status = ERR_INTERNAL;
        goto fail;
    }

    hiddev->hid_mxdev = parent;
    if ((status = hid_op_query(hiddev, 0, &hiddev->info)) < 0) {
        printf("hid: bind: hidbus query failed: %d\n", status);
        goto fail;
    }

    mtx_init(&hiddev->instance_lock, mtx_plain);
    list_initialize(&hiddev->instance_list);

    snprintf(hiddev->name, sizeof(hiddev->name), "hid-device-%03d", hiddev->info.dev_num);
    hiddev->name[MX_DEVICE_NAME_MAX] = 0;

    if (hiddev->info.boot_device) {
        status = hid_op_set_protocol(hiddev, HID_PROTOCOL_BOOT);
        if (status != NO_ERROR) {
            printf("hid: could not put HID device into boot protocol: %d\n", status);
            goto fail;
        }

        // Disable numlock
        if (hiddev->info.dev_class == HID_DEV_CLASS_KBD) {
            uint8_t zero = 0;
            hid_op_set_report(hiddev, HID_REPORT_TYPE_OUTPUT, 0, &zero, sizeof(zero));
            // ignore failure for now
        }
    }

    status = hid_op_get_descriptor(hiddev, HID_DESC_TYPE_REPORT,
            (void**)&hiddev->hid_report_desc, &hiddev->hid_report_desc_len);
    if (status != NO_ERROR) {
        printf("hid: could not retrieve HID report descriptor: %d\n", status);
        goto fail;
    }

    status = hid_process_hid_report_desc(hiddev);
    if (status != NO_ERROR) {
        printf("hid: could not parse hid report descriptor: %d\n", status);
        goto fail;
    }
#if USB_HID_DEBUG
    hid_dump_hid_report_desc(hiddev);
#endif

    status = hid_init_reassembly_buffer(hiddev);
    if (status != NO_ERROR) {
        printf("hid: failed to initialize reassembly buffer: %d\n", status);
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = hiddev->name,
        .ctx = hiddev,
        .ops = &hid_device_proto,
        .proto_id = MX_PROTOCOL_INPUT,
    };

    status = device_add(hiddev->hid_mxdev, &args, &hiddev->mxdev);
    if (status != NO_ERROR) {
        printf("hid: device_add failed for HID device: %d\n", status);
        goto fail;
    }

    // TODO: delay calling start until we've been opened by someone
    status = hid_op_start(hiddev, &hid_ifc, hiddev);
    if (status != NO_ERROR) {
        printf("hid: could not start hid device: %d\n", status);
        device_remove(hiddev->mxdev);
        // Don't fail, since we've been added. Need to let devmgr clean us up.
        return status;
    }

    status = hid_op_set_idle(hiddev, 0, 0);
    if (status != NO_ERROR) {
        printf("hid: [W] set_idle failed for %s: %d\n", hiddev->name, status);
        // continue anyway
    }
    return NO_ERROR;

fail:
    hid_release_reassembly_buffer(hiddev);
    free(hiddev);
    return status;
}

static mx_driver_ops_t hid_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = hid_bind,
};

MAGENTA_DRIVER_BEGIN(hid, hid_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_HIDBUS),
MAGENTA_DRIVER_END(hid)
