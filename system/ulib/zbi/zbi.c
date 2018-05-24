// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zbi/zbi.h>

static zbi_result_t for_each_check_entry(zbi_header_t* hdr, void* payload,
                                     void* cookie) {
    zbi_result_t result = ZBI_RESULT_OK;

    if (hdr->magic != ZBI_ITEM_MAGIC) { result = ZBI_RESULT_BAD_MAGIC; }

    // TODO(gkalsi): This is disabled for now because it seems like a bunch of
    // our bootitems don't conform to this for some reason.
    // if ((hdr->flags & ZBI_FLAG_VERSION) == 0) { result = ZBI_RESULT_BAD_VERSION; }

    // If we found a problem, try to report it back up to the caller.
    if (cookie && result != ZBI_RESULT_OK) {
        zbi_header_t** problemHeader = cookie;
        *problemHeader = hdr;
    }

    return result;
}


zbi_result_t zbi_check(void* base, zbi_header_t** err) {
    zbi_result_t res = ZBI_RESULT_OK;
    zbi_header_t* header = (zbi_header_t*)(base);

    if (header->type != ZBI_TYPE_CONTAINER) { res = ZBI_RESULT_BAD_TYPE; }
    if (header->extra != ZBI_CONTAINER_MAGIC) { res = ZBI_RESULT_BAD_MAGIC; }
    if ((header->flags & ZBI_FLAG_VERSION) == 0) { res = ZBI_RESULT_BAD_VERSION; }
    if ((header->flags & ZBI_FLAG_CRC32) == 0) {
        if (header->crc32 != ZBI_ITEM_NO_CRC32) { res = ZBI_RESULT_BAD_CRC; }
    }

    // Something was wrong with the container, don't even attempt to process
    // the rest of the image and return diagnostic information back to the
    // caller if they requested it.
    if (res != ZBI_RESULT_OK) {
        if (err) { *err = header; }
        return res;
    }

    res = zbi_for_each(base, for_each_check_entry, err);

    if (err && res == ZBI_RESULT_ERR_TRUNCATED) {
        // A truncated image perhaps indicates a problem with the container?
        *err = header;
    }

    return res;
}

zbi_result_t zbi_for_each(void* base, const zbi_foreach_cb_t cb, void* cookie) {
    zbi_header_t* header = (zbi_header_t*)(base);

    // Skip container header.
    const uint32_t totalSize = (uint32_t)(sizeof(zbi_header_t) + header->length);
    uint32_t offset = sizeof(zbi_header_t);
    while (offset < totalSize) {
        zbi_header_t* entryHeader =
            (zbi_header_t*)(base + offset);

        zbi_result_t result = cb(entryHeader, entryHeader + 1, cookie);

        if (result != ZBI_RESULT_OK) return result;

        if ((offset + entryHeader->length + sizeof(zbi_header_t)) > totalSize)
            return ZBI_RESULT_ERR_TRUNCATED;

        offset = ZBI_ALIGN(offset + entryHeader->length + sizeof(zbi_header_t));
    }

    return ZBI_RESULT_OK;
}