// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocolarray banjo file

#pragma once

#include <tuple>

#include <banjo/examples/protocolarray/cpp/banjo.h>
#include <lib/mock-function/mock-function.h>

namespace ddk {

// This class mocks a device by providing a arrayof_arrays_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockArrayofArrays arrayof_arrays;
//
// /* Set some expectations on the device by calling arrayof_arrays.Expect... methods. */
//
// SomeDriver dut(arrayof_arrays.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(arrayof_arrays.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockArrayofArrays : ddk::ArrayofArraysProtocol<MockArrayofArrays> {
public:
    MockArrayofArrays() : proto_{&arrayof_arrays_protocol_ops_, this} {}

    virtual ~MockArrayofArrays() {}

    const arrayof_arrays_protocol_t* GetProto() const { return &proto_; }

    virtual MockArrayofArrays& ExpectBool(bool b, bool out_b) {
        mock_bool_.ExpectCall({out_b}, b);
        return *this;
    }

    virtual MockArrayofArrays& ExpectInt8(int8_t i8, int8_t out_i8) {
        mock_int8_.ExpectCall({out_i8}, i8);
        return *this;
    }

    virtual MockArrayofArrays& ExpectInt16(int16_t i16, int16_t out_i16) {
        mock_int16_.ExpectCall({out_i16}, i16);
        return *this;
    }

    virtual MockArrayofArrays& ExpectInt32(int32_t i32, int32_t out_i32) {
        mock_int32_.ExpectCall({out_i32}, i32);
        return *this;
    }

    virtual MockArrayofArrays& ExpectInt64(int64_t i64, int64_t out_i64) {
        mock_int64_.ExpectCall({out_i64}, i64);
        return *this;
    }

    virtual MockArrayofArrays& ExpectUint8(uint8_t u8, uint8_t out_u8) {
        mock_uint8_.ExpectCall({out_u8}, u8);
        return *this;
    }

    virtual MockArrayofArrays& ExpectUint16(uint16_t u16, uint16_t out_u16) {
        mock_uint16_.ExpectCall({out_u16}, u16);
        return *this;
    }

    virtual MockArrayofArrays& ExpectUint32(uint32_t u32, uint32_t out_u32) {
        mock_uint32_.ExpectCall({out_u32}, u32);
        return *this;
    }

    virtual MockArrayofArrays& ExpectUint64(uint64_t u64, uint64_t out_u64) {
        mock_uint64_.ExpectCall({out_u64}, u64);
        return *this;
    }

    virtual MockArrayofArrays& ExpectFloat32(float f32, float out_f32) {
        mock_float32_.ExpectCall({out_f32}, f32);
        return *this;
    }

    virtual MockArrayofArrays& ExpectFloat64(double u64, double out_f64) {
        mock_float64_.ExpectCall({out_f64}, u64);
        return *this;
    }

    virtual MockArrayofArrays& ExpectHandle(zx::handle u64, zx::handle out_f64) {
        mock_handle_.ExpectCall({out_f64}, u64);
        return *this;
    }

    void VerifyAndClear() {
        mock_bool_.VerifyAndClear();
        mock_int8_.VerifyAndClear();
        mock_int16_.VerifyAndClear();
        mock_int32_.VerifyAndClear();
        mock_int64_.VerifyAndClear();
        mock_uint8_.VerifyAndClear();
        mock_uint16_.VerifyAndClear();
        mock_uint32_.VerifyAndClear();
        mock_uint64_.VerifyAndClear();
        mock_float32_.VerifyAndClear();
        mock_float64_.VerifyAndClear();
        mock_handle_.VerifyAndClear();
    }

    virtual void ArrayofArraysBool(const bool b[32][4], bool out_b[32][4]) {
        std::tuple<bool> ret = mock_bool_.Call(b);
        *out_b = std::get<0>(ret);
    }

    virtual void ArrayofArraysInt8(const int8_t i8[32][4], int8_t out_i8[32][4]) {
        std::tuple<int8_t> ret = mock_int8_.Call(i8);
        *out_i8 = std::get<0>(ret);
    }

    virtual void ArrayofArraysInt16(const int16_t i16[32][4], int16_t out_i16[32][4]) {
        std::tuple<int16_t> ret = mock_int16_.Call(i16);
        *out_i16 = std::get<0>(ret);
    }

    virtual void ArrayofArraysInt32(const int32_t i32[32][4], int32_t out_i32[32][4]) {
        std::tuple<int32_t> ret = mock_int32_.Call(i32);
        *out_i32 = std::get<0>(ret);
    }

    virtual void ArrayofArraysInt64(const int64_t i64[32][4], int64_t out_i64[32][4]) {
        std::tuple<int64_t> ret = mock_int64_.Call(i64);
        *out_i64 = std::get<0>(ret);
    }

    virtual void ArrayofArraysUint8(const uint8_t u8[32][4], uint8_t out_u8[32][4]) {
        std::tuple<uint8_t> ret = mock_uint8_.Call(u8);
        *out_u8 = std::get<0>(ret);
    }

    virtual void ArrayofArraysUint16(const uint16_t u16[32][4], uint16_t out_u16[32][4]) {
        std::tuple<uint16_t> ret = mock_uint16_.Call(u16);
        *out_u16 = std::get<0>(ret);
    }

    virtual void ArrayofArraysUint32(const uint32_t u32[32][4], uint32_t out_u32[32][4]) {
        std::tuple<uint32_t> ret = mock_uint32_.Call(u32);
        *out_u32 = std::get<0>(ret);
    }

    virtual void ArrayofArraysUint64(const uint64_t u64[32][4], uint64_t out_u64[32][4]) {
        std::tuple<uint64_t> ret = mock_uint64_.Call(u64);
        *out_u64 = std::get<0>(ret);
    }

    virtual void ArrayofArraysFloat32(const float f32[32][4], float out_f32[32][4]) {
        std::tuple<float> ret = mock_float32_.Call(f32);
        *out_f32 = std::get<0>(ret);
    }

    virtual void ArrayofArraysFloat64(const double u64[32][4], double out_f64[32][4]) {
        std::tuple<double> ret = mock_float64_.Call(u64);
        *out_f64 = std::get<0>(ret);
    }

    virtual void ArrayofArraysHandle(const zx::handle u64[32][4], zx::handle out_f64[32][4]) {
        std::tuple<zx::handle> ret = mock_handle_.Call(u64);
        *out_f64 = std::get<0>(ret);
    }

    mock_function::MockFunction<std::tuple<bool>, bool>& mock_bool() { return mock_bool_; }
    mock_function::MockFunction<std::tuple<int8_t>, int8_t>& mock_int8() { return mock_int8_; }
    mock_function::MockFunction<std::tuple<int16_t>, int16_t>& mock_int16() { return mock_int16_; }
    mock_function::MockFunction<std::tuple<int32_t>, int32_t>& mock_int32() { return mock_int32_; }
    mock_function::MockFunction<std::tuple<int64_t>, int64_t>& mock_int64() { return mock_int64_; }
    mock_function::MockFunction<std::tuple<uint8_t>, uint8_t>& mock_uint8() { return mock_uint8_; }
    mock_function::MockFunction<std::tuple<uint16_t>, uint16_t>& mock_uint16() { return mock_uint16_; }
    mock_function::MockFunction<std::tuple<uint32_t>, uint32_t>& mock_uint32() { return mock_uint32_; }
    mock_function::MockFunction<std::tuple<uint64_t>, uint64_t>& mock_uint64() { return mock_uint64_; }
    mock_function::MockFunction<std::tuple<float>, float>& mock_float32() { return mock_float32_; }
    mock_function::MockFunction<std::tuple<double>, double>& mock_float64() { return mock_float64_; }
    mock_function::MockFunction<std::tuple<zx::handle>, zx::handle>& mock_handle() { return mock_handle_; }

protected:
    mock_function::MockFunction<std::tuple<bool>, bool> mock_bool_;
    mock_function::MockFunction<std::tuple<int8_t>, int8_t> mock_int8_;
    mock_function::MockFunction<std::tuple<int16_t>, int16_t> mock_int16_;
    mock_function::MockFunction<std::tuple<int32_t>, int32_t> mock_int32_;
    mock_function::MockFunction<std::tuple<int64_t>, int64_t> mock_int64_;
    mock_function::MockFunction<std::tuple<uint8_t>, uint8_t> mock_uint8_;
    mock_function::MockFunction<std::tuple<uint16_t>, uint16_t> mock_uint16_;
    mock_function::MockFunction<std::tuple<uint32_t>, uint32_t> mock_uint32_;
    mock_function::MockFunction<std::tuple<uint64_t>, uint64_t> mock_uint64_;
    mock_function::MockFunction<std::tuple<float>, float> mock_float32_;
    mock_function::MockFunction<std::tuple<double>, double> mock_float64_;
    mock_function::MockFunction<std::tuple<zx::handle>, zx::handle> mock_handle_;

private:
    const arrayof_arrays_protocol_t proto_;
};

// This class mocks a device by providing a array_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockArray array;
//
// /* Set some expectations on the device by calling array.Expect... methods. */
//
// SomeDriver dut(array.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(array.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockArray : ddk::ArrayProtocol<MockArray> {
public:
    MockArray() : proto_{&array_protocol_ops_, this} {}

    virtual ~MockArray() {}

    const array_protocol_t* GetProto() const { return &proto_; }

    virtual MockArray& ExpectBool(bool b, bool out_b) {
        mock_bool_.ExpectCall({out_b}, b);
        return *this;
    }

    virtual MockArray& ExpectInt8(int8_t i8, int8_t out_i8) {
        mock_int8_.ExpectCall({out_i8}, i8);
        return *this;
    }

    virtual MockArray& ExpectInt16(int16_t i16, int16_t out_i16) {
        mock_int16_.ExpectCall({out_i16}, i16);
        return *this;
    }

    virtual MockArray& ExpectInt32(int32_t i32, int32_t out_i32) {
        mock_int32_.ExpectCall({out_i32}, i32);
        return *this;
    }

    virtual MockArray& ExpectInt64(int64_t i64, int64_t out_i64) {
        mock_int64_.ExpectCall({out_i64}, i64);
        return *this;
    }

    virtual MockArray& ExpectUint8(uint8_t u8, uint8_t out_u8) {
        mock_uint8_.ExpectCall({out_u8}, u8);
        return *this;
    }

    virtual MockArray& ExpectUint16(uint16_t u16, uint16_t out_u16) {
        mock_uint16_.ExpectCall({out_u16}, u16);
        return *this;
    }

    virtual MockArray& ExpectUint32(uint32_t u32, uint32_t out_u32) {
        mock_uint32_.ExpectCall({out_u32}, u32);
        return *this;
    }

    virtual MockArray& ExpectUint64(uint64_t u64, uint64_t out_u64) {
        mock_uint64_.ExpectCall({out_u64}, u64);
        return *this;
    }

    virtual MockArray& ExpectFloat32(float f32, float out_f32) {
        mock_float32_.ExpectCall({out_f32}, f32);
        return *this;
    }

    virtual MockArray& ExpectFloat64(double u64, double out_f64) {
        mock_float64_.ExpectCall({out_f64}, u64);
        return *this;
    }

    virtual MockArray& ExpectHandle(zx::handle u64, zx::handle out_f64) {
        mock_handle_.ExpectCall({out_f64}, u64);
        return *this;
    }

    void VerifyAndClear() {
        mock_bool_.VerifyAndClear();
        mock_int8_.VerifyAndClear();
        mock_int16_.VerifyAndClear();
        mock_int32_.VerifyAndClear();
        mock_int64_.VerifyAndClear();
        mock_uint8_.VerifyAndClear();
        mock_uint16_.VerifyAndClear();
        mock_uint32_.VerifyAndClear();
        mock_uint64_.VerifyAndClear();
        mock_float32_.VerifyAndClear();
        mock_float64_.VerifyAndClear();
        mock_handle_.VerifyAndClear();
    }

    virtual void ArrayBool(const bool b[1], bool out_b[1]) {
        std::tuple<bool> ret = mock_bool_.Call(b);
        *out_b = std::get<0>(ret);
    }

    virtual void ArrayInt8(const int8_t i8[1], int8_t out_i8[1]) {
        std::tuple<int8_t> ret = mock_int8_.Call(i8);
        *out_i8 = std::get<0>(ret);
    }

    virtual void ArrayInt16(const int16_t i16[1], int16_t out_i16[1]) {
        std::tuple<int16_t> ret = mock_int16_.Call(i16);
        *out_i16 = std::get<0>(ret);
    }

    virtual void ArrayInt32(const int32_t i32[1], int32_t out_i32[1]) {
        std::tuple<int32_t> ret = mock_int32_.Call(i32);
        *out_i32 = std::get<0>(ret);
    }

    virtual void ArrayInt64(const int64_t i64[1], int64_t out_i64[1]) {
        std::tuple<int64_t> ret = mock_int64_.Call(i64);
        *out_i64 = std::get<0>(ret);
    }

    virtual void ArrayUint8(const uint8_t u8[1], uint8_t out_u8[1]) {
        std::tuple<uint8_t> ret = mock_uint8_.Call(u8);
        *out_u8 = std::get<0>(ret);
    }

    virtual void ArrayUint16(const uint16_t u16[1], uint16_t out_u16[1]) {
        std::tuple<uint16_t> ret = mock_uint16_.Call(u16);
        *out_u16 = std::get<0>(ret);
    }

    virtual void ArrayUint32(const uint32_t u32[1], uint32_t out_u32[1]) {
        std::tuple<uint32_t> ret = mock_uint32_.Call(u32);
        *out_u32 = std::get<0>(ret);
    }

    virtual void ArrayUint64(const uint64_t u64[1], uint64_t out_u64[1]) {
        std::tuple<uint64_t> ret = mock_uint64_.Call(u64);
        *out_u64 = std::get<0>(ret);
    }

    virtual void ArrayFloat32(const float f32[1], float out_f32[1]) {
        std::tuple<float> ret = mock_float32_.Call(f32);
        *out_f32 = std::get<0>(ret);
    }

    virtual void ArrayFloat64(const double u64[1], double out_f64[1]) {
        std::tuple<double> ret = mock_float64_.Call(u64);
        *out_f64 = std::get<0>(ret);
    }

    virtual void ArrayHandle(const zx::handle u64[1], zx::handle out_f64[1]) {
        std::tuple<zx::handle> ret = mock_handle_.Call(u64);
        *out_f64 = std::get<0>(ret);
    }

    mock_function::MockFunction<std::tuple<bool>, bool>& mock_bool() { return mock_bool_; }
    mock_function::MockFunction<std::tuple<int8_t>, int8_t>& mock_int8() { return mock_int8_; }
    mock_function::MockFunction<std::tuple<int16_t>, int16_t>& mock_int16() { return mock_int16_; }
    mock_function::MockFunction<std::tuple<int32_t>, int32_t>& mock_int32() { return mock_int32_; }
    mock_function::MockFunction<std::tuple<int64_t>, int64_t>& mock_int64() { return mock_int64_; }
    mock_function::MockFunction<std::tuple<uint8_t>, uint8_t>& mock_uint8() { return mock_uint8_; }
    mock_function::MockFunction<std::tuple<uint16_t>, uint16_t>& mock_uint16() { return mock_uint16_; }
    mock_function::MockFunction<std::tuple<uint32_t>, uint32_t>& mock_uint32() { return mock_uint32_; }
    mock_function::MockFunction<std::tuple<uint64_t>, uint64_t>& mock_uint64() { return mock_uint64_; }
    mock_function::MockFunction<std::tuple<float>, float>& mock_float32() { return mock_float32_; }
    mock_function::MockFunction<std::tuple<double>, double>& mock_float64() { return mock_float64_; }
    mock_function::MockFunction<std::tuple<zx::handle>, zx::handle>& mock_handle() { return mock_handle_; }

protected:
    mock_function::MockFunction<std::tuple<bool>, bool> mock_bool_;
    mock_function::MockFunction<std::tuple<int8_t>, int8_t> mock_int8_;
    mock_function::MockFunction<std::tuple<int16_t>, int16_t> mock_int16_;
    mock_function::MockFunction<std::tuple<int32_t>, int32_t> mock_int32_;
    mock_function::MockFunction<std::tuple<int64_t>, int64_t> mock_int64_;
    mock_function::MockFunction<std::tuple<uint8_t>, uint8_t> mock_uint8_;
    mock_function::MockFunction<std::tuple<uint16_t>, uint16_t> mock_uint16_;
    mock_function::MockFunction<std::tuple<uint32_t>, uint32_t> mock_uint32_;
    mock_function::MockFunction<std::tuple<uint64_t>, uint64_t> mock_uint64_;
    mock_function::MockFunction<std::tuple<float>, float> mock_float32_;
    mock_function::MockFunction<std::tuple<double>, double> mock_float64_;
    mock_function::MockFunction<std::tuple<zx::handle>, zx::handle> mock_handle_;

private:
    const array_protocol_t proto_;
};

// This class mocks a device by providing a array2_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockArray2 array2;
//
// /* Set some expectations on the device by calling array2.Expect... methods. */
//
// SomeDriver dut(array2.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(array2.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockArray2 : ddk::Array2Protocol<MockArray2> {
public:
    MockArray2() : proto_{&array2_protocol_ops_, this} {}

    virtual ~MockArray2() {}

    const array2_protocol_t* GetProto() const { return &proto_; }

    virtual MockArray2& ExpectBool(bool b, bool out_b) {
        mock_bool_.ExpectCall({out_b}, b);
        return *this;
    }

    virtual MockArray2& ExpectInt8(int8_t i8, int8_t out_i8) {
        mock_int8_.ExpectCall({out_i8}, i8);
        return *this;
    }

    virtual MockArray2& ExpectInt16(int16_t i16, int16_t out_i16) {
        mock_int16_.ExpectCall({out_i16}, i16);
        return *this;
    }

    virtual MockArray2& ExpectInt32(int32_t i32, int32_t out_i32) {
        mock_int32_.ExpectCall({out_i32}, i32);
        return *this;
    }

    virtual MockArray2& ExpectInt64(int64_t i64, int64_t out_i64) {
        mock_int64_.ExpectCall({out_i64}, i64);
        return *this;
    }

    virtual MockArray2& ExpectUint8(uint8_t u8, uint8_t out_u8) {
        mock_uint8_.ExpectCall({out_u8}, u8);
        return *this;
    }

    virtual MockArray2& ExpectUint16(uint16_t u16, uint16_t out_u16) {
        mock_uint16_.ExpectCall({out_u16}, u16);
        return *this;
    }

    virtual MockArray2& ExpectUint32(uint32_t u32, uint32_t out_u32) {
        mock_uint32_.ExpectCall({out_u32}, u32);
        return *this;
    }

    virtual MockArray2& ExpectUint64(uint64_t u64, uint64_t out_u64) {
        mock_uint64_.ExpectCall({out_u64}, u64);
        return *this;
    }

    virtual MockArray2& ExpectFloat32(float f32, float out_f32) {
        mock_float32_.ExpectCall({out_f32}, f32);
        return *this;
    }

    virtual MockArray2& ExpectFloat64(double u64, double out_f64) {
        mock_float64_.ExpectCall({out_f64}, u64);
        return *this;
    }

    virtual MockArray2& ExpectHandle(zx::handle u64, zx::handle out_f64) {
        mock_handle_.ExpectCall({out_f64}, u64);
        return *this;
    }

    void VerifyAndClear() {
        mock_bool_.VerifyAndClear();
        mock_int8_.VerifyAndClear();
        mock_int16_.VerifyAndClear();
        mock_int32_.VerifyAndClear();
        mock_int64_.VerifyAndClear();
        mock_uint8_.VerifyAndClear();
        mock_uint16_.VerifyAndClear();
        mock_uint32_.VerifyAndClear();
        mock_uint64_.VerifyAndClear();
        mock_float32_.VerifyAndClear();
        mock_float64_.VerifyAndClear();
        mock_handle_.VerifyAndClear();
    }

    virtual void Array2Bool(const bool b[32], bool out_b[32]) {
        std::tuple<bool> ret = mock_bool_.Call(b);
        *out_b = std::get<0>(ret);
    }

    virtual void Array2Int8(const int8_t i8[32], int8_t out_i8[32]) {
        std::tuple<int8_t> ret = mock_int8_.Call(i8);
        *out_i8 = std::get<0>(ret);
    }

    virtual void Array2Int16(const int16_t i16[32], int16_t out_i16[32]) {
        std::tuple<int16_t> ret = mock_int16_.Call(i16);
        *out_i16 = std::get<0>(ret);
    }

    virtual void Array2Int32(const int32_t i32[32], int32_t out_i32[32]) {
        std::tuple<int32_t> ret = mock_int32_.Call(i32);
        *out_i32 = std::get<0>(ret);
    }

    virtual void Array2Int64(const int64_t i64[32], int64_t out_i64[32]) {
        std::tuple<int64_t> ret = mock_int64_.Call(i64);
        *out_i64 = std::get<0>(ret);
    }

    virtual void Array2Uint8(const uint8_t u8[32], uint8_t out_u8[32]) {
        std::tuple<uint8_t> ret = mock_uint8_.Call(u8);
        *out_u8 = std::get<0>(ret);
    }

    virtual void Array2Uint16(const uint16_t u16[32], uint16_t out_u16[32]) {
        std::tuple<uint16_t> ret = mock_uint16_.Call(u16);
        *out_u16 = std::get<0>(ret);
    }

    virtual void Array2Uint32(const uint32_t u32[32], uint32_t out_u32[32]) {
        std::tuple<uint32_t> ret = mock_uint32_.Call(u32);
        *out_u32 = std::get<0>(ret);
    }

    virtual void Array2Uint64(const uint64_t u64[32], uint64_t out_u64[32]) {
        std::tuple<uint64_t> ret = mock_uint64_.Call(u64);
        *out_u64 = std::get<0>(ret);
    }

    virtual void Array2Float32(const float f32[32], float out_f32[32]) {
        std::tuple<float> ret = mock_float32_.Call(f32);
        *out_f32 = std::get<0>(ret);
    }

    virtual void Array2Float64(const double u64[32], double out_f64[32]) {
        std::tuple<double> ret = mock_float64_.Call(u64);
        *out_f64 = std::get<0>(ret);
    }

    virtual void Array2Handle(const zx::handle u64[32], zx::handle out_f64[32]) {
        std::tuple<zx::handle> ret = mock_handle_.Call(u64);
        *out_f64 = std::get<0>(ret);
    }

    mock_function::MockFunction<std::tuple<bool>, bool>& mock_bool() { return mock_bool_; }
    mock_function::MockFunction<std::tuple<int8_t>, int8_t>& mock_int8() { return mock_int8_; }
    mock_function::MockFunction<std::tuple<int16_t>, int16_t>& mock_int16() { return mock_int16_; }
    mock_function::MockFunction<std::tuple<int32_t>, int32_t>& mock_int32() { return mock_int32_; }
    mock_function::MockFunction<std::tuple<int64_t>, int64_t>& mock_int64() { return mock_int64_; }
    mock_function::MockFunction<std::tuple<uint8_t>, uint8_t>& mock_uint8() { return mock_uint8_; }
    mock_function::MockFunction<std::tuple<uint16_t>, uint16_t>& mock_uint16() { return mock_uint16_; }
    mock_function::MockFunction<std::tuple<uint32_t>, uint32_t>& mock_uint32() { return mock_uint32_; }
    mock_function::MockFunction<std::tuple<uint64_t>, uint64_t>& mock_uint64() { return mock_uint64_; }
    mock_function::MockFunction<std::tuple<float>, float>& mock_float32() { return mock_float32_; }
    mock_function::MockFunction<std::tuple<double>, double>& mock_float64() { return mock_float64_; }
    mock_function::MockFunction<std::tuple<zx::handle>, zx::handle>& mock_handle() { return mock_handle_; }

protected:
    mock_function::MockFunction<std::tuple<bool>, bool> mock_bool_;
    mock_function::MockFunction<std::tuple<int8_t>, int8_t> mock_int8_;
    mock_function::MockFunction<std::tuple<int16_t>, int16_t> mock_int16_;
    mock_function::MockFunction<std::tuple<int32_t>, int32_t> mock_int32_;
    mock_function::MockFunction<std::tuple<int64_t>, int64_t> mock_int64_;
    mock_function::MockFunction<std::tuple<uint8_t>, uint8_t> mock_uint8_;
    mock_function::MockFunction<std::tuple<uint16_t>, uint16_t> mock_uint16_;
    mock_function::MockFunction<std::tuple<uint32_t>, uint32_t> mock_uint32_;
    mock_function::MockFunction<std::tuple<uint64_t>, uint64_t> mock_uint64_;
    mock_function::MockFunction<std::tuple<float>, float> mock_float32_;
    mock_function::MockFunction<std::tuple<double>, double> mock_float64_;
    mock_function::MockFunction<std::tuple<zx::handle>, zx::handle> mock_handle_;

private:
    const array2_protocol_t proto_;
};

} // namespace ddk
