// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>

#include <mdi/mdi.h>

#define VERSION_MAJOR   1

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
    const mdi_header_t* header = (const mdi_header_t *)mdi_data;
    // length should be at least size of header plus one node, and length should match header length
    if (length < sizeof(mdi_node_t) + sizeof(mdi_node_t) || length < header->length) {
        xprintf("%s: bad length\n", __FUNCTION__);
        return ERR_INVALID_ARGS;
    }
    if (header->magic != MDI_MAGIC) {
        xprintf("%s: bad magic 0x%08X\n", __FUNCTION__, header->magic);
        return ERR_INVALID_ARGS;
    }
    if (header->version_major != VERSION_MAJOR) {
        xprintf("%s: unsupported version %d.%d\n", __FUNCTION__, header->version_major,
                header->version_minor);
        return ERR_INVALID_ARGS;
    }

    const mdi_node_t* node = (const mdi_node_t *)((const char *)(header + 1));
    if (node->length != header->length - sizeof(*header)) {
        xprintf("%s: bad root node length\n", __FUNCTION__);
        out_ref->node = NULL;
        return ERR_INVALID_ARGS;
    }

    out_ref->node = node;
    out_ref->siblings_count = 0;
    out_ref->siblings_end = NULL;
    return NO_ERROR;
}

mx_status_t mdi_node_uint8(const mdi_node_ref_t* ref, uint8_t* out_value) {
    if (mdi_node_type(ref) != MDI_UINT8) {
        xprintf("%s: bad node type for mdi_node_uint8\n", __FUNCTION__);
        return ERR_WRONG_TYPE;
    }
    *out_value = ref->node->value.u8;
    return NO_ERROR;
}

mx_status_t mdi_node_int32(const mdi_node_ref_t* ref, int32_t* out_value) {
    if (mdi_node_type(ref) != MDI_INT32) {
        xprintf("%s: bad node type for mdi_node_int32\n", __FUNCTION__);
        return ERR_WRONG_TYPE;
    }
    *out_value = ref->node->value.i32;
    return NO_ERROR;
}

mx_status_t mdi_node_uint32(const mdi_node_ref_t* ref, uint32_t* out_value) {
    if (mdi_node_type(ref) != MDI_UINT32) {
        xprintf("%s: bad node type for mdi_node_uint32\n", __FUNCTION__);
        return ERR_WRONG_TYPE;
    }
    *out_value = ref->node->value.u32;
    return NO_ERROR;
}

mx_status_t mdi_node_uint64(const mdi_node_ref_t* ref, uint64_t* out_value) {
    if (mdi_node_type(ref) != MDI_UINT64) {
        xprintf("%s: bad node type for mdi_node_uint64\n", __FUNCTION__);
        return ERR_WRONG_TYPE;
    }
    *out_value = ref->node->value.u64;
    return NO_ERROR;
}

mx_status_t mdi_node_boolean(const mdi_node_ref_t* ref, bool* out_value) {
    if (mdi_node_type(ref) != MDI_BOOLEAN) {
        xprintf("%s: bad node type for mdi_node_boolean\n", __FUNCTION__);
        return ERR_WRONG_TYPE;
    }
    *out_value = !!ref->node->value.u8;
    return NO_ERROR;
}

const char* mdi_node_string(const mdi_node_ref_t* ref) {
    if (mdi_node_type(ref) != MDI_STRING) {
        xprintf("%s: bad node type for mdi_string_value\n", __FUNCTION__);
        return NULL;
    }
    return (const char *)(ref->node + 1);
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
        return ERR_WRONG_TYPE;
    }
    if (index >= node->value.child_count) {
        xprintf("%s: array index out of range\n", __FUNCTION__);
        return ERR_OUT_OF_RANGE;
    }
    const void* array_data = node + 1;
    *out_value = ((uint8_t *)array_data)[index];
    return NO_ERROR;
}

mx_status_t mdi_array_int32(const mdi_node_ref_t* ref, uint32_t index, int32_t* out_value) {
    const mdi_node_t* node = ref->node;
    if ((node->id & (MDI_TYPE_MASK | MDI_ARRAY_TYPE_MASK)) != MDI_MAKE_ARRAY_ID(MDI_INT32, 0)) {
        xprintf("%s: ref not an int32 array\n", __FUNCTION__);
        return ERR_WRONG_TYPE;
    }
    if (index >= node->value.child_count) {
        xprintf("%s: array index out of range\n", __FUNCTION__);
        return ERR_OUT_OF_RANGE;
    }
    const void* array_data = node + 1;
    *out_value = ((int32_t *)array_data)[index];
    return NO_ERROR;
}

mx_status_t mdi_array_uint32(const mdi_node_ref_t* ref, uint32_t index, uint32_t* out_value) {
    const mdi_node_t* node = ref->node;
    if ((node->id & (MDI_TYPE_MASK | MDI_ARRAY_TYPE_MASK)) != MDI_MAKE_ARRAY_ID(MDI_UINT32, 0)) {
        xprintf("%s: ref not an uint32 array\n", __FUNCTION__);
        return ERR_WRONG_TYPE;
    }
    if (index >= node->value.child_count) {
        xprintf("%s: array index out of range\n", __FUNCTION__);
        return ERR_OUT_OF_RANGE;
    }
    const void* array_data = node + 1;
    *out_value = ((uint32_t *)array_data)[index];
    return NO_ERROR;
}

mx_status_t mdi_array_uint64(const mdi_node_ref_t* ref, uint32_t index, uint64_t* out_value) {
    const mdi_node_t* node = ref->node;
    if ((node->id & (MDI_TYPE_MASK | MDI_ARRAY_TYPE_MASK)) != MDI_MAKE_ARRAY_ID(MDI_UINT64, 0)) {
        xprintf("%s: ref not an uint64 array\n", __FUNCTION__);
        return ERR_WRONG_TYPE;
    }
    if (index >= node->value.child_count) {
        xprintf("%s: array index out of range\n", __FUNCTION__);
        return ERR_OUT_OF_RANGE;
    }
    const void* array_data = node + 1;
    *out_value = ((uint64_t *)array_data)[index];
    return NO_ERROR;
}

mx_status_t mdi_array_boolean(const mdi_node_ref_t* ref, uint32_t index, bool* out_value) {
    const mdi_node_t* node = ref->node;
    if ((node->id & (MDI_TYPE_MASK | MDI_ARRAY_TYPE_MASK)) != MDI_MAKE_ARRAY_ID(MDI_BOOLEAN, 0)) {
        xprintf("%s: ref not an boolean array\n", __FUNCTION__);
        return ERR_WRONG_TYPE;
    }
    if (index >= node->value.child_count) {
        xprintf("%s: array index out of range\n", __FUNCTION__);
        return ERR_OUT_OF_RANGE;
    }
    const void* array_data = node + 1;
    *out_value = ((bool *)array_data)[index];
    return NO_ERROR;
}

mx_status_t mdi_first_child(const mdi_node_ref_t* ref, mdi_node_ref_t* out_ref) {
    out_ref->node = NULL;
    out_ref->siblings_count = 0;
    out_ref->siblings_end = NULL;

   if (mdi_node_type(ref) != MDI_LIST) {
        xprintf("%s: ref not a list\n", __FUNCTION__);
        return ERR_WRONG_TYPE;
    }

    const mdi_node_t* node = ref->node;
    if (node->value.child_count == 0) {
        return ERR_NOT_FOUND;
    }

    // first child immediately follows list node
    const mdi_node_t* child = &node[1];
    void* siblings_end = (void *)node + node->length;
    if ((void *)child + child->length > siblings_end) {
        xprintf("%s: child length out of range\n", __FUNCTION__);
        return ERR_INVALID_ARGS;
    }

    out_ref->node = child;
    out_ref->siblings_count = node->value.child_count - 1;
    out_ref->siblings_end = siblings_end;
    return NO_ERROR;
}

mx_status_t mdi_next_child(const mdi_node_ref_t* ref, mdi_node_ref_t* out_ref) {
    if (ref->siblings_count == 0) {
        out_ref->node = NULL;
        out_ref->siblings_count = 0;
        out_ref->siblings_end = NULL;
        return ERR_NOT_FOUND;
    }

    const mdi_node_t* node = ref->node;
    const mdi_node_t* next = (const mdi_node_t *)((void *)node + node->length);
    if ((void *)next + next->length > ref->siblings_end) {
        xprintf("%s: child length out of range\n", __FUNCTION__);
        return ERR_INVALID_ARGS;
    }

    out_ref->node = next;
    out_ref->siblings_count = ref->siblings_count - 1;
    out_ref->siblings_end = ref->siblings_end;
    return NO_ERROR;
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

    while (status == NO_ERROR && out_ref->node->id != id) {
        status = mdi_next_child(out_ref, out_ref);
    }
    if (status != NO_ERROR) {
        out_ref->node = NULL;
    }
    return status;
}
