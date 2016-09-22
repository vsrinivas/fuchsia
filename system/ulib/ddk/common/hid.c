// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/common/hid.h>
#include <ddk/common/hid-fifo.h>

#include <magenta/listnode.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HID_FLAGS_DEAD 1

#define USB_HID_DEBUG 0

#define to_hid_dev(d) containerof(d, mx_hid_device_t, dev)
#define to_hid_instance(d) containerof(d, mx_hid_instance_t, dev);
#define foreach_instance(root, instance) \
    list_for_every_entry(&root->instance_list, instance, mx_hid_instance_t, node)
#define bits_to_bytes(n) (((n) + 7) / 8)

// Until we do full HID parsing, we put mouse and keyboard devices into boot
// protocol mode. In particular, a mouse will always send 3 byte reports (see
// ddk/protocol/input.h for the format). This macro sets ioctl return values for
// boot mouse devices to reflect the boot protocol, rather than what the device
// itself reports.
// TODO: update this to include keyboards if we find a keyboard in the wild that
// needs a hack as well.
#define BOOT_MOUSE_HACK 1

typedef struct mx_hid_instance {
    mx_device_t dev;
    mx_hid_device_t* root;

    uint32_t flags;

    mx_hid_fifo_t fifo;

    struct list_node node;
} mx_hid_instance_t;

static input_report_size_t hid_get_report_size_by_id(mx_hid_device_t* hid,
        input_report_id_t id, input_report_type_t type) {
#if BOOT_MOUSE_HACK
    // Ignore the HID report descriptor from the device, since we're putting the
    // device into boot protocol mode.
    if (hid->dev_class == HID_DEV_CLASS_POINTER) return 3;
#endif
    for (size_t i = 0; i < hid->num_reports; i++) {
        if (hid->sizes[i].id < 0) break;
        if (hid->sizes[i].id == id) {
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

static mx_status_t hid_get_protocol(mx_hid_device_t* hid, void* out_buf, size_t out_len) {
    if (out_len < sizeof(int)) return ERR_INVALID_ARGS;

    int* reply = out_buf;
    *reply = INPUT_PROTO_NONE;
    if (hid->dev_class == HID_DEV_CLASS_KBD || hid->dev_class == HID_DEV_CLASS_KBD_POINTER) {
        *reply = INPUT_PROTO_KBD;
    } else if (hid->dev_class == HID_DEV_CLASS_POINTER) {
        *reply = INPUT_PROTO_MOUSE;
    }
    return sizeof(*reply);
}

static mx_status_t hid_get_hid_desc_size(mx_hid_device_t* hid, void* out_buf, size_t out_len) {
    if (out_len < sizeof(size_t)) return ERR_INVALID_ARGS;

    size_t* reply = out_buf;
    *reply = hid->hid_report_desc_len;
    return sizeof(*reply);
}

static mx_status_t hid_get_hid_desc(mx_hid_device_t* hid, void* out_buf, size_t out_len) {
    if (out_len < hid->hid_report_desc_len) return ERR_INVALID_ARGS;

    memcpy(out_buf, hid->hid_report_desc, hid->hid_report_desc_len);
    return hid->hid_report_desc_len;
}

static mx_status_t hid_get_num_reports(mx_hid_device_t* hid, void* out_buf, size_t out_len) {
    if (out_len < sizeof(size_t)) return ERR_INVALID_ARGS;

    size_t* reply = out_buf;
    *reply = hid->num_reports;
#if BOOT_MOUSE_HACK
    if (hid->dev_class == HID_DEV_CLASS_POINTER) *reply = 1;
#endif
    return sizeof(*reply);
}

static mx_status_t hid_get_report_ids(mx_hid_device_t* hid, void* out_buf, size_t out_len) {
#if BOOT_MOUSE_HACK
    if (hid->dev_class == HID_DEV_CLASS_POINTER) {
        if (out_len < sizeof(input_report_id_t)) {
            return ERR_INVALID_ARGS;
        }
    } else {
        if (out_len < hid->num_reports * sizeof(input_report_id_t))
        return ERR_INVALID_ARGS;
    }
#else
    if (out_len < hid->num_reports * sizeof(input_report_id_t))
        return ERR_INVALID_ARGS;
#endif

    input_report_id_t* reply = out_buf;
#if BOOT_MOUSE_HACK
    if (hid->dev_class == HID_DEV_CLASS_POINTER) {
        *reply = 0;
        return sizeof(input_report_id_t);
    }
#endif
    for (size_t i = 0; i < hid->num_reports; i++) {
        assert(hid->sizes[i].id >= 0);
        *reply++ = (input_report_id_t)hid->sizes[i].id;
    }
    return hid->num_reports * sizeof(input_report_id_t);
}

static mx_status_t hid_get_report_size(mx_hid_device_t* hid, const void* in_buf, size_t in_len,
                                           void* out_buf, size_t out_len) {
    if (in_len < sizeof(input_get_report_size_t)) return ERR_INVALID_ARGS;
    if (out_len < sizeof(input_report_size_t)) return ERR_INVALID_ARGS;

    const input_get_report_size_t* inp = in_buf;

    input_report_size_t* reply = out_buf;
    *reply = hid_get_report_size_by_id(hid, inp->id, inp->type);
    if (*reply == 0)
        return ERR_INVALID_ARGS;
    return sizeof(*reply);
}

static mx_status_t hid_get_max_reportsize(mx_hid_device_t* hid, void* out_buf, size_t out_len) {
    if (out_len < sizeof(input_report_size_t)) return ERR_INVALID_ARGS;

    input_report_size_t* reply = out_buf;
    *reply = 0;
    for (int i = 0; i < HID_MAX_REPORT_IDS; i++) {
        if (hid->sizes[i].id >= 0 &&
            hid->sizes[i].in_size > *reply)
            *reply = hid->sizes[i].in_size;
    }

    *reply = bits_to_bytes(*reply);
#if BOOT_MOUSE_HACK
    if (hid->dev_class == HID_DEV_CLASS_POINTER) *reply = 3;
#endif
    return sizeof(*reply);
}

static mx_status_t hid_get_report(mx_hid_device_t* hid, const void* in_buf, size_t in_len,
                                      void* out_buf, size_t out_len) {
    if (in_len < sizeof(input_get_report_t)) return ERR_INVALID_ARGS;
    const input_get_report_t* inp = in_buf;

    input_report_size_t needed = hid_get_report_size_by_id(hid, inp->id, inp->type);
    if (needed == 0) return ERR_INVALID_ARGS;
    if (out_len < (size_t)needed) return ERR_BUFFER_TOO_SMALL;

    return hid->ops->get_report(hid, inp->type, inp->id, out_buf, out_len);
}

static mx_status_t hid_set_report(mx_hid_device_t* hid, const void* in_buf, size_t in_len) {

    if (in_len < sizeof(input_set_report_t)) return ERR_INVALID_ARGS;
    const input_set_report_t* inp = in_buf;

    input_report_size_t needed = hid_get_report_size_by_id(hid, inp->id, inp->type);
    if (needed == 0) return ERR_INVALID_ARGS;
    if (in_len - sizeof(input_set_report_t) < (size_t)needed) return ERR_INVALID_ARGS;

    return hid->ops->set_report(hid, inp->type, inp->id, (void*)inp->data,
            in_len - sizeof(input_set_report_t));
}


static mx_status_t hid_create_instance(mx_hid_instance_t** dev) {
    *dev = calloc(1, sizeof(mx_hid_instance_t));
    if (*dev == NULL) {
        return ERR_NO_MEMORY;
    }
    mx_hid_fifo_init(&(*dev)->fifo);
    return NO_ERROR;
}

static void hid_cleanup_instance(mx_hid_instance_t* dev) {
    if (!(dev->flags & HID_FLAGS_DEAD)) {
        mtx_lock(&dev->root->instance_lock);
        list_delete(&dev->node);
        mtx_unlock(&dev->root->instance_lock);
    }
    free(dev);
}

static ssize_t hid_read_instance(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
    mx_hid_instance_t* hid = to_hid_instance(dev);

    if (hid->flags & HID_FLAGS_DEAD) {
        return ERR_REMOTE_CLOSED;
    }

    size_t left;
    mtx_lock(&hid->fifo.lock);
    size_t xfer;
    uint8_t rpt_id = 0;
    if (hid->root->num_reports > 1) {
        ssize_t r = mx_hid_fifo_peek(&hid->fifo, &rpt_id);
        if (r < 1) {
            printf("error reading hid device: fifo empty!\n");
            mtx_unlock(&hid->fifo.lock);
            return ERR_BAD_STATE;
        }
    }
    xfer = hid_get_report_size_by_id(hid->root, rpt_id, INPUT_REPORT_INPUT);
    if (xfer == 0) {
        printf("error reading hid device: unknown report id (%u)!\n", rpt_id);
        mtx_unlock(&hid->fifo.lock);
        return ERR_BAD_STATE;
    }
#if BOOT_MOUSE_HACK
    if (hid->root->dev_class != HID_DEV_CLASS_POINTER) {
#endif
    if (hid->root->num_reports > 1) {
        // account for the report id
        xfer++;
    }
#if BOOT_MOUSE_HACK
    }
#endif
    if (xfer > count) {
        printf("next report: %zd, read count: %zd\n", xfer, count);
        mtx_unlock(&hid->fifo.lock);
        return ERR_BUFFER_TOO_SMALL;
    }
    ssize_t r = mx_hid_fifo_read(&hid->fifo, buf, xfer);
    left = mx_hid_fifo_size(&hid->fifo);
    if (left == 0) {
        device_state_clr(&hid->dev, DEV_STATE_READABLE);
    }
    mtx_unlock(&hid->fifo.lock);
    return r;
}

static ssize_t hid_ioctl_instance(mx_device_t* dev, uint32_t op,
        const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    mx_hid_instance_t* hid = to_hid_instance(dev);
    if (hid->flags & HID_FLAGS_DEAD) return ERR_REMOTE_CLOSED;

    switch (op) {
    case IOCTL_INPUT_GET_PROTOCOL:
        return hid_get_protocol(hid->root, out_buf, out_len);
    case IOCTL_INPUT_GET_REPORT_DESC_SIZE:
        return hid_get_hid_desc_size(hid->root, out_buf, out_len);
    case IOCTL_INPUT_GET_REPORT_DESC:
        return hid_get_hid_desc(hid->root, out_buf, out_len);
    case IOCTL_INPUT_GET_NUM_REPORTS:
        return hid_get_num_reports(hid->root, out_buf, out_len);
    case IOCTL_INPUT_GET_REPORT_IDS:
        return hid_get_report_ids(hid->root, out_buf, out_len);
    case IOCTL_INPUT_GET_REPORT_SIZE:
        return hid_get_report_size(hid->root, in_buf, in_len, out_buf, out_len);
    case IOCTL_INPUT_GET_MAX_REPORTSIZE:
        return hid_get_max_reportsize(hid->root, out_buf, out_len);
    case IOCTL_INPUT_GET_REPORT:
        return hid_get_report(hid->root, in_buf, in_len, out_buf, out_len);
    case IOCTL_INPUT_SET_REPORT:
        return hid_set_report(hid->root, in_buf, in_len);
    }
    return ERR_NOT_SUPPORTED;
}

static mx_status_t hid_release_instance(mx_device_t* dev) {
    mx_hid_instance_t* hid = to_hid_instance(dev);
    hid_cleanup_instance(hid);
    return NO_ERROR;
}

mx_protocol_device_t hid_instance_proto = {
    .read = hid_read_instance,
    .ioctl = hid_ioctl_instance,
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

static void hid_dump_hid_report_desc(mx_hid_device_t* dev) {
    printf("hid: dev %p HID report descriptor\n", dev);
    for (size_t c = 0; c < dev->hid_report_desc_len; c++) {
        printf("%02x ", dev->hid_report_desc[c]);
        if (c % 16 == 15) printf("\n");
    }
    printf("\n");
    printf("hid: num reports: %zd\n", dev->num_reports);
    for (size_t i = 0; i < dev->num_reports; i++) {
        if (dev->sizes[i].id >= 0) {
            printf("  report id: %u  sizes: in %u out %u feat %u\n",
                    dev->sizes[i].id, dev->sizes[i].in_size, dev->sizes[i].out_size,
                    dev->sizes[i].feat_size);
        }
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

static int hid_find_report_id(input_report_id_t report_id, mx_hid_device_t* dev) {
    for (int i = 0; i < HID_MAX_REPORT_IDS; i++) {
        if (dev->sizes[i].id == report_id) return i;
        if (dev->sizes[i].id == -1) {
            dev->sizes[i].id = report_id;
            dev->num_reports++;
            return i;
        }
    }
    return -1;
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

static mx_status_t hid_process_hid_report_desc(mx_hid_device_t* dev) {
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
                idx = hid_find_report_id(state.rpt_id, dev);
                if (idx < 0) {
                    status = ERR_NOT_SUPPORTED;
                    goto done;
                }
                dev->sizes[idx].in_size += inc;
                break;
            case HID_ITEM_MAIN_TAG_OUTPUT:
                idx = hid_find_report_id(state.rpt_id, dev);
                if (idx < 0) {
                    status = ERR_NOT_SUPPORTED;
                    goto done;
                }
                dev->sizes[idx].out_size += inc;
                break;
            case HID_ITEM_MAIN_TAG_FEATURE:
                idx = hid_find_report_id(state.rpt_id, dev);
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
    return status;
}

static inline void hid_init_report_sizes(mx_hid_device_t* dev) {
    for (int i = 0; i < HID_MAX_REPORT_IDS; i++) {
        dev->sizes[i].id = -1;
    }
}

void hid_init_device(mx_hid_device_t* dev, hid_bus_ops_t* bus,
        uint8_t dev_num, bool boot_device, uint8_t dev_class) {
    dev->ops = bus;
    dev->dev_num = dev_num;
    dev->boot_device = boot_device;
    dev->dev_class = dev_class;
    hid_init_report_sizes(dev);
    mtx_init(&dev->instance_lock, mtx_plain);
    list_initialize(&dev->instance_list);
}

void hid_release_device(mx_hid_device_t* dev) {
    if (dev->hid_report_desc) {
        free(dev->hid_report_desc);
        dev->hid_report_desc = NULL;
        dev->hid_report_desc_len = 0;
    }
}

static mx_status_t hid_open_device(mx_device_t* dev, mx_device_t** dev_out, uint32_t flags) {
    mx_hid_device_t* hid = to_hid_dev(dev);

    mx_hid_instance_t* inst = NULL;
    mx_status_t status = hid_create_instance(&inst);
    if (inst == NULL) {
        return ERR_NO_MEMORY;
    }

    device_init(&inst->dev, hid->drv, "hid", &hid_instance_proto);
    inst->dev.protocol_id = MX_PROTOCOL_INPUT;
    status = device_add_instance(&inst->dev, dev);
    if (status != NO_ERROR) {
        hid_cleanup_instance(inst);
        return status;
    }
    inst->root = hid;

    mtx_lock(&hid->instance_lock);
    list_add_tail(&hid->instance_list, &inst->node);
    mtx_unlock(&hid->instance_lock);

    *dev_out = &inst->dev;
    return NO_ERROR;
}

static void hid_unbind_device(mx_device_t* dev) {
    mx_hid_device_t* hid = to_hid_dev(dev);
    mtx_lock(&hid->instance_lock);
    mx_hid_instance_t* instance;
    foreach_instance(hid, instance) {
        instance->flags |= HID_FLAGS_DEAD;
        device_state_set(&instance->dev, DEV_STATE_READABLE);
    }
    mtx_unlock(&hid->instance_lock);
    device_remove(&hid->dev);
}

static mx_status_t hid_proto_release_device(mx_device_t* dev) {
    mx_hid_device_t* hid = to_hid_dev(dev);
    hid_release_device(hid);
    return NO_ERROR;
}

mx_protocol_device_t hid_device_proto = {
    .open = hid_open_device,
    .unbind = hid_unbind_device,
    .release = hid_proto_release_device,
};

void hid_io_queue(mx_hid_device_t* hid, const uint8_t* buf, size_t len) {
    mtx_lock(&hid->instance_lock);
    mx_hid_instance_t* instance;
    foreach_instance(hid, instance) {
        mtx_lock(&instance->fifo.lock);
        bool was_empty = mx_hid_fifo_size(&instance->fifo) == 0;
        ssize_t wrote = mx_hid_fifo_write(&instance->fifo, buf, len);
        if (wrote <= 0) {
            printf("could not write to hid fifo (ret=%zd)\n", wrote);
        } else {
            if (was_empty) {
                device_state_set(&instance->dev, DEV_STATE_READABLE);
            }
        }
        mtx_unlock(&instance->fifo.lock);
    }
    mtx_unlock(&hid->instance_lock);
}

mx_status_t hid_add_device(mx_driver_t* drv, mx_hid_device_t* dev, mx_device_t* parent) {
    mx_status_t status;
    if (dev->boot_device) {
        status = dev->ops->set_protocol(dev, HID_PROTOCOL_BOOT);
        if (status != NO_ERROR) {
            printf("Could not put HID device into boot protocol: %d\n", status);
            return ERR_NOT_SUPPORTED;
        }

        // Disable numlock
        if (dev->dev_class == HID_DEV_CLASS_KBD) {
            uint8_t zero = 0;
            dev->ops->set_report(dev, HID_REPORT_TYPE_OUTPUT, 0, &zero, sizeof(zero));
            // ignore failure for now
        }
    }

    status = dev->ops->get_descriptor(dev, HID_DESC_TYPE_REPORT, (void**)&dev->hid_report_desc,
            &dev->hid_report_desc_len);
    if (status != NO_ERROR) {
        printf("Could not retrieve HID report descriptor: %d\n", status);
        return status;
    }

    status = hid_process_hid_report_desc(dev);
    if (status != NO_ERROR) {
        printf("Could not parse hid report descriptor: %d\n", status);
        return status;
    }
#if USB_HID_DEBUG
    hid_dump_hid_report_desc(dev);
#endif

    char name[15];
    snprintf(name, sizeof(name), "hid-device-%03d", dev->dev_num);
    device_init(&dev->dev, drv, name, &hid_device_proto);
    dev->dev.protocol_id = MX_PROTOCOL_INPUT;
    status = device_add(&dev->dev, parent);
    if (status != NO_ERROR) {
        printf("device_add failed for HID device: %d\n", status);
        return status;
    }

    status = dev->ops->set_idle(dev, 0, 0);
    if (status != NO_ERROR) {
        printf("W: set_idle failed for %s: %d\n", name, status);
        // continue anyway
    }

    return NO_ERROR;
}
