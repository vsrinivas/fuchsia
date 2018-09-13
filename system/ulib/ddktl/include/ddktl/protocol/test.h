// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/test.banjo INSTEAD.

#pragma once

#include <ddk/protocol/test.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "test-internal.h"

// DDK test-protocol support
//
// :: Proxies ::
//
// ddk::TestProtocolProxy is a simple wrapper around
// test_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::TestProtocol is a mixin class that simplifies writing DDK drivers
// that implement the test protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_TEST device.
// class TestDevice {
// using TestDeviceType = ddk::Device<TestDevice, /* ddk mixins */>;
//
// class TestDevice : public TestDeviceType,
//                    public ddk::TestProtocol<TestDevice> {
//   public:
//     TestDevice(zx_device_t* parent)
//         : TestDeviceType("my-test-protocol-device", parent) {}
//
//     void TestSetOutputSocket(zx_handle_t handle);
//
//     zx_handle_t TestGetOutputSocket();
//
//     void TestSetControlChannel(zx_handle_t handle);
//
//     zx_handle_t TestGetControlChannel();
//
//     void TestSetTestFunc(const test_func_t* func);
//
//     zx_status_t TestRunTests(const void* arg_buffer, size_t arg_size, test_report_t* out_report);
//
//     void TestDestroy();
//
//     ...
// };

namespace ddk {

template <typename D>
class TestProtocol : public internal::base_mixin {
public:
    TestProtocol() {
        internal::CheckTestProtocolSubclass<D>();
        test_protocol_ops_.set_output_socket = TestSetOutputSocket;
        test_protocol_ops_.get_output_socket = TestGetOutputSocket;
        test_protocol_ops_.set_control_channel = TestSetControlChannel;
        test_protocol_ops_.get_control_channel = TestGetControlChannel;
        test_protocol_ops_.set_test_func = TestSetTestFunc;
        test_protocol_ops_.run_tests = TestRunTests;
        test_protocol_ops_.destroy = TestDestroy;
    }

protected:
    test_protocol_ops_t test_protocol_ops_ = {};

private:
    // Sets test output socket.
    static void TestSetOutputSocket(void* ctx, zx_handle_t handle) {
        static_cast<D*>(ctx)->TestSetOutputSocket(handle);
    }
    // Gets test output socket.
    static zx_handle_t TestGetOutputSocket(void* ctx) {
        return static_cast<D*>(ctx)->TestGetOutputSocket();
    }
    // Sets control channel.
    static void TestSetControlChannel(void* ctx, zx_handle_t handle) {
        static_cast<D*>(ctx)->TestSetControlChannel(handle);
    }
    // Gets control channel.
    static zx_handle_t TestGetControlChannel(void* ctx) {
        return static_cast<D*>(ctx)->TestGetControlChannel();
    }
    // Sets test function.
    static void TestSetTestFunc(void* ctx, const test_func_t* func) {
        static_cast<D*>(ctx)->TestSetTestFunc(func);
    }
    // Run tests, calls the function set in |SetTestFunc|.
    static zx_status_t TestRunTests(void* ctx, const void* arg_buffer, size_t arg_size,
                                    test_report_t* out_report) {
        return static_cast<D*>(ctx)->TestRunTests(arg_buffer, arg_size, out_report);
    }
    // Calls `device_remove()`.
    static void TestDestroy(void* ctx) { static_cast<D*>(ctx)->TestDestroy(); }
};

class TestProtocolProxy {
public:
    TestProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    TestProtocolProxy(const test_protocol_t* proto) : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(test_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    // Sets test output socket.
    void SetOutputSocket(zx_handle_t handle) { ops_->set_output_socket(ctx_, handle); }
    // Gets test output socket.
    zx_handle_t GetOutputSocket() { return ops_->get_output_socket(ctx_); }
    // Sets control channel.
    void SetControlChannel(zx_handle_t handle) { ops_->set_control_channel(ctx_, handle); }
    // Gets control channel.
    zx_handle_t GetControlChannel() { return ops_->get_control_channel(ctx_); }
    // Sets test function.
    void SetTestFunc(const test_func_t* func) { ops_->set_test_func(ctx_, func); }
    // Run tests, calls the function set in |SetTestFunc|.
    zx_status_t RunTests(const void* arg_buffer, size_t arg_size, test_report_t* out_report) {
        return ops_->run_tests(ctx_, arg_buffer, arg_size, out_report);
    }
    // Calls `device_remove()`.
    void Destroy() { ops_->destroy(ctx_); }

private:
    test_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
