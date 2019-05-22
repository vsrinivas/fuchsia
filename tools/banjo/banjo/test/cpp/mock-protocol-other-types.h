// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocol.other.types banjo file

#pragma once

#include <string.h>

#include <string>
#include <tuple>

#include <banjo/examples/protocol/other/types.h>
#include <lib/mock-function/mock-function.h>

namespace ddk {

class MockOtherTypes : ddk::OtherTypesProtocol<MockOtherTypes> {
public:
    MockOtherTypes() : proto_{&other_types_protocol_ops_, this} {}

    const other_types_protocol_t* GetProto() const { return &proto_; }

    MockOtherTypes& ExpectStruct(this_is_astruct_t s, this_is_astruct_t out_s) {
        mock_struct_.ExpectCall({out_s}, s);
        return *this;
    }

    MockOtherTypes& ExpectUnion(this_is_aunion_t u, this_is_aunion_t out_u) {
        mock_union_.ExpectCall({out_u}, u);
        return *this;
    }

    MockOtherTypes& ExpectEnum(this_is_an_enum_t out_e, this_is_an_enum_t e) {
        mock_enum_.ExpectCall({out_e}, e);
        return *this;
    }

    MockOtherTypes& ExpectString(std::string s, std::string s) {
        mock_string_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    MockOtherTypes& ExpectStringSized(std::string s, std::string s) {
        mock_string_sized_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    MockOtherTypes& ExpectStringSized2(std::string s, std::string s) {
        mock_string_sized2_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    void VerifyAndClear() {
        mock_struct_.VerifyAndClear();
        mock_union_.VerifyAndClear();
        mock_enum_.VerifyAndClear();
        mock_string_.VerifyAndClear();
        mock_string_sized_.VerifyAndClear();
        mock_string_sized2_.VerifyAndClear();
    }

    void OtherTypesStruct(const this_is_astruct_t* s, this_is_astruct_t* out_s) {
        std::tuple<this_is_astruct_t> ret = mock_struct_.Call(s);
        *out_s = std::get<0>(ret);
    }

    void OtherTypesUnion(const this_is_aunion_t* u, this_is_aunion_t* out_u) {
        std::tuple<this_is_aunion_t> ret = mock_union_.Call(u);
        *out_u = std::get<0>(ret);
    }

    this_is_an_enum_t OtherTypesEnum(this_is_an_enum_t e) {
        std::tuple<this_is_an_enum_t> ret = mock_enum_.Call(e);
        return std::get<0>(ret);
    }

    void OtherTypesString(const char* s, char* out_s, size_t s_capacity) {
        std::tuple<std::string> ret = mock_string_.Call(std::string(s));
        strncpy(out_s, std::get<0>(ret).c_str(), s_capacity));
    }

    void OtherTypesStringSized(const char* s, char* out_s, size_t s_capacity) {
        std::tuple<std::string> ret = mock_string_sized_.Call(std::string(s));
        strncpy(out_s, std::get<0>(ret).c_str(), s_capacity));
    }

    void OtherTypesStringSized2(const char* s, char* out_s, size_t s_capacity) {
        std::tuple<std::string> ret = mock_string_sized2_.Call(std::string(s));
        strncpy(out_s, std::get<0>(ret).c_str(), s_capacity));
    }

private:
    const other_types_protocol_t proto_;
    mock_function::MockFunction<std::tuple<this_is_astruct_t>, this_is_astruct_t> mock_struct_;
    mock_function::MockFunction<std::tuple<this_is_aunion_t>, this_is_aunion_t> mock_union_;
    mock_function::MockFunction<std::tuple<this_is_an_enum_t>, this_is_an_enum_t> mock_enum_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_sized_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_sized2_;
};

class MockOtherTypesAsync : ddk::OtherTypesAsyncProtocol<MockOtherTypesAsync> {
public:
    MockOtherTypesAsync() : proto_{&other_types_async_protocol_ops_, this} {}

    const other_types_async_protocol_t* GetProto() const { return &proto_; }

    MockOtherTypesAsync& ExpectStruct(this_is_astruct_t s, this_is_astruct_t out_s) {
        mock_struct_.ExpectCall({out_s}, s);
        return *this;
    }

    MockOtherTypesAsync& ExpectUnion(this_is_aunion_t u, this_is_aunion_t out_u) {
        mock_union_.ExpectCall({out_u}, u);
        return *this;
    }

    MockOtherTypesAsync& ExpectEnum(this_is_an_enum_t e, this_is_an_enum_t out_e) {
        mock_enum_.ExpectCall({out_e}, e);
        return *this;
    }

    MockOtherTypesAsync& ExpectString(std::string s, std::string s) {
        mock_string_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    MockOtherTypesAsync& ExpectStringSized(std::string s, std::string s) {
        mock_string_sized_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    MockOtherTypesAsync& ExpectStringSized2(std::string s, std::string s) {
        mock_string_sized2_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    void VerifyAndClear() {
        mock_struct_.VerifyAndClear();
        mock_union_.VerifyAndClear();
        mock_enum_.VerifyAndClear();
        mock_string_.VerifyAndClear();
        mock_string_sized_.VerifyAndClear();
        mock_string_sized2_.VerifyAndClear();
    }

    void OtherTypesAsyncStruct(const this_is_astruct_t* s, other_types_async_struct_callback callback, void* cookie) {
        std::tuple<this_is_astruct_t> ret = mock_struct_.Call(s);
        callback(cookie, std::get<0>(ret));
    }

    void OtherTypesAsyncUnion(const this_is_aunion_t* u, other_types_async_union_callback callback, void* cookie) {
        std::tuple<this_is_aunion_t> ret = mock_union_.Call(u);
        callback(cookie, std::get<0>(ret));
    }

    void OtherTypesAsyncEnum(this_is_an_enum_t e, other_types_async_enum_callback callback, void* cookie) {
        std::tuple<this_is_an_enum_t> ret = mock_enum_.Call(e);
        callback(cookie, std::get<0>(ret));
    }

    void OtherTypesAsyncString(const char* s, other_types_async_string_callback callback, void* cookie) {
        std::tuple<std::string> ret = mock_string_.Call(std::string(s));
        callback(cookie, std::get<0>(ret).c_str());
    }

    void OtherTypesAsyncStringSized(const char* s, other_types_async_string_sized_callback callback, void* cookie) {
        std::tuple<std::string> ret = mock_string_sized_.Call(std::string(s));
        callback(cookie, std::get<0>(ret).c_str());
    }

    void OtherTypesAsyncStringSized2(const char* s, other_types_async_string_sized2_callback callback, void* cookie) {
        std::tuple<std::string> ret = mock_string_sized2_.Call(std::string(s));
        callback(cookie, std::get<0>(ret).c_str());
    }

private:
    const other_types_async_protocol_t proto_;
    mock_function::MockFunction<std::tuple<this_is_astruct_t>, this_is_astruct_t> mock_struct_;
    mock_function::MockFunction<std::tuple<this_is_aunion_t>, this_is_aunion_t> mock_union_;
    mock_function::MockFunction<std::tuple<this_is_an_enum_t>, this_is_an_enum_t> mock_enum_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_sized_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_sized2_;
};

class MockOtherTypesReference : ddk::OtherTypesReferenceProtocol<MockOtherTypesReference> {
public:
    MockOtherTypesReference() : proto_{&other_types_reference_protocol_ops_, this} {}

    const other_types_reference_protocol_t* GetProto() const { return &proto_; }

    MockOtherTypesReference& ExpectStruct(this_is_astruct_t s, this_is_astruct_t out_s) {
        mock_struct_.ExpectCall({out_s}, s);
        return *this;
    }

    MockOtherTypesReference& ExpectUnion(this_is_aunion_t u, this_is_aunion_t out_u) {
        mock_union_.ExpectCall({out_u}, u);
        return *this;
    }

    MockOtherTypesReference& ExpectString(std::string s, std::string s) {
        mock_string_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    MockOtherTypesReference& ExpectStringSized(std::string s, std::string s) {
        mock_string_sized_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    MockOtherTypesReference& ExpectStringSized2(std::string s, std::string s) {
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

    void OtherTypesReferenceStruct(this_is_astruct_t* s, this_is_astruct_t** out_s) {
        std::tuple<this_is_astruct_t> ret = mock_struct_.Call(s);
        *out_s = std::get<0>(ret);
    }

    void OtherTypesReferenceUnion(this_is_aunion_t* u, this_is_aunion_t** out_u) {
        std::tuple<this_is_aunion_t> ret = mock_union_.Call(u);
        *out_u = std::get<0>(ret);
    }

    void OtherTypesReferenceString(const char* s, char* out_s, size_t s_capacity) {
        std::tuple<std::string> ret = mock_string_.Call(std::string(s));
        strncpy(out_s, std::get<0>(ret).c_str(), s_capacity));
    }

    void OtherTypesReferenceStringSized(const char* s, char* out_s, size_t s_capacity) {
        std::tuple<std::string> ret = mock_string_sized_.Call(std::string(s));
        strncpy(out_s, std::get<0>(ret).c_str(), s_capacity));
    }

    void OtherTypesReferenceStringSized2(const char* s, char* out_s, size_t s_capacity) {
        std::tuple<std::string> ret = mock_string_sized2_.Call(std::string(s));
        strncpy(out_s, std::get<0>(ret).c_str(), s_capacity));
    }

private:
    const other_types_reference_protocol_t proto_;
    mock_function::MockFunction<std::tuple<this_is_astruct_t>, this_is_astruct_t> mock_struct_;
    mock_function::MockFunction<std::tuple<this_is_aunion_t>, this_is_aunion_t> mock_union_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_sized_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_sized2_;
};

class MockOtherTypesAsyncReference : ddk::OtherTypesAsyncReferenceProtocol<MockOtherTypesAsyncReference> {
public:
    MockOtherTypesAsyncReference() : proto_{&other_types_async_reference_protocol_ops_, this} {}

    const other_types_async_reference_protocol_t* GetProto() const { return &proto_; }

    MockOtherTypesAsyncReference& ExpectStruct(this_is_astruct_t s, this_is_astruct_t out_s) {
        mock_struct_.ExpectCall({out_s}, s);
        return *this;
    }

    MockOtherTypesAsyncReference& ExpectUnion(this_is_aunion_t u, this_is_aunion_t out_u) {
        mock_union_.ExpectCall({out_u}, u);
        return *this;
    }

    MockOtherTypesAsyncReference& ExpectString(std::string s, std::string s) {
        mock_string_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    MockOtherTypesAsyncReference& ExpectStringSized(std::string s, std::string s) {
        mock_string_sized_.ExpectCall({std::move(out_s)}, std::move(s));
        return *this;
    }

    MockOtherTypesAsyncReference& ExpectStringSized2(std::string s, std::string s) {
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

    void OtherTypesAsyncReferenceStruct(this_is_astruct_t* s, other_types_async_reference_struct_callback callback, void* cookie) {
        std::tuple<this_is_astruct_t> ret = mock_struct_.Call(s);
        callback(cookie, std::get<0>(ret));
    }

    void OtherTypesAsyncReferenceUnion(this_is_aunion_t* u, other_types_async_reference_union_callback callback, void* cookie) {
        std::tuple<this_is_aunion_t> ret = mock_union_.Call(u);
        callback(cookie, std::get<0>(ret));
    }

    void OtherTypesAsyncReferenceString(const char* s, other_types_async_reference_string_callback callback, void* cookie) {
        std::tuple<std::string> ret = mock_string_.Call(std::string(s));
        callback(cookie, std::get<0>(ret).c_str());
    }

    void OtherTypesAsyncReferenceStringSized(const char* s, other_types_async_reference_string_sized_callback callback, void* cookie) {
        std::tuple<std::string> ret = mock_string_sized_.Call(std::string(s));
        callback(cookie, std::get<0>(ret).c_str());
    }

    void OtherTypesAsyncReferenceStringSized2(const char* s, other_types_async_reference_string_sized2_callback callback, void* cookie) {
        std::tuple<std::string> ret = mock_string_sized2_.Call(std::string(s));
        callback(cookie, std::get<0>(ret).c_str());
    }

private:
    const other_types_async_reference_protocol_t proto_;
    mock_function::MockFunction<std::tuple<this_is_astruct_t>, this_is_astruct_t> mock_struct_;
    mock_function::MockFunction<std::tuple<this_is_aunion_t>, this_is_aunion_t> mock_union_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_sized_;
    mock_function::MockFunction<std::tuple<std::string>, std::string> mock_string_sized2_;
};

class MockInterface : ddk::InterfaceProtocol<MockInterface> {
public:
    MockInterface() : proto_{&interface_protocol_ops_, this} {}

    const interface_protocol_t* GetProto() const { return &proto_; }

    MockInterface& ExpectValue(other_types_protocol_t intf, other_types_protocol_t out_intf) {
        mock_value_.ExpectCall({out_intf}, intf);
        return *this;
    }

    MockInterface& ExpectReference(other_types_protocol_t intf, other_types_protocol_t out_intf) {
        mock_reference_.ExpectCall({out_intf}, intf);
        return *this;
    }

    MockInterface& ExpectAsync(other_types_protocol_t intf, other_types_protocol_t out_intf) {
        mock_async_.ExpectCall({out_intf}, intf);
        return *this;
    }

    MockInterface& ExpectAsyncRefernce(other_types_protocol_t intf, other_types_protocol_t out_intf) {
        mock_async_refernce_.ExpectCall({out_intf}, intf);
        return *this;
    }

    void VerifyAndClear() {
        mock_value_.VerifyAndClear();
        mock_reference_.VerifyAndClear();
        mock_async_.VerifyAndClear();
        mock_async_refernce_.VerifyAndClear();
    }

    void InterfaceValue(void* intf_ctx, other_types_protocol_ops_t* intf_ops, other_types_protocol_t* out_intf) {
        std::tuple<other_types_protocol_t> ret = mock_value_.Call(intf);
        *out_intf = std::get<0>(ret);
    }

    void InterfaceReference(void* intf_ctx, other_types_protocol_ops_t* intf_ops, other_types_protocol_t** out_intf) {
        std::tuple<other_types_protocol_t> ret = mock_reference_.Call(intf);
        *out_intf = std::get<0>(ret);
    }

    void InterfaceAsync(void* intf_ctx, other_types_protocol_ops_t* intf_ops, interface_async_callback callback, void* cookie) {
        std::tuple<other_types_protocol_t> ret = mock_async_.Call(intf);
        callback(cookie, std::get<0>(ret));
    }

    void InterfaceAsyncRefernce(void* intf_ctx, other_types_protocol_ops_t* intf_ops, interface_async_refernce_callback callback, void* cookie) {
        std::tuple<other_types_protocol_t> ret = mock_async_refernce_.Call(intf);
        callback(cookie, std::get<0>(ret));
    }

private:
    const interface_protocol_t proto_;
    mock_function::MockFunction<std::tuple<other_types_protocol_t>, other_types_protocol_t> mock_value_;
    mock_function::MockFunction<std::tuple<other_types_protocol_t>, other_types_protocol_t> mock_reference_;
    mock_function::MockFunction<std::tuple<other_types_protocol_t>, other_types_protocol_t> mock_async_;
    mock_function::MockFunction<std::tuple<other_types_protocol_t>, other_types_protocol_t> mock_async_refernce_;
};

} // namespace ddk
