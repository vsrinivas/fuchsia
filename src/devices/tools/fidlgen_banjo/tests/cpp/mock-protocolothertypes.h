// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocolothertypes banjo file

#pragma once

#include <string.h>

#include <string>
#include <tuple>

#include <banjo/examples/protocolothertypes/cpp/banjo.h>
#include <lib/mock-function/mock-function.h>

namespace ddk {

// This class mocks a device by providing a other_types_reference_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockOtherTypesReference other_types_reference;
//
// /* Set some expectations on the device by calling other_types_reference.Expect... methods. */
//
// SomeDriver dut(other_types_reference.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(other_types_reference.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockOtherTypesReference : ddk::OtherTypesReferenceProtocol<MockOtherTypesReference> {
public:
    MockOtherTypesReference() : proto_{&other_types_reference_protocol_ops_, this} {}

    virtual ~MockOtherTypesReference() {}

    const other_types_reference_protocol_t* GetProto() const { return &proto_; }

    virtual MockOtherTypesReference& ExpectStruct(this_is_astruct_t s, this_is_astruct_t out_s) {
        mock_struct_.ExpectCall({out_s}, s);
        return *this;
    }

    virtual MockOtherTypesReference& ExpectUnion(this_is_aunion_t u, this_is_aunion_t out_u) {
        mock_union_.ExpectCall({out_u}, u);
        return *this;
    }

    virtual MockOtherTypesReference& ExpectString(std::string s, std::string s) {
        mock_string_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    virtual MockOtherTypesReference& ExpectStringSized(std::string s, std::string s) {
        mock_string_sized_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    virtual MockOtherTypesReference& ExpectStringSized2(std::string s, std::string s) {
        mock_string_sized2_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    void VerifyAndClear() {
        mock_struct_.VerifyAndClear();
        mock_union_.VerifyAndClear();
        mock_string_.VerifyAndClear();
        mock_string_sized_.VerifyAndClear();
        mock_string_sized2_.VerifyAndClear();
    }

    virtual void OtherTypesReferenceStruct(const this_is_astruct_t* s, this_is_astruct_t** out_s) {
        std::tuple<this_is_astruct_t> ret = mock_struct_.Call(*s);
        *out_s = std::get<0>(ret);
    }

    virtual void OtherTypesReferenceUnion(const this_is_aunion_t* u, this_is_aunion_t** out_u) {
        std::tuple<this_is_aunion_t> ret = mock_union_.Call(*u);
        *out_u = std::get<0>(ret);
    }

    virtual void OtherTypesReferenceString(const char* s, char* out_s, size_t s_capacity) {
        std::tuple<std::string> ret = mock_string_.Call(std::string(s));
        strncpy(out_s, std::get<0>(ret).c_str(), s_capacity));
    }

    virtual void OtherTypesReferenceStringSized(const char* s, char* out_s, size_t s_capacity) {
        std::tuple<std::string> ret = mock_string_sized_.Call(std::string(s));
        strncpy(out_s, std::get<0>(ret).c_str(), s_capacity));
    }

    virtual void OtherTypesReferenceStringSized2(const char* s, char* out_s, size_t s_capacity) {
        std::tuple<std::string> ret = mock_string_sized2_.Call(std::string(s));
        strncpy(out_s, std::get<0>(ret).c_str(), s_capacity));
    }

    mock_function::MockFunction<std::tuple<this_is_astruct_t>, this_is_astruct_t>& mock_struct() { return mock_struct_; }
    mock_function::MockFunction<std::tuple<this_is_aunion_t>, this_is_aunion_t>& mock_union() { return mock_union_; }
    mock_function::MockFunction<std::tuple<std::string>, std::string>& mock_string() { return mock_string_; }
    mock_function::MockFunction<std::tuple<std::string>, std::string>& mock_string_sized() { return mock_string_sized_; }
    mock_function::MockFunction<std::tuple<std::string>, std::string>& mock_string_sized2() { return mock_string_sized2_; }

protected:
    mock_function::MockFunction<std::tuple<this_is_astruct_t>, this_is_astruct_t> mock_struct_;
    mock_function::MockFunction<std::tuple<this_is_aunion_t>, this_is_aunion_t> mock_union_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_sized_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_sized2_;

private:
    const other_types_reference_protocol_t proto_;
};

// This class mocks a device by providing a other_types_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockOtherTypes other_types;
//
// /* Set some expectations on the device by calling other_types.Expect... methods. */
//
// SomeDriver dut(other_types.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(other_types.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockOtherTypes : ddk::OtherTypesProtocol<MockOtherTypes> {
public:
    MockOtherTypes() : proto_{&other_types_protocol_ops_, this} {}

    virtual ~MockOtherTypes() {}

    const other_types_protocol_t* GetProto() const { return &proto_; }

    virtual MockOtherTypes& ExpectStruct(this_is_astruct_t s, this_is_astruct_t out_s) {
        mock_struct_.ExpectCall({out_s}, s);
        return *this;
    }

    virtual MockOtherTypes& ExpectUnion(this_is_aunion_t u, this_is_aunion_t out_u) {
        mock_union_.ExpectCall({out_u}, u);
        return *this;
    }

    virtual MockOtherTypes& ExpectEnum(this_is_an_enum_t out_e, this_is_an_enum_t e) {
        mock_enum_.ExpectCall({out_e}, e);
        return *this;
    }

    virtual MockOtherTypes& ExpectBits(this_is_abits_t out_e, this_is_abits_t e) {
        mock_bits_.ExpectCall({out_e}, e);
        return *this;
    }

    virtual MockOtherTypes& ExpectString(std::string s, std::string s) {
        mock_string_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    virtual MockOtherTypes& ExpectStringSized(std::string s, std::string s) {
        mock_string_sized_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    virtual MockOtherTypes& ExpectStringSized2(std::string s, std::string s) {
        mock_string_sized2_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    virtual MockOtherTypes& ExpectInlineTable(uint32_t out_response_member, uint32_t request_member) {
        mock_inline_table_.ExpectCall({out_response_member}, request_member);
        return *this;
    }

    void VerifyAndClear() {
        mock_struct_.VerifyAndClear();
        mock_union_.VerifyAndClear();
        mock_enum_.VerifyAndClear();
        mock_bits_.VerifyAndClear();
        mock_string_.VerifyAndClear();
        mock_string_sized_.VerifyAndClear();
        mock_string_sized2_.VerifyAndClear();
        mock_inline_table_.VerifyAndClear();
    }

    virtual void OtherTypesStruct(const this_is_astruct_t* s, this_is_astruct_t* out_s) {
        std::tuple<this_is_astruct_t> ret = mock_struct_.Call(*s);
        *out_s = std::get<0>(ret);
    }

    virtual void OtherTypesUnion(const this_is_aunion_t* u, this_is_aunion_t* out_u) {
        std::tuple<this_is_aunion_t> ret = mock_union_.Call(*u);
        *out_u = std::get<0>(ret);
    }

    virtual this_is_an_enum_t OtherTypesEnum(this_is_an_enum_t e) {
        std::tuple<this_is_an_enum_t> ret = mock_enum_.Call(e);
        return std::get<0>(ret);
    }

    virtual this_is_abits_t OtherTypesBits(this_is_abits_t e) {
        std::tuple<this_is_abits_t> ret = mock_bits_.Call(e);
        return std::get<0>(ret);
    }

    virtual void OtherTypesString(const char* s, char* out_s, size_t s_capacity) {
        std::tuple<std::string> ret = mock_string_.Call(std::string(s));
        strncpy(out_s, std::get<0>(ret).c_str(), s_capacity));
    }

    virtual void OtherTypesStringSized(const char* s, char* out_s, size_t s_capacity) {
        std::tuple<std::string> ret = mock_string_sized_.Call(std::string(s));
        strncpy(out_s, std::get<0>(ret).c_str(), s_capacity));
    }

    virtual void OtherTypesStringSized2(const char* s, char* out_s, size_t s_capacity) {
        std::tuple<std::string> ret = mock_string_sized2_.Call(std::string(s));
        strncpy(out_s, std::get<0>(ret).c_str(), s_capacity));
    }

    virtual uint32_t OtherTypesInlineTable(uint32_t request_member) {
        std::tuple<uint32_t> ret = mock_inline_table_.Call(request_member);
        return std::get<0>(ret);
    }

    mock_function::MockFunction<std::tuple<this_is_astruct_t>, this_is_astruct_t>& mock_struct() { return mock_struct_; }
    mock_function::MockFunction<std::tuple<this_is_aunion_t>, this_is_aunion_t>& mock_union() { return mock_union_; }
    mock_function::MockFunction<std::tuple<this_is_an_enum_t>, this_is_an_enum_t>& mock_enum() { return mock_enum_; }
    mock_function::MockFunction<std::tuple<this_is_abits_t>, this_is_abits_t>& mock_bits() { return mock_bits_; }
    mock_function::MockFunction<std::tuple<std::string>, std::string>& mock_string() { return mock_string_; }
    mock_function::MockFunction<std::tuple<std::string>, std::string>& mock_string_sized() { return mock_string_sized_; }
    mock_function::MockFunction<std::tuple<std::string>, std::string>& mock_string_sized2() { return mock_string_sized2_; }
    mock_function::MockFunction<std::tuple<uint32_t>, uint32_t>& mock_inline_table() { return mock_inline_table_; }

protected:
    mock_function::MockFunction<std::tuple<this_is_astruct_t>, this_is_astruct_t> mock_struct_;
    mock_function::MockFunction<std::tuple<this_is_aunion_t>, this_is_aunion_t> mock_union_;
    mock_function::MockFunction<std::tuple<this_is_an_enum_t>, this_is_an_enum_t> mock_enum_;
    mock_function::MockFunction<std::tuple<this_is_abits_t>, this_is_abits_t> mock_bits_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_sized_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_sized2_;
    mock_function::MockFunction<std::tuple<uint32_t>, uint32_t> mock_inline_table_;

private:
    const other_types_protocol_t proto_;
};

// This class mocks a device by providing a other_types_async_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockOtherTypesAsync other_types_async;
//
// /* Set some expectations on the device by calling other_types_async.Expect... methods. */
//
// SomeDriver dut(other_types_async.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(other_types_async.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockOtherTypesAsync : ddk::OtherTypesAsyncProtocol<MockOtherTypesAsync> {
public:
    MockOtherTypesAsync() : proto_{&other_types_async_protocol_ops_, this} {}

    virtual ~MockOtherTypesAsync() {}

    const other_types_async_protocol_t* GetProto() const { return &proto_; }

    virtual MockOtherTypesAsync& ExpectStruct(this_is_astruct_t s, this_is_astruct_t out_s) {
        mock_struct_.ExpectCall({out_s}, s);
        return *this;
    }

    virtual MockOtherTypesAsync& ExpectUnion(this_is_aunion_t u, this_is_aunion_t out_u) {
        mock_union_.ExpectCall({out_u}, u);
        return *this;
    }

    virtual MockOtherTypesAsync& ExpectEnum(this_is_an_enum_t e, this_is_an_enum_t out_e) {
        mock_enum_.ExpectCall({out_e}, e);
        return *this;
    }

    virtual MockOtherTypesAsync& ExpectBits(this_is_abits_t e, this_is_abits_t out_e) {
        mock_bits_.ExpectCall({out_e}, e);
        return *this;
    }

    virtual MockOtherTypesAsync& ExpectString(std::string s, std::string s) {
        mock_string_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    virtual MockOtherTypesAsync& ExpectStringSized(std::string s, std::string s) {
        mock_string_sized_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    virtual MockOtherTypesAsync& ExpectStringSized2(std::string s, std::string s) {
        mock_string_sized2_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    void VerifyAndClear() {
        mock_struct_.VerifyAndClear();
        mock_union_.VerifyAndClear();
        mock_enum_.VerifyAndClear();
        mock_bits_.VerifyAndClear();
        mock_string_.VerifyAndClear();
        mock_string_sized_.VerifyAndClear();
        mock_string_sized2_.VerifyAndClear();
    }

    virtual void OtherTypesAsyncStruct(const this_is_astruct_t* s, other_types_async_struct_callback callback, void* cookie) {
        std::tuple<this_is_astruct_t> ret = mock_struct_.Call(*s);
        callback(cookie, &std::get<0>(ret));
    }

    virtual void OtherTypesAsyncUnion(const this_is_aunion_t* u, other_types_async_union_callback callback, void* cookie) {
        std::tuple<this_is_aunion_t> ret = mock_union_.Call(*u);
        callback(cookie, &std::get<0>(ret));
    }

    virtual void OtherTypesAsyncEnum(this_is_an_enum_t e, other_types_async_enum_callback callback, void* cookie) {
        std::tuple<this_is_an_enum_t> ret = mock_enum_.Call(e);
        callback(cookie, std::get<0>(ret));
    }

    virtual void OtherTypesAsyncBits(this_is_abits_t e, other_types_async_bits_callback callback, void* cookie) {
        std::tuple<this_is_abits_t> ret = mock_bits_.Call(e);
        callback(cookie, std::get<0>(ret));
    }

    virtual void OtherTypesAsyncString(const char* s, other_types_async_string_callback callback, void* cookie) {
        std::tuple<std::string> ret = mock_string_.Call(std::string(s));
        callback(cookie, std::get<0>(ret).c_str());
    }

    virtual void OtherTypesAsyncStringSized(const char* s, other_types_async_string_sized_callback callback, void* cookie) {
        std::tuple<std::string> ret = mock_string_sized_.Call(std::string(s));
        callback(cookie, std::get<0>(ret).c_str());
    }

    virtual void OtherTypesAsyncStringSized2(const char* s, other_types_async_string_sized2_callback callback, void* cookie) {
        std::tuple<std::string> ret = mock_string_sized2_.Call(std::string(s));
        callback(cookie, std::get<0>(ret).c_str());
    }

    mock_function::MockFunction<std::tuple<this_is_astruct_t>, this_is_astruct_t>& mock_struct() { return mock_struct_; }
    mock_function::MockFunction<std::tuple<this_is_aunion_t>, this_is_aunion_t>& mock_union() { return mock_union_; }
    mock_function::MockFunction<std::tuple<this_is_an_enum_t>, this_is_an_enum_t>& mock_enum() { return mock_enum_; }
    mock_function::MockFunction<std::tuple<this_is_abits_t>, this_is_abits_t>& mock_bits() { return mock_bits_; }
    mock_function::MockFunction<std::tuple<std::string>, std::string>& mock_string() { return mock_string_; }
    mock_function::MockFunction<std::tuple<std::string>, std::string>& mock_string_sized() { return mock_string_sized_; }
    mock_function::MockFunction<std::tuple<std::string>, std::string>& mock_string_sized2() { return mock_string_sized2_; }

protected:
    mock_function::MockFunction<std::tuple<this_is_astruct_t>, this_is_astruct_t> mock_struct_;
    mock_function::MockFunction<std::tuple<this_is_aunion_t>, this_is_aunion_t> mock_union_;
    mock_function::MockFunction<std::tuple<this_is_an_enum_t>, this_is_an_enum_t> mock_enum_;
    mock_function::MockFunction<std::tuple<this_is_abits_t>, this_is_abits_t> mock_bits_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_sized_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_sized2_;

private:
    const other_types_async_protocol_t proto_;
};

// This class mocks a device by providing a other_types_async_reference_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockOtherTypesAsyncReference other_types_async_reference;
//
// /* Set some expectations on the device by calling other_types_async_reference.Expect... methods. */
//
// SomeDriver dut(other_types_async_reference.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(other_types_async_reference.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockOtherTypesAsyncReference : ddk::OtherTypesAsyncReferenceProtocol<MockOtherTypesAsyncReference> {
public:
    MockOtherTypesAsyncReference() : proto_{&other_types_async_reference_protocol_ops_, this} {}

    virtual ~MockOtherTypesAsyncReference() {}

    const other_types_async_reference_protocol_t* GetProto() const { return &proto_; }

    virtual MockOtherTypesAsyncReference& ExpectStruct(this_is_astruct_t s, this_is_astruct_t out_s) {
        mock_struct_.ExpectCall({out_s}, s);
        return *this;
    }

    virtual MockOtherTypesAsyncReference& ExpectUnion(this_is_aunion_t u, this_is_aunion_t out_u) {
        mock_union_.ExpectCall({out_u}, u);
        return *this;
    }

    virtual MockOtherTypesAsyncReference& ExpectString(std::string s, std::string s) {
        mock_string_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    virtual MockOtherTypesAsyncReference& ExpectStringSized(std::string s, std::string s) {
        mock_string_sized_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    virtual MockOtherTypesAsyncReference& ExpectStringSized2(std::string s, std::string s) {
        mock_string_sized2_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    void VerifyAndClear() {
        mock_struct_.VerifyAndClear();
        mock_union_.VerifyAndClear();
        mock_string_.VerifyAndClear();
        mock_string_sized_.VerifyAndClear();
        mock_string_sized2_.VerifyAndClear();
    }

    virtual void OtherTypesAsyncReferenceStruct(const this_is_astruct_t* s, other_types_async_reference_struct_callback callback, void* cookie) {
        std::tuple<this_is_astruct_t> ret = mock_struct_.Call(*s);
        callback(cookie, &std::get<0>(ret));
    }

    virtual void OtherTypesAsyncReferenceUnion(const this_is_aunion_t* u, other_types_async_reference_union_callback callback, void* cookie) {
        std::tuple<this_is_aunion_t> ret = mock_union_.Call(*u);
        callback(cookie, &std::get<0>(ret));
    }

    virtual void OtherTypesAsyncReferenceString(const char* s, other_types_async_reference_string_callback callback, void* cookie) {
        std::tuple<std::string> ret = mock_string_.Call(std::string(s));
        callback(cookie, std::get<0>(ret).c_str());
    }

    virtual void OtherTypesAsyncReferenceStringSized(const char* s, other_types_async_reference_string_sized_callback callback, void* cookie) {
        std::tuple<std::string> ret = mock_string_sized_.Call(std::string(s));
        callback(cookie, std::get<0>(ret).c_str());
    }

    virtual void OtherTypesAsyncReferenceStringSized2(const char* s, other_types_async_reference_string_sized2_callback callback, void* cookie) {
        std::tuple<std::string> ret = mock_string_sized2_.Call(std::string(s));
        callback(cookie, std::get<0>(ret).c_str());
    }

    mock_function::MockFunction<std::tuple<this_is_astruct_t>, this_is_astruct_t>& mock_struct() { return mock_struct_; }
    mock_function::MockFunction<std::tuple<this_is_aunion_t>, this_is_aunion_t>& mock_union() { return mock_union_; }
    mock_function::MockFunction<std::tuple<std::string>, std::string>& mock_string() { return mock_string_; }
    mock_function::MockFunction<std::tuple<std::string>, std::string>& mock_string_sized() { return mock_string_sized_; }
    mock_function::MockFunction<std::tuple<std::string>, std::string>& mock_string_sized2() { return mock_string_sized2_; }

protected:
    mock_function::MockFunction<std::tuple<this_is_astruct_t>, this_is_astruct_t> mock_struct_;
    mock_function::MockFunction<std::tuple<this_is_aunion_t>, this_is_aunion_t> mock_union_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_sized_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_sized2_;

private:
    const other_types_async_reference_protocol_t proto_;
};

// This class mocks a device by providing a interface_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockInterface interface;
//
// /* Set some expectations on the device by calling interface.Expect... methods. */
//
// SomeDriver dut(interface.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(interface.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockInterface : ddk::InterfaceProtocol<MockInterface> {
public:
    MockInterface() : proto_{&interface_protocol_ops_, this} {}

    virtual ~MockInterface() {}

    const interface_protocol_t* GetProto() const { return &proto_; }

    virtual MockInterface& ExpectValue(other_types_protocol_t intf, other_types_protocol_t out_intf) {
        mock_value_.ExpectCall({out_intf}, intf);
        return *this;
    }

    virtual MockInterface& ExpectReference(other_types_protocol_t intf, other_types_protocol_t out_intf) {
        mock_reference_.ExpectCall({out_intf}, intf);
        return *this;
    }

    virtual MockInterface& ExpectAsync(other_types_protocol_t intf, other_types_protocol_t out_intf) {
        mock_async_.ExpectCall({out_intf}, intf);
        return *this;
    }

    virtual MockInterface& ExpectAsyncRefernce(other_types_protocol_t intf, other_types_protocol_t out_intf) {
        mock_async_refernce_.ExpectCall({out_intf}, intf);
        return *this;
    }

    void VerifyAndClear() {
        mock_value_.VerifyAndClear();
        mock_reference_.VerifyAndClear();
        mock_async_.VerifyAndClear();
        mock_async_refernce_.VerifyAndClear();
    }

    virtual void InterfaceValue(const other_types_protocol_t* intf, other_types_protocol_t* out_intf) {
        std::tuple<other_types_protocol_t> ret = mock_value_.Call(*intf);
        *out_intf = std::get<0>(ret);
    }

    virtual void InterfaceReference(const other_types_protocol_t* intf, other_types_protocol_t** out_intf) {
        std::tuple<other_types_protocol_t> ret = mock_reference_.Call(*intf);
        *out_intf = std::get<0>(ret);
    }

    virtual void InterfaceAsync(const other_types_protocol_t* intf, interface_async_callback callback, void* cookie) {
        std::tuple<other_types_protocol_t> ret = mock_async_.Call(*intf);
        callback(cookie, std::get<0>(ret));
    }

    virtual void InterfaceAsyncRefernce(const other_types_protocol_t* intf, interface_async_refernce_callback callback, void* cookie) {
        std::tuple<other_types_protocol_t> ret = mock_async_refernce_.Call(*intf);
        callback(cookie, std::get<0>(ret));
    }

    mock_function::MockFunction<std::tuple<other_types_protocol_t>, other_types_protocol_t>& mock_value() { return mock_value_; }
    mock_function::MockFunction<std::tuple<other_types_protocol_t>, other_types_protocol_t>& mock_reference() { return mock_reference_; }
    mock_function::MockFunction<std::tuple<other_types_protocol_t>, other_types_protocol_t>& mock_async() { return mock_async_; }
    mock_function::MockFunction<std::tuple<other_types_protocol_t>, other_types_protocol_t>& mock_async_refernce() { return mock_async_refernce_; }

protected:
    mock_function::MockFunction<std::tuple<other_types_protocol_t>, other_types_protocol_t> mock_value_;
    mock_function::MockFunction<std::tuple<other_types_protocol_t>, other_types_protocol_t> mock_reference_;
    mock_function::MockFunction<std::tuple<other_types_protocol_t>, other_types_protocol_t> mock_async_;
    mock_function::MockFunction<std::tuple<other_types_protocol_t>, other_types_protocol_t> mock_async_refernce_;

private:
    const interface_protocol_t proto_;
};

} // namespace ddk
