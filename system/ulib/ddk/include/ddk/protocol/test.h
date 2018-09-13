// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/test.banjo INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct test_report test_report_t;
typedef struct test_func test_func_t;
typedef struct test_protocol test_protocol_t;

// Declarations

struct test_report {
    uint32_t n_tests;
    uint32_t n_success;
    uint32_t n_failed;
};

struct test_func {
    zx_status_t (*callback)(void* ctx, const void* arg_buffer, size_t arg_size,
                            test_report_t* out_report);
    void* ctx;
};

typedef struct test_protocol_ops {
    void (*set_output_socket)(void* ctx, zx_handle_t handle);
    zx_handle_t (*get_output_socket)(void* ctx);
    void (*set_control_channel)(void* ctx, zx_handle_t handle);
    zx_handle_t (*get_control_channel)(void* ctx);
    void (*set_test_func)(void* ctx, const test_func_t* func);
    zx_status_t (*run_tests)(void* ctx, const void* arg_buffer, size_t arg_size,
                             test_report_t* out_report);
    void (*destroy)(void* ctx);
} test_protocol_ops_t;

struct test_protocol {
    test_protocol_ops_t* ops;
    void* ctx;
};

// Sets test output socket.
static inline void test_set_output_socket(const test_protocol_t* proto, zx_handle_t handle) {
    proto->ops->set_output_socket(proto->ctx, handle);
}
// Gets test output socket.
static inline zx_handle_t test_get_output_socket(const test_protocol_t* proto) {
    return proto->ops->get_output_socket(proto->ctx);
}
// Sets control channel.
static inline void test_set_control_channel(const test_protocol_t* proto, zx_handle_t handle) {
    proto->ops->set_control_channel(proto->ctx, handle);
}
// Gets control channel.
static inline zx_handle_t test_get_control_channel(const test_protocol_t* proto) {
    return proto->ops->get_control_channel(proto->ctx);
}
// Sets test function.
static inline void test_set_test_func(const test_protocol_t* proto, const test_func_t* func) {
    proto->ops->set_test_func(proto->ctx, func);
}
// Run tests, calls the function set in |SetTestFunc|.
static inline zx_status_t test_run_tests(const test_protocol_t* proto, const void* arg_buffer,
                                         size_t arg_size, test_report_t* out_report) {
    return proto->ops->run_tests(proto->ctx, arg_buffer, arg_size, out_report);
}
// Calls `device_remove()`.
static inline void test_destroy(const test_protocol_t* proto) {
    proto->ops->destroy(proto->ctx);
}

__END_CDECLS;
