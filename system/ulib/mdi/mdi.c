// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <mdi/mdi.h>
#include <magenta/boot/bootdata.h>

#define DEBUG   0

#if DEBUG
#ifdef _KERNEL
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) fprintf(stderr, fmt)
#endif
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

// takes pointer to MDI header and returns reference to MDI root node
mx_status_t mdi_init(const void* mdi_data, size_t length, mdi_node_ref_t* out_ref) {
    const bootdata_t* header = (const bootdata_t *)mdi_data;

    if (length < sizeof(bootdata_t)) {
        xprintf("%s: bad bootdata length\n", __FUNCTION__);
        return MX_ERR_INVALID_ARGS;
    }
    if (header->type != BOOTDATA_MDI) {
        xprintf("%s: not a MDI bootdata header\n", __FUNCTION__);
        return MX_ERR_INVALID_ARGS;
    }
    mdi_data += sizeof(bootdata_t);
    length -= sizeof(bootdata_t);

    // Adjust for extended header if present
    if (header->flags & BOOTDATA_FLAG_EXTRA) {
        if (length < sizeof(bootextra_t)) {
            xprintf("%s: bad bootextra length\n", __FUNCTION__);
            return MX_ERR_INVALID_ARGS;
        }
        mdi_data += sizeof(bootextra_t);
        length -= sizeof(bootextra_t);
    }

    // Sanity check the length. Must be big enough to contain at least one node.
    if (length < header->length || header->length < sizeof(mdi_node_t)) {
        xprintf("%s: bad length\n", __FUNCTION__);
        return MX_ERR_INVALID_ARGS;
    }

    const mdi_node_t* node = (const mdi_node_t *)(header + 1);
    if (node->length != header->length) {
        xprintf("%s: bad root node length\n", __FUNCTION__);
        out_ref->node = NULL;
        return MX_ERR_INVALID_ARGS;
    }

    out_ref->node = node;
    out_ref->siblings_count = 0;
    out_ref->siblings_end = NULL;
    return MX_OK;
}

mx_status_t mdi_node_uint8(const mdi_node_ref_t* ref, uint8_t* out_value) {
    if (mdi_node_type(ref) != MDI_UINT8) {
        xprintf("%s: bad node type for mdi_node_uint8\n", __FUNCTION__);
        return MX_ERR_WRONG_TYPE;
    }
    *out_value = ref->node->value.u8;
    return MX_OK;
}

mx_status_t mdi_node_int32(const mdi_node_ref_t* ref, int32_t* out_value) {
    if (mdi_node_type(ref) != MDI_INT32) {
        xprintf("%s: bad node type for mdi_node_int32\n", __FUNCTION__);
        return MX_ERR_WRONG_TYPE;
    }
    *out_value = ref->node->value.i32;
    return MX_OK;
}

mx_status_t mdi_node_uint32(const mdi_node_ref_t* ref, uint32_t* out_value) {
    if (mdi_node_type(ref) != MDI_UINT32) {
        xprintf("%s: bad node type for mdi_node_uint32\n", __FUNCTION__);
        return MX_ERR_WRONG_TYPE;
    }
    *out_value = ref->node->value.u32;
    return MX_OK;
}

mx_status_t mdi_node_uint64(const mdi_node_ref_t* ref, uint64_t* out_value) {
    if (mdi_node_type(ref) != MDI_UINT64) {
        xprintf("%s: bad node type for mdi_node_uint64\n", __FUNCTION__);
        return MX_ERR_WRONG_TYPE;
    }
    *out_value = ref->node->value.u64;
    return MX_OK;
}

mx_status_t mdi_node_boolean(const mdi_node_ref_t* ref, bool* out_value) {
    if (mdi_node_type(ref) != MDI_BOOLEAN) {
        xprintf("%s: bad node type for mdi_node_boolean\n", __FUNCTION__);
        return MX_ERR_WRONG_TYPE;
    }
    *out_value = !!ref->node->value.u8;
    return MX_OK;
}

const char* mdi_node_string(const mdi_node_ref_t* ref) {
    if (mdi_node_type(ref) != MDI_STRING) {
        xprintf("%s: bad node type for mdi_string_value\n", __FUNCTION__);
        return NULL;
    }
    return (const char *)(ref->node + 1);
}

const void* mdi_array_values(const mdi_node_ref_t* ref) {
    if (mdi_node_type(ref) == MDI_ARRAY) {
        return ref->node + 1;
    } else {
        xprintf("%s: bad node type for mdi_array_values\n", __FUNCTION__);
        return NULL;
    }
}

uint32_t mdi_array_length(const mdi_node_ref_t* ref) {
    if (mdi_node_type(ref) == MDI_ARRAY) {
        return ref->node->value.child_count;
    } else {
        xprintf("%s: bad node type for mdi_array_length\n", __FUNCTION__);
        return -1;
    }
}

mx_status_t mdi_array_uint8(const mdi_node_ref_t* ref, uint8_t index, uint8_t* out_value) {
    const mdi_node_t* node = ref->node;
    if ((node->id & (MDI_TYPE_MASK | MDI_ARRAY_TYPE_MASK)) != MDI_MAKE_ARRAY_ID(MDI_UINT8, 0)) {
        xprintf("%s: ref not an uint8 array\n", __FUNCTION__);
        return MX_ERR_WRONG_TYPE;
    }
    if (index >= node->value.child_count) {
        xprintf("%s: array index out of range\n", __FUNCTION__);
        return MX_ERR_OUT_OF_RANGE;
    }
    const void* array_data = node + 1;
    *out_value = ((uint8_t *)array_data)[index];
    return MX_OK;
}

mx_status_t mdi_array_int32(const mdi_node_ref_t* ref, uint32_t index, int32_t* out_value) {
    const mdi_node_t* node = ref->node;
    if ((node->id & (MDI_TYPE_MASK | MDI_ARRAY_TYPE_MASK)) != MDI_MAKE_ARRAY_ID(MDI_INT32, 0)) {
        xprintf("%s: ref not an int32 array\n", __FUNCTION__);
        return MX_ERR_WRONG_TYPE;
    }
    if (index >= node->value.child_count) {
        xprintf("%s: array index out of range\n", __FUNCTION__);
        return MX_ERR_OUT_OF_RANGE;
    }
    const void* array_data = node + 1;
    *out_value = ((int32_t *)array_data)[index];
    return MX_OK;
}

mx_status_t mdi_array_uint32(const mdi_node_ref_t* ref, uint32_t index, uint32_t* out_value) {
    const mdi_node_t* node = ref->node;
    if ((node->id & (MDI_TYPE_MASK | MDI_ARRAY_TYPE_MASK)) != MDI_MAKE_ARRAY_ID(MDI_UINT32, 0)) {
        xprintf("%s: ref not an uint32 array\n", __FUNCTION__);
        return MX_ERR_WRONG_TYPE;
    }
    if (index >= node->value.child_count) {
        xprintf("%s: array index out of range\n", __FUNCTION__);
        return MX_ERR_OUT_OF_RANGE;
    }
    const void* array_data = node + 1;
    *out_value = ((uint32_t *)array_data)[index];
    return MX_OK;
}

mx_status_t mdi_array_uint64(const mdi_node_ref_t* ref, uint32_t index, uint64_t* out_value) {
    const mdi_node_t* node = ref->node;
    if ((node->id & (MDI_TYPE_MASK | MDI_ARRAY_TYPE_MASK)) != MDI_MAKE_ARRAY_ID(MDI_UINT64, 0)) {
        xprintf("%s: ref not an uint64 array\n", __FUNCTION__);
        return MX_ERR_WRONG_TYPE;
    }
    if (index >= node->value.child_count) {
        xprintf("%s: array index out of range\n", __FUNCTION__);
        return MX_ERR_OUT_OF_RANGE;
    }
    const void* array_data = node + 1;
    *out_value = ((uint64_t *)array_data)[index];
    return MX_OK;
}

mx_status_t mdi_array_boolean(const mdi_node_ref_t* ref, uint32_t index, bool* out_value) {
    const mdi_node_t* node = ref->node;
    if ((node->id & (MDI_TYPE_MASK | MDI_ARRAY_TYPE_MASK)) != MDI_MAKE_ARRAY_ID(MDI_BOOLEAN, 0)) {
        xprintf("%s: ref not an boolean array\n", __FUNCTION__);
        return MX_ERR_WRONG_TYPE;
    }
    if (index >= node->value.child_count) {
        xprintf("%s: array index out of range\n", __FUNCTION__);
        return MX_ERR_OUT_OF_RANGE;
    }
    const void* array_data = node + 1;
    *out_value = ((bool *)array_data)[index];
    return MX_OK;
}

mx_status_t mdi_first_child(const mdi_node_ref_t* ref, mdi_node_ref_t* out_ref) {
    out_ref->node = NULL;
    out_ref->siblings_count = 0;
    out_ref->siblings_end = NULL;

   if (mdi_node_type(ref) != MDI_LIST) {
        xprintf("%s: ref not a list\n", __FUNCTION__);
        return MX_ERR_WRONG_TYPE;
    }

    const mdi_node_t* node = ref->node;
    if (node->value.child_count == 0) {
        return MX_ERR_NOT_FOUND;
    }

    // first child immediately follows list node
    const mdi_node_t* child = &node[1];
    void* siblings_end = (void *)node + node->length;
    if ((void *)child + child->length > siblings_end) {
        xprintf("%s: child length out of range\n", __FUNCTION__);
        return MX_ERR_INVALID_ARGS;
    }

    out_ref->node = child;
    out_ref->siblings_count = node->value.child_count - 1;
    out_ref->siblings_end = siblings_end;
    return MX_OK;
}

mx_status_t mdi_next_child(const mdi_node_ref_t* ref, mdi_node_ref_t* out_ref) {
    if (ref->siblings_count == 0) {
        out_ref->node = NULL;
        out_ref->siblings_count = 0;
        out_ref->siblings_end = NULL;
        return MX_ERR_NOT_FOUND;
    }

    const mdi_node_t* node = ref->node;
    const mdi_node_t* next = (const mdi_node_t *)((void *)node + node->length);
    if ((void *)next + next->length > ref->siblings_end) {
        xprintf("%s: child length out of range\n", __FUNCTION__);
        return MX_ERR_INVALID_ARGS;
    }

    out_ref->node = next;
    out_ref->siblings_count = ref->siblings_count - 1;
    out_ref->siblings_end = ref->siblings_end;
    return MX_OK;
}

uint32_t mdi_child_count(const mdi_node_ref_t* ref) {
    if (mdi_node_type(ref) == MDI_LIST) {
        return ref->node->value.child_count;
    } else {
        return 0;
    }
}

mx_status_t mdi_find_node(const mdi_node_ref_t* ref, mdi_id_t id, mdi_node_ref_t* out_ref) {
    out_ref->siblings_count = 0;
    out_ref->siblings_end = NULL;
    mx_status_t status = mdi_first_child(ref, out_ref);

    while (status == MX_OK && out_ref->node->id != id) {
        status = mdi_next_child(out_ref, out_ref);
    }
    if (status != MX_OK) {
        out_ref->node = NULL;
    }
    return status;
}
