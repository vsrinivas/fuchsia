// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.interface banjo file

#pragma once

#include <tuple>

#include <banjo/examples/interface/cpp/banjo.h>
#include <lib/mock-function/mock-function.h>

namespace ddk {

// This class mocks a device by providing a baker_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockBaker baker;
//
// /* Set some expectations on the device by calling baker.Expect... methods. */
//
// SomeDriver dut(baker.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(baker.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockBaker : ddk::BakerProtocol<MockBaker> {
public:
    MockBaker() : proto_{&baker_protocol_ops_, this} {}

    virtual ~MockBaker() {}

    const baker_protocol_t* GetProto() const { return &proto_; }

    virtual MockBaker& ExpectRegister(cookie_maker_protocol_t intf, cookie_jarrer_protocol_t jar) {
        mock_register_.ExpectCall(intf, jar);
        return *this;
    }

    virtual MockBaker& ExpectChange(change_args_t payload, change_args_t out_payload) {
        mock_change_.ExpectCall({out_payload}, payload);
        return *this;
    }

    virtual MockBaker& ExpectDeRegister() {
        mock_de_register_.ExpectCall();
        return *this;
    }

    void VerifyAndClear() {
        mock_register_.VerifyAndClear();
        mock_change_.VerifyAndClear();
        mock_de_register_.VerifyAndClear();
    }

    virtual void BakerRegister(const cookie_maker_protocol_t* intf, const cookie_jarrer_protocol_t* jar) {
        mock_register_.Call(*intf, *jar);
    }

    virtual void BakerChange(const change_args_t* payload, change_args_t* out_payload) {
        std::tuple<change_args_t> ret = mock_change_.Call(*payload);
        *out_payload = std::get<0>(ret);
    }

    virtual void BakerDeRegister() {
        mock_de_register_.Call();
    }

    mock_function::MockFunction<void, cookie_maker_protocol_t, cookie_jarrer_protocol_t>& mock_register() { return mock_register_; }
    mock_function::MockFunction<std::tuple<change_args_t>, change_args_t>& mock_change() { return mock_change_; }
    mock_function::MockFunction<void>& mock_de_register() { return mock_de_register_; }

protected:
    mock_function::MockFunction<void, cookie_maker_protocol_t, cookie_jarrer_protocol_t> mock_register_;
    mock_function::MockFunction<std::tuple<change_args_t>, change_args_t> mock_change_;
    mock_function::MockFunction<void> mock_de_register_;

private:
    const baker_protocol_t proto_;
};

} // namespace ddk
