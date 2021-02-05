// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocol.vector banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
#define vector_size UINT32_C(32)
typedef struct vector_of_vectors_protocol vector_of_vectors_protocol_t;
typedef struct vector2_protocol vector2_protocol_t;
typedef struct vector_protocol vector_protocol_t;

// Declarations
typedef struct vector_of_vectors_protocol_ops {
    void (*bool)(void* ctx, const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual);
    void (*int8)(void* ctx, const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual);
    void (*int16)(void* ctx, const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual);
    void (*int32)(void* ctx, const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual);
    void (*int64)(void* ctx, const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual);
    void (*uint8)(void* ctx, const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual);
    void (*uint16)(void* ctx, const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual);
    void (*uint32)(void* ctx, const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual);
    void (*uint64)(void* ctx, const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual);
    void (*float32)(void* ctx, const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual);
    void (*float64)(void* ctx, const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual);
    void (*handle)(void* ctx, const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual);
} vector_of_vectors_protocol_ops_t;


struct vector_of_vectors_protocol {
    vector_of_vectors_protocol_ops_t* ops;
    void* ctx;
};

static inline void vector_of_vectors_bool(const vector_of_vectors_protocol_t* proto, const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual) {
    proto->ops->bool(proto->ctx, b_list, b_count, out_b_list, b_count, out_b_actual);
}

static inline void vector_of_vectors_int8(const vector_of_vectors_protocol_t* proto, const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual) {
    proto->ops->int8(proto->ctx, i8_list, i8_count, out_i8_list, i8_count, out_i8_actual);
}

static inline void vector_of_vectors_int16(const vector_of_vectors_protocol_t* proto, const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual) {
    proto->ops->int16(proto->ctx, i16_list, i16_count, out_i16_list, i16_count, out_i16_actual);
}

static inline void vector_of_vectors_int32(const vector_of_vectors_protocol_t* proto, const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual) {
    proto->ops->int32(proto->ctx, i32_list, i32_count, out_i32_list, i32_count, out_i32_actual);
}

static inline void vector_of_vectors_int64(const vector_of_vectors_protocol_t* proto, const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual) {
    proto->ops->int64(proto->ctx, i64_list, i64_count, out_i64_list, i64_count, out_i64_actual);
}

static inline void vector_of_vectors_uint8(const vector_of_vectors_protocol_t* proto, const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual) {
    proto->ops->uint8(proto->ctx, u8_list, u8_count, out_u8_list, u8_count, out_u8_actual);
}

static inline void vector_of_vectors_uint16(const vector_of_vectors_protocol_t* proto, const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual) {
    proto->ops->uint16(proto->ctx, u16_list, u16_count, out_u16_list, u16_count, out_u16_actual);
}

static inline void vector_of_vectors_uint32(const vector_of_vectors_protocol_t* proto, const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual) {
    proto->ops->uint32(proto->ctx, u32_list, u32_count, out_u32_list, u32_count, out_u32_actual);
}

static inline void vector_of_vectors_uint64(const vector_of_vectors_protocol_t* proto, const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual) {
    proto->ops->uint64(proto->ctx, u64_list, u64_count, out_u64_list, u64_count, out_u64_actual);
}

static inline void vector_of_vectors_float32(const vector_of_vectors_protocol_t* proto, const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual) {
    proto->ops->float32(proto->ctx, f32_list, f32_count, out_f32_list, f32_count, out_f32_actual);
}

static inline void vector_of_vectors_float64(const vector_of_vectors_protocol_t* proto, const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
    proto->ops->float64(proto->ctx, u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
}

static inline void vector_of_vectors_handle(const vector_of_vectors_protocol_t* proto, const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
    proto->ops->handle(proto->ctx, u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
}


typedef struct vector2_protocol_ops {
    void (*bool)(void* ctx, const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual);
    void (*int8)(void* ctx, const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual);
    void (*int16)(void* ctx, const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual);
    void (*int32)(void* ctx, const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual);
    void (*int64)(void* ctx, const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual);
    void (*uint8)(void* ctx, const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual);
    void (*uint16)(void* ctx, const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual);
    void (*uint32)(void* ctx, const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual);
    void (*uint64)(void* ctx, const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual);
    void (*float32)(void* ctx, const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual);
    void (*float64)(void* ctx, const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual);
    void (*handle)(void* ctx, const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual);
} vector2_protocol_ops_t;


struct vector2_protocol {
    vector2_protocol_ops_t* ops;
    void* ctx;
};

static inline void vector2_bool(const vector2_protocol_t* proto, const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual) {
    proto->ops->bool(proto->ctx, b_list, b_count, out_b_list, b_count, out_b_actual);
}

static inline void vector2_int8(const vector2_protocol_t* proto, const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual) {
    proto->ops->int8(proto->ctx, i8_list, i8_count, out_i8_list, i8_count, out_i8_actual);
}

static inline void vector2_int16(const vector2_protocol_t* proto, const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual) {
    proto->ops->int16(proto->ctx, i16_list, i16_count, out_i16_list, i16_count, out_i16_actual);
}

static inline void vector2_int32(const vector2_protocol_t* proto, const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual) {
    proto->ops->int32(proto->ctx, i32_list, i32_count, out_i32_list, i32_count, out_i32_actual);
}

static inline void vector2_int64(const vector2_protocol_t* proto, const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual) {
    proto->ops->int64(proto->ctx, i64_list, i64_count, out_i64_list, i64_count, out_i64_actual);
}

static inline void vector2_uint8(const vector2_protocol_t* proto, const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual) {
    proto->ops->uint8(proto->ctx, u8_list, u8_count, out_u8_list, u8_count, out_u8_actual);
}

static inline void vector2_uint16(const vector2_protocol_t* proto, const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual) {
    proto->ops->uint16(proto->ctx, u16_list, u16_count, out_u16_list, u16_count, out_u16_actual);
}

static inline void vector2_uint32(const vector2_protocol_t* proto, const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual) {
    proto->ops->uint32(proto->ctx, u32_list, u32_count, out_u32_list, u32_count, out_u32_actual);
}

static inline void vector2_uint64(const vector2_protocol_t* proto, const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual) {
    proto->ops->uint64(proto->ctx, u64_list, u64_count, out_u64_list, u64_count, out_u64_actual);
}

static inline void vector2_float32(const vector2_protocol_t* proto, const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual) {
    proto->ops->float32(proto->ctx, f32_list, f32_count, out_f32_list, f32_count, out_f32_actual);
}

static inline void vector2_float64(const vector2_protocol_t* proto, const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
    proto->ops->float64(proto->ctx, u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
}

static inline void vector2_handle(const vector2_protocol_t* proto, const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
    proto->ops->handle(proto->ctx, u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
}


typedef struct vector_protocol_ops {
    void (*bool)(void* ctx, const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual);
    void (*int8)(void* ctx, const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual);
    void (*int16)(void* ctx, const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual);
    void (*int32)(void* ctx, const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual);
    void (*int64)(void* ctx, const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual);
    void (*uint8)(void* ctx, const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual);
    void (*uint16)(void* ctx, const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual);
    void (*uint32)(void* ctx, const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual);
    void (*uint64)(void* ctx, const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual);
    void (*float32)(void* ctx, const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual);
    void (*float64)(void* ctx, const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual);
    void (*handle)(void* ctx, const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual);
} vector_protocol_ops_t;


struct vector_protocol {
    vector_protocol_ops_t* ops;
    void* ctx;
};

static inline void vector_bool(const vector_protocol_t* proto, const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual) {
    proto->ops->bool(proto->ctx, b_list, b_count, out_b_list, b_count, out_b_actual);
}

static inline void vector_int8(const vector_protocol_t* proto, const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual) {
    proto->ops->int8(proto->ctx, i8_list, i8_count, out_i8_list, i8_count, out_i8_actual);
}

static inline void vector_int16(const vector_protocol_t* proto, const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual) {
    proto->ops->int16(proto->ctx, i16_list, i16_count, out_i16_list, i16_count, out_i16_actual);
}

static inline void vector_int32(const vector_protocol_t* proto, const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual) {
    proto->ops->int32(proto->ctx, i32_list, i32_count, out_i32_list, i32_count, out_i32_actual);
}

static inline void vector_int64(const vector_protocol_t* proto, const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual) {
    proto->ops->int64(proto->ctx, i64_list, i64_count, out_i64_list, i64_count, out_i64_actual);
}

static inline void vector_uint8(const vector_protocol_t* proto, const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual) {
    proto->ops->uint8(proto->ctx, u8_list, u8_count, out_u8_list, u8_count, out_u8_actual);
}

static inline void vector_uint16(const vector_protocol_t* proto, const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual) {
    proto->ops->uint16(proto->ctx, u16_list, u16_count, out_u16_list, u16_count, out_u16_actual);
}

static inline void vector_uint32(const vector_protocol_t* proto, const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual) {
    proto->ops->uint32(proto->ctx, u32_list, u32_count, out_u32_list, u32_count, out_u32_actual);
}

static inline void vector_uint64(const vector_protocol_t* proto, const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual) {
    proto->ops->uint64(proto->ctx, u64_list, u64_count, out_u64_list, u64_count, out_u64_actual);
}

static inline void vector_float32(const vector_protocol_t* proto, const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual) {
    proto->ops->float32(proto->ctx, f32_list, f32_count, out_f32_list, f32_count, out_f32_actual);
}

static inline void vector_float64(const vector_protocol_t* proto, const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
    proto->ops->float64(proto->ctx, u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
}

static inline void vector_handle(const vector_protocol_t* proto, const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
    proto->ops->handle(proto->ctx, u64_list, u64_count, out_f64_list, f64_count, out_f64_actual);
}



__END_CDECLS
