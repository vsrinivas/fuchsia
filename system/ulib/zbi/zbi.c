// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zbi/zbi.h>
#include <string.h>

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


zbi_result_t zbi_check(const void* base, zbi_header_t** err) {
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

zbi_result_t zbi_for_each(const void* base, const zbi_foreach_cb_t cb,
                          void* cookie) {
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

zbi_result_t zbi_append_section(void* base, const size_t capacity,
                                uint32_t section_length, uint32_t type,
                                uint32_t extra, uint32_t flags,
                                const void* payload) {

    uint8_t* new_section;
    zbi_result_t result = zbi_create_section(base, capacity, section_length,
                                             type, extra, flags,
                                             (void**)&new_section);

    if (result != ZBI_RESULT_OK) return result;

    // Copy in the payload.
    memcpy(new_section, payload, section_length);
    return ZBI_RESULT_OK;
}

zbi_result_t zbi_create_section(void* base, size_t capacity,
                                uint32_t section_length, uint32_t type,
                                uint32_t extra, uint32_t flags,
                                void** payload) {
    // We don't support CRC computation (yet?)
    if (flags & ZBI_FLAG_CRC32) return ZBI_RESULT_ERROR;

    zbi_header_t* hdr = (zbi_header_t*)base;

    // Make sure we were actually passed a bootdata container.
    if ((hdr->type != ZBI_TYPE_CONTAINER) ||
        (hdr->magic != ZBI_ITEM_MAGIC)    ||
        (hdr->extra != ZBI_CONTAINER_MAGIC)) {
        return ZBI_RESULT_BAD_TYPE;
    }

    // Make sure we have enough space in the buffer to append the new section.
    const size_t unpadded_length = sizeof(*hdr) + hdr->length;
    const size_t zbi_length = ZBI_ALIGN(unpadded_length);

    if (capacity < (zbi_length + section_length + sizeof(zbi_header_t))) {
        return ZBI_RESULT_TOO_BIG;
    }

    // Zero out the padding bytes.
    uint8_t* write_head = base + unpadded_length;
    const size_t padding_length = zbi_length - unpadded_length;
    memset(write_head, 0, padding_length);
    write_head += padding_length;

    // Copy over the new section.
    zbi_header_t* new_header = (zbi_header_t*)write_head;
    new_header->type = type;
    new_header->length = section_length;
    new_header->extra = extra;
    new_header->flags = flags | ZBI_FLAG_VERSION;
    new_header->reserved0 = 0;
    new_header->reserved1 = 0;
    new_header->magic = ZBI_ITEM_MAGIC;
    new_header->crc32 = ZBI_ITEM_NO_CRC32;

    write_head += sizeof(*new_header);

    // Set the result.
    *payload = write_head;

    // Patch up the container header.
    hdr->length = ZBI_ALIGN(hdr->length) + sizeof(zbi_header_t) + section_length;

    return ZBI_RESULT_OK;
}