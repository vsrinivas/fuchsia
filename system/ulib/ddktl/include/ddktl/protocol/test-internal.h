// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/test.banjo INSTEAD.

#pragma once

#include <ddk/protocol/test.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_test_protocol_set_output_socket, TestSetOutputSocket,
                                     void (C::*)(zx_handle_t handle));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_test_protocol_get_output_socket, TestGetOutputSocket,
                                     zx_handle_t (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_test_protocol_set_control_channel, TestSetControlChannel,
                                     void (C::*)(zx_handle_t handle));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_test_protocol_get_control_channel, TestGetControlChannel,
                                     zx_handle_t (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_test_protocol_set_test_func, TestSetTestFunc,
                                     void (C::*)(const test_func_t* func));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_test_protocol_run_tests, TestRunTests,
                                     zx_status_t (C::*)(const void* arg_buffer, size_t arg_size,
                                                        test_report_t* out_report));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_test_protocol_destroy, TestDestroy, void (C::*)());

template <typename D>
constexpr void CheckTestProtocolSubclass() {
    static_assert(internal::has_test_protocol_set_output_socket<D>::value,
                  "TestProtocol subclasses must implement "
                  "void TestSetOutputSocket(zx_handle_t handle");
    static_assert(internal::has_test_protocol_get_output_socket<D>::value,
                  "TestProtocol subclasses must implement "
                  "zx_handle_t TestGetOutputSocket(");
    static_assert(internal::has_test_protocol_set_control_channel<D>::value,
                  "TestProtocol subclasses must implement "
                  "void TestSetControlChannel(zx_handle_t handle");
    static_assert(internal::has_test_protocol_get_control_channel<D>::value,
                  "TestProtocol subclasses must implement "
                  "zx_handle_t TestGetControlChannel(");
    static_assert(internal::has_test_protocol_set_test_func<D>::value,
                  "TestProtocol subclasses must implement "
                  "void TestSetTestFunc(const test_func_t* func");
    static_assert(internal::has_test_protocol_run_tests<D>::value,
                  "TestProtocol subclasses must implement "
                  "zx_status_t TestRunTests(const void* arg_buffer, size_t arg_size, "
                  "test_report_t* out_report");
    static_assert(internal::has_test_protocol_destroy<D>::value,
                  "TestProtocol subclasses must implement "
                  "void TestDestroy(");
}

} // namespace internal
} // namespace ddk
