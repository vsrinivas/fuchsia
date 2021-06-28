// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocolprimitive banjo file

#pragma once

#include <tuple>

#include <banjo/examples/protocolprimitive/cpp/banjo.h>
#include <lib/mock-function/mock-function.h>

namespace ddk {

// This class mocks a device by providing a synchronous_primitive_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockSynchronousPrimitive synchronous_primitive;
//
// /* Set some expectations on the device by calling synchronous_primitive.Expect... methods. */
//
// SomeDriver dut(synchronous_primitive.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(synchronous_primitive.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockSynchronousPrimitive : ddk::SynchronousPrimitiveProtocol<MockSynchronousPrimitive> {
public:
    MockSynchronousPrimitive() : proto_{&synchronous_primitive_protocol_ops_, this} {}

    virtual ~MockSynchronousPrimitive() {}

    const synchronous_primitive_protocol_t* GetProto() const { return &proto_; }

    virtual MockSynchronousPrimitive& ExpectBool(bool out_b, bool b, bool out_b_2) {
        mock_bool_.ExpectCall({out_b, out_b_2}, b);
        return *this;
    }

    virtual MockSynchronousPrimitive& ExpectInt8(int8_t out_i8, int8_t i8, int8_t out_i8_2) {
        mock_int8_.ExpectCall({out_i8, out_i8_2}, i8);
        return *this;
    }

    virtual MockSynchronousPrimitive& ExpectInt16(int16_t out_i16, int16_t i16, int16_t out_i16_2) {
        mock_int16_.ExpectCall({out_i16, out_i16_2}, i16);
        return *this;
    }

    virtual MockSynchronousPrimitive& ExpectInt32(int32_t out_i32, int32_t i32, int32_t out_i32_2) {
        mock_int32_.ExpectCall({out_i32, out_i32_2}, i32);
        return *this;
    }

    virtual MockSynchronousPrimitive& ExpectInt64(int64_t out_i64, int64_t i64, int64_t out_i64_2) {
        mock_int64_.ExpectCall({out_i64, out_i64_2}, i64);
        return *this;
    }

    virtual MockSynchronousPrimitive& ExpectUint8(uint8_t out_u8, uint8_t u8, uint8_t out_u8_2) {
        mock_uint8_.ExpectCall({out_u8, out_u8_2}, u8);
        return *this;
    }

    virtual MockSynchronousPrimitive& ExpectUint16(uint16_t out_u16, uint16_t u16, uint16_t out_u16_2) {
        mock_uint16_.ExpectCall({out_u16, out_u16_2}, u16);
        return *this;
    }

    virtual MockSynchronousPrimitive& ExpectUint32(uint32_t out_u32, uint32_t u32, uint32_t out_u32_2) {
        mock_uint32_.ExpectCall({out_u32, out_u32_2}, u32);
        return *this;
    }

    virtual MockSynchronousPrimitive& ExpectUint64(uint64_t out_u64, uint64_t u64, uint64_t out_u64_2) {
        mock_uint64_.ExpectCall({out_u64, out_u64_2}, u64);
        return *this;
    }

    virtual MockSynchronousPrimitive& ExpectFloat32(float out_f32, float f32, float out_f32_2) {
        mock_float32_.ExpectCall({out_f32, out_f32_2}, f32);
        return *this;
    }

    virtual MockSynchronousPrimitive& ExpectFloat64(double out_f64, double u64, double out_f64_2) {
        mock_float64_.ExpectCall({out_f64, out_f64_2}, u64);
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
    }

    virtual bool SynchronousPrimitiveBool(bool b, bool* out_b_2) {
        std::tuple<bool, bool> ret = mock_bool_.Call(b);
        *out_b_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual int8_t SynchronousPrimitiveInt8(int8_t i8, int8_t* out_i8_2) {
        std::tuple<int8_t, int8_t> ret = mock_int8_.Call(i8);
        *out_i8_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual int16_t SynchronousPrimitiveInt16(int16_t i16, int16_t* out_i16_2) {
        std::tuple<int16_t, int16_t> ret = mock_int16_.Call(i16);
        *out_i16_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual int32_t SynchronousPrimitiveInt32(int32_t i32, int32_t* out_i32_2) {
        std::tuple<int32_t, int32_t> ret = mock_int32_.Call(i32);
        *out_i32_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual int64_t SynchronousPrimitiveInt64(int64_t i64, int64_t* out_i64_2) {
        std::tuple<int64_t, int64_t> ret = mock_int64_.Call(i64);
        *out_i64_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual uint8_t SynchronousPrimitiveUint8(uint8_t u8, uint8_t* out_u8_2) {
        std::tuple<uint8_t, uint8_t> ret = mock_uint8_.Call(u8);
        *out_u8_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual uint16_t SynchronousPrimitiveUint16(uint16_t u16, uint16_t* out_u16_2) {
        std::tuple<uint16_t, uint16_t> ret = mock_uint16_.Call(u16);
        *out_u16_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual uint32_t SynchronousPrimitiveUint32(uint32_t u32, uint32_t* out_u32_2) {
        std::tuple<uint32_t, uint32_t> ret = mock_uint32_.Call(u32);
        *out_u32_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual uint64_t SynchronousPrimitiveUint64(uint64_t u64, uint64_t* out_u64_2) {
        std::tuple<uint64_t, uint64_t> ret = mock_uint64_.Call(u64);
        *out_u64_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual float SynchronousPrimitiveFloat32(float f32, float* out_f32_2) {
        std::tuple<float, float> ret = mock_float32_.Call(f32);
        *out_f32_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual double SynchronousPrimitiveFloat64(double u64, double* out_f64_2) {
        std::tuple<double, double> ret = mock_float64_.Call(u64);
        *out_f64_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    mock_function::MockFunction<std::tuple<bool, bool>, bool>& mock_bool() { return mock_bool_; }
    mock_function::MockFunction<std::tuple<int8_t, int8_t>, int8_t>& mock_int8() { return mock_int8_; }
    mock_function::MockFunction<std::tuple<int16_t, int16_t>, int16_t>& mock_int16() { return mock_int16_; }
    mock_function::MockFunction<std::tuple<int32_t, int32_t>, int32_t>& mock_int32() { return mock_int32_; }
    mock_function::MockFunction<std::tuple<int64_t, int64_t>, int64_t>& mock_int64() { return mock_int64_; }
    mock_function::MockFunction<std::tuple<uint8_t, uint8_t>, uint8_t>& mock_uint8() { return mock_uint8_; }
    mock_function::MockFunction<std::tuple<uint16_t, uint16_t>, uint16_t>& mock_uint16() { return mock_uint16_; }
    mock_function::MockFunction<std::tuple<uint32_t, uint32_t>, uint32_t>& mock_uint32() { return mock_uint32_; }
    mock_function::MockFunction<std::tuple<uint64_t, uint64_t>, uint64_t>& mock_uint64() { return mock_uint64_; }
    mock_function::MockFunction<std::tuple<float, float>, float>& mock_float32() { return mock_float32_; }
    mock_function::MockFunction<std::tuple<double, double>, double>& mock_float64() { return mock_float64_; }

protected:
    mock_function::MockFunction<std::tuple<bool, bool>, bool> mock_bool_;
    mock_function::MockFunction<std::tuple<int8_t, int8_t>, int8_t> mock_int8_;
    mock_function::MockFunction<std::tuple<int16_t, int16_t>, int16_t> mock_int16_;
    mock_function::MockFunction<std::tuple<int32_t, int32_t>, int32_t> mock_int32_;
    mock_function::MockFunction<std::tuple<int64_t, int64_t>, int64_t> mock_int64_;
    mock_function::MockFunction<std::tuple<uint8_t, uint8_t>, uint8_t> mock_uint8_;
    mock_function::MockFunction<std::tuple<uint16_t, uint16_t>, uint16_t> mock_uint16_;
    mock_function::MockFunction<std::tuple<uint32_t, uint32_t>, uint32_t> mock_uint32_;
    mock_function::MockFunction<std::tuple<uint64_t, uint64_t>, uint64_t> mock_uint64_;
    mock_function::MockFunction<std::tuple<float, float>, float> mock_float32_;
    mock_function::MockFunction<std::tuple<double, double>, double> mock_float64_;

private:
    const synchronous_primitive_protocol_t proto_;
};

// This class mocks a device by providing a async_primitive_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockAsyncPrimitive async_primitive;
//
// /* Set some expectations on the device by calling async_primitive.Expect... methods. */
//
// SomeDriver dut(async_primitive.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(async_primitive.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockAsyncPrimitive : ddk::AsyncPrimitiveProtocol<MockAsyncPrimitive> {
public:
    MockAsyncPrimitive() : proto_{&async_primitive_protocol_ops_, this} {}

    virtual ~MockAsyncPrimitive() {}

    const async_primitive_protocol_t* GetProto() const { return &proto_; }

    virtual MockAsyncPrimitive& ExpectBool(bool b, bool out_b, bool out_b_2) {
        mock_bool_.ExpectCall({out_b, out_b_2}, b);
        return *this;
    }

    virtual MockAsyncPrimitive& ExpectInt8(int8_t i8, int8_t out_i8, int8_t out_i8_2) {
        mock_int8_.ExpectCall({out_i8, out_i8_2}, i8);
        return *this;
    }

    virtual MockAsyncPrimitive& ExpectInt16(int16_t i16, int16_t out_i16, int16_t out_i16_2) {
        mock_int16_.ExpectCall({out_i16, out_i16_2}, i16);
        return *this;
    }

    virtual MockAsyncPrimitive& ExpectInt32(int32_t i32, int32_t out_i32, int32_t out_i32_2) {
        mock_int32_.ExpectCall({out_i32, out_i32_2}, i32);
        return *this;
    }

    virtual MockAsyncPrimitive& ExpectInt64(int64_t i64, int64_t out_i64, int64_t out_i64_2) {
        mock_int64_.ExpectCall({out_i64, out_i64_2}, i64);
        return *this;
    }

    virtual MockAsyncPrimitive& ExpectUint8(uint8_t u8, uint8_t out_u8, uint8_t out_u8_2) {
        mock_uint8_.ExpectCall({out_u8, out_u8_2}, u8);
        return *this;
    }

    virtual MockAsyncPrimitive& ExpectUint16(uint16_t u16, uint16_t out_u16, uint16_t out_u16_2) {
        mock_uint16_.ExpectCall({out_u16, out_u16_2}, u16);
        return *this;
    }

    virtual MockAsyncPrimitive& ExpectUint32(uint32_t u32, uint32_t out_u32, uint32_t out_u32_2) {
        mock_uint32_.ExpectCall({out_u32, out_u32_2}, u32);
        return *this;
    }

    virtual MockAsyncPrimitive& ExpectUint64(uint64_t u64, uint64_t out_u64, uint64_t out_u64_2) {
        mock_uint64_.ExpectCall({out_u64, out_u64_2}, u64);
        return *this;
    }

    virtual MockAsyncPrimitive& ExpectFloat32(float f32, float out_f32, float out_f32_2) {
        mock_float32_.ExpectCall({out_f32, out_f32_2}, f32);
        return *this;
    }

    virtual MockAsyncPrimitive& ExpectFloat64(double u64, double out_f64, double out_f64_2) {
        mock_float64_.ExpectCall({out_f64, out_f64_2}, u64);
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
    }

    virtual void AsyncPrimitiveBool(bool b, async_primitive_bool_callback callback, void* cookie) {
        std::tuple<bool, bool> ret = mock_bool_.Call(b);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncPrimitiveInt8(int8_t i8, async_primitive_int8_callback callback, void* cookie) {
        std::tuple<int8_t, int8_t> ret = mock_int8_.Call(i8);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncPrimitiveInt16(int16_t i16, async_primitive_int16_callback callback, void* cookie) {
        std::tuple<int16_t, int16_t> ret = mock_int16_.Call(i16);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncPrimitiveInt32(int32_t i32, async_primitive_int32_callback callback, void* cookie) {
        std::tuple<int32_t, int32_t> ret = mock_int32_.Call(i32);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncPrimitiveInt64(int64_t i64, async_primitive_int64_callback callback, void* cookie) {
        std::tuple<int64_t, int64_t> ret = mock_int64_.Call(i64);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncPrimitiveUint8(uint8_t u8, async_primitive_uint8_callback callback, void* cookie) {
        std::tuple<uint8_t, uint8_t> ret = mock_uint8_.Call(u8);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncPrimitiveUint16(uint16_t u16, async_primitive_uint16_callback callback, void* cookie) {
        std::tuple<uint16_t, uint16_t> ret = mock_uint16_.Call(u16);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncPrimitiveUint32(uint32_t u32, async_primitive_uint32_callback callback, void* cookie) {
        std::tuple<uint32_t, uint32_t> ret = mock_uint32_.Call(u32);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncPrimitiveUint64(uint64_t u64, async_primitive_uint64_callback callback, void* cookie) {
        std::tuple<uint64_t, uint64_t> ret = mock_uint64_.Call(u64);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncPrimitiveFloat32(float f32, async_primitive_float32_callback callback, void* cookie) {
        std::tuple<float, float> ret = mock_float32_.Call(f32);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncPrimitiveFloat64(double u64, async_primitive_float64_callback callback, void* cookie) {
        std::tuple<double, double> ret = mock_float64_.Call(u64);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    mock_function::MockFunction<std::tuple<bool, bool>, bool>& mock_bool() { return mock_bool_; }
    mock_function::MockFunction<std::tuple<int8_t, int8_t>, int8_t>& mock_int8() { return mock_int8_; }
    mock_function::MockFunction<std::tuple<int16_t, int16_t>, int16_t>& mock_int16() { return mock_int16_; }
    mock_function::MockFunction<std::tuple<int32_t, int32_t>, int32_t>& mock_int32() { return mock_int32_; }
    mock_function::MockFunction<std::tuple<int64_t, int64_t>, int64_t>& mock_int64() { return mock_int64_; }
    mock_function::MockFunction<std::tuple<uint8_t, uint8_t>, uint8_t>& mock_uint8() { return mock_uint8_; }
    mock_function::MockFunction<std::tuple<uint16_t, uint16_t>, uint16_t>& mock_uint16() { return mock_uint16_; }
    mock_function::MockFunction<std::tuple<uint32_t, uint32_t>, uint32_t>& mock_uint32() { return mock_uint32_; }
    mock_function::MockFunction<std::tuple<uint64_t, uint64_t>, uint64_t>& mock_uint64() { return mock_uint64_; }
    mock_function::MockFunction<std::tuple<float, float>, float>& mock_float32() { return mock_float32_; }
    mock_function::MockFunction<std::tuple<double, double>, double>& mock_float64() { return mock_float64_; }

protected:
    mock_function::MockFunction<std::tuple<bool, bool>, bool> mock_bool_;
    mock_function::MockFunction<std::tuple<int8_t, int8_t>, int8_t> mock_int8_;
    mock_function::MockFunction<std::tuple<int16_t, int16_t>, int16_t> mock_int16_;
    mock_function::MockFunction<std::tuple<int32_t, int32_t>, int32_t> mock_int32_;
    mock_function::MockFunction<std::tuple<int64_t, int64_t>, int64_t> mock_int64_;
    mock_function::MockFunction<std::tuple<uint8_t, uint8_t>, uint8_t> mock_uint8_;
    mock_function::MockFunction<std::tuple<uint16_t, uint16_t>, uint16_t> mock_uint16_;
    mock_function::MockFunction<std::tuple<uint32_t, uint32_t>, uint32_t> mock_uint32_;
    mock_function::MockFunction<std::tuple<uint64_t, uint64_t>, uint64_t> mock_uint64_;
    mock_function::MockFunction<std::tuple<float, float>, float> mock_float32_;
    mock_function::MockFunction<std::tuple<double, double>, double> mock_float64_;

private:
    const async_primitive_protocol_t proto_;
};

} // namespace ddk
