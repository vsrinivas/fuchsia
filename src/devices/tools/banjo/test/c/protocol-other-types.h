// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocol.other.types banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
#define strings_size UINT32_C(32)
typedef uint32_t this_is_an_enum_t;
#define THIS_IS_AN_ENUM_X UINT32_C(23)
typedef union this_is_aunion this_is_aunion_t;
typedef struct this_is_astruct this_is_astruct_t;
typedef void (*other_types_async_reference_struct_callback)(void* ctx, const this_is_astruct_t* s);
typedef void (*other_types_async_reference_union_callback)(void* ctx, const this_is_aunion_t* u);
typedef void (*other_types_async_reference_string_callback)(void* ctx, const char* s);
typedef void (*other_types_async_reference_string_sized_callback)(void* ctx, const char* s);
typedef void (*other_types_async_reference_string_sized2_callback)(void* ctx, const char* s);
typedef struct other_types_async_reference_protocol other_types_async_reference_protocol_t;
typedef struct other_types_reference_protocol other_types_reference_protocol_t;
typedef void (*other_types_async_struct_callback)(void* ctx, const this_is_astruct_t* s);
typedef void (*other_types_async_union_callback)(void* ctx, const this_is_aunion_t* u);
typedef void (*other_types_async_enum_callback)(void* ctx, this_is_an_enum_t e);
typedef void (*other_types_async_string_callback)(void* ctx, const char* s);
typedef void (*other_types_async_string_sized_callback)(void* ctx, const char* s);
typedef void (*other_types_async_string_sized2_callback)(void* ctx, const char* s);
typedef struct other_types_async_protocol other_types_async_protocol_t;
typedef struct other_types_protocol other_types_protocol_t;
typedef void (*interface_async_callback)(void* ctx, void* intf_ctx, other_types_protocol_ops_t* intf_ops);
typedef void (*interface_async_refernce_callback)(void* ctx, void* intf_ctx, other_types_protocol_ops_t* intf_ops);
typedef struct interface_protocol interface_protocol_t;

// Declarations
union this_is_aunion {
    const char* s;
};

struct this_is_astruct {
    const char* s;
};

typedef struct other_types_async_reference_protocol_ops {
    void (*struct)(void* ctx, const this_is_astruct_t* s, other_types_async_reference_struct_callback callback, void* cookie);
    void (*union)(void* ctx, const this_is_aunion_t* u, other_types_async_reference_union_callback callback, void* cookie);
    void (*string)(void* ctx, const char* s, other_types_async_reference_string_callback callback, void* cookie);
    void (*string_sized)(void* ctx, const char* s, other_types_async_reference_string_sized_callback callback, void* cookie);
    void (*string_sized2)(void* ctx, const char* s, other_types_async_reference_string_sized2_callback callback, void* cookie);
} other_types_async_reference_protocol_ops_t;


struct other_types_async_reference_protocol {
    other_types_async_reference_protocol_ops_t* ops;
    void* ctx;
};

static inline void other_types_async_reference_struct(const other_types_async_reference_protocol_t* proto, const this_is_astruct_t* s, other_types_async_reference_struct_callback callback, void* cookie) {
    proto->ops->struct(proto->ctx, s, callback, cookie);
}

static inline void other_types_async_reference_union(const other_types_async_reference_protocol_t* proto, const this_is_aunion_t* u, other_types_async_reference_union_callback callback, void* cookie) {
    proto->ops->union(proto->ctx, u, callback, cookie);
}

static inline void other_types_async_reference_string(const other_types_async_reference_protocol_t* proto, const char* s, other_types_async_reference_string_callback callback, void* cookie) {
    proto->ops->string(proto->ctx, s, callback, cookie);
}

static inline void other_types_async_reference_string_sized(const other_types_async_reference_protocol_t* proto, const char* s, other_types_async_reference_string_sized_callback callback, void* cookie) {
    proto->ops->string_sized(proto->ctx, s, callback, cookie);
}

static inline void other_types_async_reference_string_sized2(const other_types_async_reference_protocol_t* proto, const char* s, other_types_async_reference_string_sized2_callback callback, void* cookie) {
    proto->ops->string_sized2(proto->ctx, s, callback, cookie);
}


typedef struct other_types_reference_protocol_ops {
    void (*struct)(void* ctx, const this_is_astruct_t* s, this_is_astruct_t** out_s);
    void (*union)(void* ctx, const this_is_aunion_t* u, this_is_aunion_t** out_u);
    void (*string)(void* ctx, const char* s, char* out_s, size_t s_capacity);
    void (*string_sized)(void* ctx, const char* s, char* out_s, size_t s_capacity);
    void (*string_sized2)(void* ctx, const char* s, char* out_s, size_t s_capacity);
} other_types_reference_protocol_ops_t;


struct other_types_reference_protocol {
    other_types_reference_protocol_ops_t* ops;
    void* ctx;
};

static inline void other_types_reference_struct(const other_types_reference_protocol_t* proto, const this_is_astruct_t* s, this_is_astruct_t** out_s) {
    proto->ops->struct(proto->ctx, s, out_s);
}

static inline void other_types_reference_union(const other_types_reference_protocol_t* proto, const this_is_aunion_t* u, this_is_aunion_t** out_u) {
    proto->ops->union(proto->ctx, u, out_u);
}

static inline void other_types_reference_string(const other_types_reference_protocol_t* proto, const char* s, char* out_s, size_t s_capacity) {
    proto->ops->string(proto->ctx, s, out_s, s_capacity);
}

static inline void other_types_reference_string_sized(const other_types_reference_protocol_t* proto, const char* s, char* out_s, size_t s_capacity) {
    proto->ops->string_sized(proto->ctx, s, out_s, s_capacity);
}

static inline void other_types_reference_string_sized2(const other_types_reference_protocol_t* proto, const char* s, char* out_s, size_t s_capacity) {
    proto->ops->string_sized2(proto->ctx, s, out_s, s_capacity);
}


typedef struct other_types_async_protocol_ops {
    void (*struct)(void* ctx, const this_is_astruct_t* s, other_types_async_struct_callback callback, void* cookie);
    void (*union)(void* ctx, const this_is_aunion_t* u, other_types_async_union_callback callback, void* cookie);
    void (*enum)(void* ctx, this_is_an_enum_t e, other_types_async_enum_callback callback, void* cookie);
    void (*string)(void* ctx, const char* s, other_types_async_string_callback callback, void* cookie);
    void (*string_sized)(void* ctx, const char* s, other_types_async_string_sized_callback callback, void* cookie);
    void (*string_sized2)(void* ctx, const char* s, other_types_async_string_sized2_callback callback, void* cookie);
} other_types_async_protocol_ops_t;


struct other_types_async_protocol {
    other_types_async_protocol_ops_t* ops;
    void* ctx;
};

static inline void other_types_async_struct(const other_types_async_protocol_t* proto, const this_is_astruct_t* s, other_types_async_struct_callback callback, void* cookie) {
    proto->ops->struct(proto->ctx, s, callback, cookie);
}

static inline void other_types_async_union(const other_types_async_protocol_t* proto, const this_is_aunion_t* u, other_types_async_union_callback callback, void* cookie) {
    proto->ops->union(proto->ctx, u, callback, cookie);
}

static inline void other_types_async_enum(const other_types_async_protocol_t* proto, this_is_an_enum_t e, other_types_async_enum_callback callback, void* cookie) {
    proto->ops->enum(proto->ctx, e, callback, cookie);
}

static inline void other_types_async_string(const other_types_async_protocol_t* proto, const char* s, other_types_async_string_callback callback, void* cookie) {
    proto->ops->string(proto->ctx, s, callback, cookie);
}

static inline void other_types_async_string_sized(const other_types_async_protocol_t* proto, const char* s, other_types_async_string_sized_callback callback, void* cookie) {
    proto->ops->string_sized(proto->ctx, s, callback, cookie);
}

static inline void other_types_async_string_sized2(const other_types_async_protocol_t* proto, const char* s, other_types_async_string_sized2_callback callback, void* cookie) {
    proto->ops->string_sized2(proto->ctx, s, callback, cookie);
}


typedef struct other_types_protocol_ops {
    void (*struct)(void* ctx, const this_is_astruct_t* s, this_is_astruct_t* out_s);
    void (*union)(void* ctx, const this_is_aunion_t* u, this_is_aunion_t* out_u);
    this_is_an_enum_t (*enum)(void* ctx, this_is_an_enum_t e);
    void (*string)(void* ctx, const char* s, char* out_s, size_t s_capacity);
    void (*string_sized)(void* ctx, const char* s, char* out_s, size_t s_capacity);
    void (*string_sized2)(void* ctx, const char* s, char* out_s, size_t s_capacity);
} other_types_protocol_ops_t;


struct other_types_protocol {
    other_types_protocol_ops_t* ops;
    void* ctx;
};

static inline void other_types_struct(const other_types_protocol_t* proto, const this_is_astruct_t* s, this_is_astruct_t* out_s) {
    proto->ops->struct(proto->ctx, s, out_s);
}

static inline void other_types_union(const other_types_protocol_t* proto, const this_is_aunion_t* u, this_is_aunion_t* out_u) {
    proto->ops->union(proto->ctx, u, out_u);
}

static inline this_is_an_enum_t other_types_enum(const other_types_protocol_t* proto, this_is_an_enum_t e) {
    return proto->ops->enum(proto->ctx, e);
}

static inline void other_types_string(const other_types_protocol_t* proto, const char* s, char* out_s, size_t s_capacity) {
    proto->ops->string(proto->ctx, s, out_s, s_capacity);
}

static inline void other_types_string_sized(const other_types_protocol_t* proto, const char* s, char* out_s, size_t s_capacity) {
    proto->ops->string_sized(proto->ctx, s, out_s, s_capacity);
}

static inline void other_types_string_sized2(const other_types_protocol_t* proto, const char* s, char* out_s, size_t s_capacity) {
    proto->ops->string_sized2(proto->ctx, s, out_s, s_capacity);
}


typedef struct interface_protocol_ops {
    void (*value)(void* ctx, const other_types_protocol_t* intf, other_types_protocol_t* out_intf);
    void (*reference)(void* ctx, const other_types_protocol_t* intf, other_types_protocol_t** out_intf);
    void (*async)(void* ctx, const other_types_protocol_t* intf, interface_async_callback callback, void* cookie);
    void (*async_refernce)(void* ctx, const other_types_protocol_t* intf, interface_async_refernce_callback callback, void* cookie);
} interface_protocol_ops_t;


struct interface_protocol {
    interface_protocol_ops_t* ops;
    void* ctx;
};

static inline void interface_value(const interface_protocol_t* proto, void* intf_ctx, other_types_protocol_ops_t* intf_ops, other_types_protocol_t* out_intf) {
    const other_types_protocol_t intf2 = {
        .ops = intf_ops,
        .ctx = intf_ctx,
    };
    const other_types_protocol_t* intf = &intf2;
    proto->ops->value(proto->ctx, intf, out_intf);
}

static inline void interface_reference(const interface_protocol_t* proto, void* intf_ctx, other_types_protocol_ops_t* intf_ops, other_types_protocol_t** out_intf) {
    const other_types_protocol_t intf2 = {
        .ops = intf_ops,
        .ctx = intf_ctx,
    };
    const other_types_protocol_t* intf = &intf2;
    proto->ops->reference(proto->ctx, intf, out_intf);
}

static inline void interface_async(const interface_protocol_t* proto, void* intf_ctx, other_types_protocol_ops_t* intf_ops, interface_async_callback callback, void* cookie) {
    const other_types_protocol_t intf2 = {
        .ops = intf_ops,
        .ctx = intf_ctx,
    };
    const other_types_protocol_t* intf = &intf2;
    proto->ops->async(proto->ctx, intf, callback, cookie);
}

static inline void interface_async_refernce(const interface_protocol_t* proto, void* intf_ctx, other_types_protocol_ops_t* intf_ops, interface_async_refernce_callback callback, void* cookie) {
    const other_types_protocol_t intf2 = {
        .ops = intf_ops,
        .ctx = intf_ctx,
    };
    const other_types_protocol_t* intf = &intf2;
    proto->ops->async_refernce(proto->ctx, intf, callback, cookie);
}



__END_CDECLS
