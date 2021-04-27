// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.passcallback banjo file

#pragma once

#include <tuple>

#include <banjo/examples/passcallback/cpp/banjo.h>
#include <lib/mock-function/mock-function.h>

namespace ddk {

// This class mocks a device by providing a action_protocol_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockActionProtocol action_protocol;
//
// /* Set some expectations on the device by calling action_protocol.Expect... methods. */
//
// SomeDriver dut(action_protocol.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(action_protocol.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockActionProtocol : ddk::ActionProtocolProtocol<MockActionProtocol> {
public:
    MockActionProtocol() : proto_{&action_protocol_protocol_ops_, this} {}

    virtual ~MockActionProtocol() {}

    const action_protocol_protocol_t* GetProto() const { return &proto_; }

    virtual MockActionProtocol& ExpectRegisterCallback(zx_status_t out_s, uint32_t id, action_notify_t cb) {
        mock_register_callback_.ExpectCall({out_s}, id, cb);
        return *this;
    }

    virtual MockActionProtocol& ExpectGetCallback(zx_status_t out_s, uint32_t id, action_notify_t out_cb) {
        mock_get_callback_.ExpectCall({out_s, out_cb}, id);
        return *this;
    }

    void VerifyAndClear() {
        mock_register_callback_.VerifyAndClear();
        mock_get_callback_.VerifyAndClear();
    }

    virtual zx_status_t ActionProtocolRegisterCallback(uint32_t id, const action_notify_t* cb) {
        std::tuple<zx_status_t> ret = mock_register_callback_.Call(id, *cb);
        return std::get<0>(ret);
    }

    virtual zx_status_t ActionProtocolGetCallback(uint32_t id, action_notify_t* out_cb) {
        std::tuple<zx_status_t, action_notify_t> ret = mock_get_callback_.Call(id);
        *out_cb = std::get<1>(ret);
        return std::get<0>(ret);
    }

    mock_function::MockFunction<std::tuple<zx_status_t>, uint32_t, action_notify_t>& mock_register_callback() { return mock_register_callback_; }
    mock_function::MockFunction<std::tuple<zx_status_t, action_notify_t>, uint32_t>& mock_get_callback() { return mock_get_callback_; }

protected:
    mock_function::MockFunction<std::tuple<zx_status_t>, uint32_t, action_notify_t> mock_register_callback_;
    mock_function::MockFunction<std::tuple<zx_status_t, action_notify_t>, uint32_t> mock_get_callback_;

private:
    const action_protocol_protocol_t proto_;
};

} // namespace ddk
