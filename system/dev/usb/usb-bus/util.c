// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/usb.h>
#include <usb/usb-request.h>
#include <endian.h>
#include <lib/sync/completion.h>
#include <stdio.h>
#include <string.h>
#include <utf_conversion/utf_conversion.h>

#include "usb-bus.h"
#include "usb-device.h"
#include "util.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

typedef struct usb_langid_desc {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wLangIds[127];
} __PACKED usb_langid_desc_t;

static void usb_util_control_complete(usb_request_t* req, void* cookie) {
    sync_completion_signal((sync_completion_t*)cookie);
}

zx_status_t usb_util_control(usb_device_t* dev, uint8_t request_type, uint8_t request,
                             uint16_t value, uint16_t index, void* data, size_t length) {
    usb_request_t* req = NULL;
    bool use_free_list = length == 0;
    if (use_free_list) {
        req = usb_request_pool_get(&dev->free_reqs, length);
    }
    if (req == NULL) {
        zx_status_t status = usb_request_alloc(&req, length, 0, dev->req_size);
        if (status != ZX_OK) return status;
    }

    // fill in protocol data
    usb_setup_t* setup = &req->setup;
    setup->bmRequestType = request_type;
    setup->bRequest = request;
    setup->wValue = value;
    setup->wIndex = index;
    setup->wLength = length;
    req->header.device_id = dev->device_id;

    bool out = !!((request_type & USB_DIR_MASK) == USB_DIR_OUT);
    if (length > 0 && out) {
        usb_request_copy_to(req, data, length, 0);
    }

    sync_completion_t completion = SYNC_COMPLETION_INIT;

    req->header.length = length;
    req->complete_cb = usb_util_control_complete;
    req->cookie = &completion;

    usb_hci_request_queue(&dev->hci, req, usb_util_control_complete, &completion);
    zx_status_t status = sync_completion_wait(&completion, ZX_SEC(1));
    if (status == ZX_OK) {
        status = req->response.status;
    } else if (status == ZX_ERR_TIMED_OUT) {
        sync_completion_reset(&completion);
        status = usb_hci_cancel_all(&dev->hci, dev->device_id, 0);
        if (status == ZX_OK) {
            sync_completion_wait(&completion, ZX_TIME_INFINITE);
            status = ZX_ERR_TIMED_OUT;
        }
    }

    if (status == ZX_OK) {
        status = req->response.actual;

        if (length > 0 && !out) {
            usb_request_copy_from(req, data, req->response.actual, 0);
        }
    }
    if (use_free_list) {
        if (usb_request_pool_add(&dev->free_reqs, req) != ZX_OK) {
            zxlogf(TRACE, "Unable to add back request to the free pool\n");
            usb_request_release(req);
        }
    } else {
        usb_request_release(req);
    }
    return status;
}

zx_status_t usb_util_get_descriptor(usb_device_t* dev, uint16_t type, uint16_t index,
                                    uint16_t language, void* data, size_t length) {
    return usb_util_control(dev, USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                           USB_REQ_GET_DESCRIPTOR, type << 8 | index, language, data, length);
}

zx_status_t usb_util_get_string_descriptor(usb_device_t* dev, uint8_t desc_id, uint16_t lang_id,
                                           uint8_t* buf, size_t buflen, size_t* out_actual,
                                           uint16_t* out_actual_lang_id) {
    //  If we have never attempted to load our language ID table, do so now.
    zx_status_t result;
    if (!atomic_load_explicit(&dev->langids_fetched, memory_order_relaxed)) {
        usb_langid_desc_t* id_desc = calloc(1, sizeof(usb_langid_desc_t));

        if (id_desc != NULL) {
            result = usb_util_get_descriptor(dev, USB_DT_STRING, 0, 0, id_desc, sizeof(*id_desc));
            if (result == ZX_ERR_IO_REFUSED || result == ZX_ERR_IO_INVALID) {
                // some devices do not support fetching language list
                // in that case assume US English (0x0409)
                usb_hci_reset_endpoint(&dev->hci, dev->device_id, 0);
                id_desc->bLength = 4;
                id_desc->wLangIds[0] = htole16(0x0409);
                result = 4;
            } else if ((result >= 0) &&
                      ((result < 4) || (result != id_desc->bLength) || (result & 0x1))) {
                result = ZX_ERR_INTERNAL;
            }

            // So, if we have managed to fetch/synthesize a language ID table,
            // go ahead and perform a bit of fixup.  Redefine bLength to be the
            // valid number of entires in the table, and fixup the endianness of
            // all the entires in the table.  Then, attempt to swap in the new
            // language ID table.
            if (result >= 0) {
                id_desc->bLength = (id_desc->bLength - 2) >> 1;
#if BYTE_ORDER != LITTLE_ENDIAN
                for (uint8_t i = 0; i < id_desc->bLength; ++i) {
                    id_desc->wLangIds[i] = letoh16(id_desc->wLangIds[i]);
                }
#endif
                uintptr_t expected = 0;
                if (atomic_compare_exchange_strong(&dev->lang_ids, &expected, (uintptr_t)id_desc)) {
                    id_desc = NULL;
                }
            }

            // Make sure that we free any table that we allocated, but did not
            // end up swapping into place.
            free(id_desc);
        } else {
            result = ZX_ERR_NO_MEMORY;
        }

        atomic_store_explicit(&dev->langids_fetched, true, memory_order_relaxed);
        if (result < 0) {
            return result;
        }
    }

    // At this point in time, if we don't have a language id table, but we have
    // tried to obtain or synthesize one in the past, we are not going to get
    // one.  Just fail.
    usb_langid_desc_t* lang_ids =
        (usb_langid_desc_t*)(atomic_load_explicit(&dev->lang_ids, memory_order_relaxed));
    if (!lang_ids) {
        return ZX_ERR_BAD_STATE;
    }

    // Handle the special case that the user asked for the language ID table.
    if (desc_id == 0) {
        size_t table_sz = (lang_ids->bLength << 1);
        buflen &= ~1;
        
        size_t actual = MIN(table_sz, buflen);
        memcpy(buf, lang_ids->wLangIds, actual);
        *out_actual = actual;
        return ZX_OK;
    }

    // Search for the requested language ID.
    uint32_t lang_ndx;
    for (lang_ndx = 0; lang_ndx < lang_ids->bLength; ++ lang_ndx) {
        if (lang_id == lang_ids->wLangIds[lang_ndx]) {
            break;
        }
    }

    // If we didn't find it, default to the first entry in the table.
    if (lang_ndx >= lang_ids->bLength) {
        ZX_DEBUG_ASSERT(lang_ids->bLength >= 1);
        lang_id = lang_ids->wLangIds[0];
    }

    struct {
        uint8_t bLength;
        uint8_t bDescriptorType;
        uint16_t code_points[127];
    } string_desc;

    result = usb_util_get_descriptor(dev, USB_DT_STRING, desc_id, le16toh(lang_id),
                                     &string_desc, sizeof(string_desc));

    if (result == ZX_ERR_IO_REFUSED || result == ZX_ERR_IO_INVALID) {
        zx_status_t reset_result = usb_hci_reset_endpoint(&dev->hci, dev->device_id, 0);
        if (reset_result != ZX_OK) {
            zxlogf(ERROR, "failed to reset endpoint, err: %d\n", reset_result);
            return result;
        }
        result = usb_util_get_descriptor(dev, USB_DT_STRING, desc_id, le16toh(lang_id),
                                         &string_desc, sizeof(string_desc));
        if (result == ZX_ERR_IO_REFUSED || result == ZX_ERR_IO_INVALID) {
            reset_result = usb_hci_reset_endpoint(&dev->hci, dev->device_id, 0);
            if (reset_result != ZX_OK) {
                zxlogf(ERROR, "failed to reset endpoint, err: %d\n", reset_result);
                return result;
            }
        }
    }

    if (result < 0) {
        return result;
    }

    if ((result < 2) || (result != string_desc.bLength)) {
        result = ZX_ERR_INTERNAL;
    } else  {
        // Success! Convert this result from UTF16LE to UTF8 and store the
        // language ID we actually fetched (if it was not what the user
        // requested).
        *out_actual = buflen;
        *out_actual_lang_id = lang_id;
        utf16_to_utf8(string_desc.code_points, (string_desc.bLength >> 1) - 1,
                      buf, out_actual,
                      UTF_CONVERT_FLAG_FORCE_LITTLE_ENDIAN);
        return ZX_OK;
    }

    return result;
}
