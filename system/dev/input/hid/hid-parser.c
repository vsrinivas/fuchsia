// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hid-fifo.h"
#include "hid-parser.h"

#include <zircon/assert.h>
#include <zircon/listnode.h>

#include <assert.h>
#include <string.h>

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

zx_status_t hid_parse_reports(const uint8_t *buf, const size_t buf_len,
                        hid_reports_t *reports) {
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
