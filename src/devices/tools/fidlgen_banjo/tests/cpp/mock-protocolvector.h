// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocolvector banjo file

#pragma once

#include <tuple>
#include <vector>

#include <banjo/examples/protocolvector/cpp/banjo.h>
#include <lib/mock-function/mock-function.h>

namespace ddk {

// This class mocks a device by providing a vector_of_vectors_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockVectorOfVectors vector_of_vectors;
//
// /* Set some expectations on the device by calling vector_of_vectors.Expect... methods. */
//
// SomeDriver dut(vector_of_vectors.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(vector_of_vectors.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockVectorOfVectors : ddk::VectorOfVectorsProtocol<MockVectorOfVectors> {
public:
    MockVectorOfVectors() : proto_{&vector_of_vectors_protocol_ops_, this} {}

    virtual ~MockVectorOfVectors() {}

    const vector_of_vectors_protocol_t* GetProto() const { return &proto_; }

    virtual MockVectorOfVectors& ExpectBool(std::vector<bool> b, std::vector<bool> out_b) {
        mock_bool_.ExpectCall({std::move(out_b)}, std::move(b));
        return *this;
    }

    virtual MockVectorOfVectors& ExpectInt8(std::vector<int8_t> i8, std::vector<int8_t> out_i8) {
        mock_int8_.ExpectCall({std::move(out_i8)}, std::move(i8));
        return *this;
    }

    virtual MockVectorOfVectors& ExpectInt16(std::vector<int16_t> i16, std::vector<int16_t> out_i16) {
        mock_int16_.ExpectCall({std::move(out_i16)}, std::move(i16));
        return *this;
    }

    virtual MockVectorOfVectors& ExpectInt32(std::vector<int32_t> i32, std::vector<int32_t> out_i32) {
        mock_int32_.ExpectCall({std::move(out_i32)}, std::move(i32));
        return *this;
    }

    virtual MockVectorOfVectors& ExpectInt64(std::vector<int64_t> i64, std::vector<int64_t> out_i64) {
        mock_int64_.ExpectCall({std::move(out_i64)}, std::move(i64));
        return *this;
    }

    virtual MockVectorOfVectors& ExpectUint8(std::vector<uint8_t> u8, std::vector<uint8_t> out_u8) {
        mock_uint8_.ExpectCall({std::move(out_u8)}, std::move(u8));
        return *this;
    }

    virtual MockVectorOfVectors& ExpectUint16(std::vector<uint16_t> u16, std::vector<uint16_t> out_u16) {
        mock_uint16_.ExpectCall({std::move(out_u16)}, std::move(u16));
        return *this;
    }

    virtual MockVectorOfVectors& ExpectUint32(std::vector<uint32_t> u32, std::vector<uint32_t> out_u32) {
        mock_uint32_.ExpectCall({std::move(out_u32)}, std::move(u32));
        return *this;
    }

    virtual MockVectorOfVectors& ExpectUint64(std::vector<uint64_t> u64, std::vector<uint64_t> out_u64) {
        mock_uint64_.ExpectCall({std::move(out_u64)}, std::move(u64));
        return *this;
    }

    virtual MockVectorOfVectors& ExpectFloat32(std::vector<float> f32, std::vector<float> out_f32) {
        mock_float32_.ExpectCall({std::move(out_f32)}, std::move(f32));
        return *this;
    }

    virtual MockVectorOfVectors& ExpectFloat64(std::vector<double> u64, std::vector<double> out_f64) {
        mock_float64_.ExpectCall({std::move(out_f64)}, std::move(u64));
        return *this;
    }

    virtual MockVectorOfVectors& ExpectHandle(std::vector<zx_handle_t> u64, std::vector<zx_handle_t> out_f64) {
        mock_handle_.ExpectCall({std::move(out_f64)}, std::move(u64));
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

    virtual void VectorOfVectorsBool(const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual) {
        std::tuple<std::vector<bool>> ret = mock_bool_.Call(std::vector<bool>(b_list, b_list + b_count));
        *out_b_actual = std::min<size_t>(std::get<0>(ret).size(), b_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_b_actual, out_b_list);
    }

    virtual void VectorOfVectorsInt8(const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual) {
        std::tuple<std::vector<int8_t>> ret = mock_int8_.Call(std::vector<int8_t>(i8_list, i8_list + i8_count));
        *out_i8_actual = std::min<size_t>(std::get<0>(ret).size(), i8_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_i8_actual, out_i8_list);
    }

    virtual void VectorOfVectorsInt16(const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual) {
        std::tuple<std::vector<int16_t>> ret = mock_int16_.Call(std::vector<int16_t>(i16_list, i16_list + i16_count));
        *out_i16_actual = std::min<size_t>(std::get<0>(ret).size(), i16_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_i16_actual, out_i16_list);
    }

    virtual void VectorOfVectorsInt32(const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual) {
        std::tuple<std::vector<int32_t>> ret = mock_int32_.Call(std::vector<int32_t>(i32_list, i32_list + i32_count));
        *out_i32_actual = std::min<size_t>(std::get<0>(ret).size(), i32_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_i32_actual, out_i32_list);
    }

    virtual void VectorOfVectorsInt64(const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual) {
        std::tuple<std::vector<int64_t>> ret = mock_int64_.Call(std::vector<int64_t>(i64_list, i64_list + i64_count));
        *out_i64_actual = std::min<size_t>(std::get<0>(ret).size(), i64_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_i64_actual, out_i64_list);
    }

    virtual void VectorOfVectorsUint8(const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual) {
        std::tuple<std::vector<uint8_t>> ret = mock_uint8_.Call(std::vector<uint8_t>(u8_list, u8_list + u8_count));
        *out_u8_actual = std::min<size_t>(std::get<0>(ret).size(), u8_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_u8_actual, out_u8_list);
    }

    virtual void VectorOfVectorsUint16(const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual) {
        std::tuple<std::vector<uint16_t>> ret = mock_uint16_.Call(std::vector<uint16_t>(u16_list, u16_list + u16_count));
        *out_u16_actual = std::min<size_t>(std::get<0>(ret).size(), u16_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_u16_actual, out_u16_list);
    }

    virtual void VectorOfVectorsUint32(const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual) {
        std::tuple<std::vector<uint32_t>> ret = mock_uint32_.Call(std::vector<uint32_t>(u32_list, u32_list + u32_count));
        *out_u32_actual = std::min<size_t>(std::get<0>(ret).size(), u32_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_u32_actual, out_u32_list);
    }

    virtual void VectorOfVectorsUint64(const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual) {
        std::tuple<std::vector<uint64_t>> ret = mock_uint64_.Call(std::vector<uint64_t>(u64_list, u64_list + u64_count));
        *out_u64_actual = std::min<size_t>(std::get<0>(ret).size(), u64_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_u64_actual, out_u64_list);
    }

    virtual void VectorOfVectorsFloat32(const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual) {
        std::tuple<std::vector<float>> ret = mock_float32_.Call(std::vector<float>(f32_list, f32_list + f32_count));
        *out_f32_actual = std::min<size_t>(std::get<0>(ret).size(), f32_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_f32_actual, out_f32_list);
    }

    virtual void VectorOfVectorsFloat64(const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
        std::tuple<std::vector<double>> ret = mock_float64_.Call(std::vector<double>(u64_list, u64_list + u64_count));
        *out_f64_actual = std::min<size_t>(std::get<0>(ret).size(), f64_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_f64_actual, out_f64_list);
    }

    virtual void VectorOfVectorsHandle(const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
        std::tuple<std::vector<zx_handle_t>> ret = mock_handle_.Call(std::vector<zx_handle_t>(u64_list, u64_list + u64_count));
        *out_f64_actual = std::min<size_t>(std::get<0>(ret).size(), f64_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_f64_actual, out_f64_list);
    }

    mock_function::MockFunction<std::tuple<std::vector<bool>>, std::vector<bool>>& mock_bool() { return mock_bool_; }
    mock_function::MockFunction<std::tuple<std::vector<int8_t>>, std::vector<int8_t>>& mock_int8() { return mock_int8_; }
    mock_function::MockFunction<std::tuple<std::vector<int16_t>>, std::vector<int16_t>>& mock_int16() { return mock_int16_; }
    mock_function::MockFunction<std::tuple<std::vector<int32_t>>, std::vector<int32_t>>& mock_int32() { return mock_int32_; }
    mock_function::MockFunction<std::tuple<std::vector<int64_t>>, std::vector<int64_t>>& mock_int64() { return mock_int64_; }
    mock_function::MockFunction<std::tuple<std::vector<uint8_t>>, std::vector<uint8_t>>& mock_uint8() { return mock_uint8_; }
    mock_function::MockFunction<std::tuple<std::vector<uint16_t>>, std::vector<uint16_t>>& mock_uint16() { return mock_uint16_; }
    mock_function::MockFunction<std::tuple<std::vector<uint32_t>>, std::vector<uint32_t>>& mock_uint32() { return mock_uint32_; }
    mock_function::MockFunction<std::tuple<std::vector<uint64_t>>, std::vector<uint64_t>>& mock_uint64() { return mock_uint64_; }
    mock_function::MockFunction<std::tuple<std::vector<float>>, std::vector<float>>& mock_float32() { return mock_float32_; }
    mock_function::MockFunction<std::tuple<std::vector<double>>, std::vector<double>>& mock_float64() { return mock_float64_; }
    mock_function::MockFunction<std::tuple<std::vector<zx_handle_t>>, std::vector<zx_handle_t>>& mock_handle() { return mock_handle_; }

protected:
    mock_function::MockFunction<std::tuple<std::vector<bool>>, std::vector<bool>> mock_bool_;
    mock_function::MockFunction<std::tuple<std::vector<int8_t>>, std::vector<int8_t>> mock_int8_;
    mock_function::MockFunction<std::tuple<std::vector<int16_t>>, std::vector<int16_t>> mock_int16_;
    mock_function::MockFunction<std::tuple<std::vector<int32_t>>, std::vector<int32_t>> mock_int32_;
    mock_function::MockFunction<std::tuple<std::vector<int64_t>>, std::vector<int64_t>> mock_int64_;
    mock_function::MockFunction<std::tuple<std::vector<uint8_t>>, std::vector<uint8_t>> mock_uint8_;
    mock_function::MockFunction<std::tuple<std::vector<uint16_t>>, std::vector<uint16_t>> mock_uint16_;
    mock_function::MockFunction<std::tuple<std::vector<uint32_t>>, std::vector<uint32_t>> mock_uint32_;
    mock_function::MockFunction<std::tuple<std::vector<uint64_t>>, std::vector<uint64_t>> mock_uint64_;
    mock_function::MockFunction<std::tuple<std::vector<float>>, std::vector<float>> mock_float32_;
    mock_function::MockFunction<std::tuple<std::vector<double>>, std::vector<double>> mock_float64_;
    mock_function::MockFunction<std::tuple<std::vector<zx_handle_t>>, std::vector<zx_handle_t>> mock_handle_;

private:
    const vector_of_vectors_protocol_t proto_;
};

// This class mocks a device by providing a vector_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockVector vector;
//
// /* Set some expectations on the device by calling vector.Expect... methods. */
//
// SomeDriver dut(vector.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(vector.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockVector : ddk::VectorProtocol<MockVector> {
public:
    MockVector() : proto_{&vector_protocol_ops_, this} {}

    virtual ~MockVector() {}

    const vector_protocol_t* GetProto() const { return &proto_; }

    virtual MockVector& ExpectBool(std::vector<bool> b, std::vector<bool> out_b) {
        mock_bool_.ExpectCall({std::move(out_b)}, std::move(b));
        return *this;
    }

    virtual MockVector& ExpectInt8(std::vector<int8_t> i8, std::vector<int8_t> out_i8) {
        mock_int8_.ExpectCall({std::move(out_i8)}, std::move(i8));
        return *this;
    }

    virtual MockVector& ExpectInt16(std::vector<int16_t> i16, std::vector<int16_t> out_i16) {
        mock_int16_.ExpectCall({std::move(out_i16)}, std::move(i16));
        return *this;
    }

    virtual MockVector& ExpectInt32(std::vector<int32_t> i32, std::vector<int32_t> out_i32) {
        mock_int32_.ExpectCall({std::move(out_i32)}, std::move(i32));
        return *this;
    }

    virtual MockVector& ExpectInt64(std::vector<int64_t> i64, std::vector<int64_t> out_i64) {
        mock_int64_.ExpectCall({std::move(out_i64)}, std::move(i64));
        return *this;
    }

    virtual MockVector& ExpectUint8(std::vector<uint8_t> u8, std::vector<uint8_t> out_u8) {
        mock_uint8_.ExpectCall({std::move(out_u8)}, std::move(u8));
        return *this;
    }

    virtual MockVector& ExpectUint16(std::vector<uint16_t> u16, std::vector<uint16_t> out_u16) {
        mock_uint16_.ExpectCall({std::move(out_u16)}, std::move(u16));
        return *this;
    }

    virtual MockVector& ExpectUint32(std::vector<uint32_t> u32, std::vector<uint32_t> out_u32) {
        mock_uint32_.ExpectCall({std::move(out_u32)}, std::move(u32));
        return *this;
    }

    virtual MockVector& ExpectUint64(std::vector<uint64_t> u64, std::vector<uint64_t> out_u64) {
        mock_uint64_.ExpectCall({std::move(out_u64)}, std::move(u64));
        return *this;
    }

    virtual MockVector& ExpectFloat32(std::vector<float> f32, std::vector<float> out_f32) {
        mock_float32_.ExpectCall({std::move(out_f32)}, std::move(f32));
        return *this;
    }

    virtual MockVector& ExpectFloat64(std::vector<double> u64, std::vector<double> out_f64) {
        mock_float64_.ExpectCall({std::move(out_f64)}, std::move(u64));
        return *this;
    }

    virtual MockVector& ExpectHandle(std::vector<zx_handle_t> u64, std::vector<zx_handle_t> out_f64) {
        mock_handle_.ExpectCall({std::move(out_f64)}, std::move(u64));
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

    virtual void VectorBool(const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual) {
        std::tuple<std::vector<bool>> ret = mock_bool_.Call(std::vector<bool>(b_list, b_list + b_count));
        *out_b_actual = std::min<size_t>(std::get<0>(ret).size(), b_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_b_actual, out_b_list);
    }

    virtual void VectorInt8(const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual) {
        std::tuple<std::vector<int8_t>> ret = mock_int8_.Call(std::vector<int8_t>(i8_list, i8_list + i8_count));
        *out_i8_actual = std::min<size_t>(std::get<0>(ret).size(), i8_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_i8_actual, out_i8_list);
    }

    virtual void VectorInt16(const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual) {
        std::tuple<std::vector<int16_t>> ret = mock_int16_.Call(std::vector<int16_t>(i16_list, i16_list + i16_count));
        *out_i16_actual = std::min<size_t>(std::get<0>(ret).size(), i16_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_i16_actual, out_i16_list);
    }

    virtual void VectorInt32(const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual) {
        std::tuple<std::vector<int32_t>> ret = mock_int32_.Call(std::vector<int32_t>(i32_list, i32_list + i32_count));
        *out_i32_actual = std::min<size_t>(std::get<0>(ret).size(), i32_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_i32_actual, out_i32_list);
    }

    virtual void VectorInt64(const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual) {
        std::tuple<std::vector<int64_t>> ret = mock_int64_.Call(std::vector<int64_t>(i64_list, i64_list + i64_count));
        *out_i64_actual = std::min<size_t>(std::get<0>(ret).size(), i64_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_i64_actual, out_i64_list);
    }

    virtual void VectorUint8(const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual) {
        std::tuple<std::vector<uint8_t>> ret = mock_uint8_.Call(std::vector<uint8_t>(u8_list, u8_list + u8_count));
        *out_u8_actual = std::min<size_t>(std::get<0>(ret).size(), u8_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_u8_actual, out_u8_list);
    }

    virtual void VectorUint16(const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual) {
        std::tuple<std::vector<uint16_t>> ret = mock_uint16_.Call(std::vector<uint16_t>(u16_list, u16_list + u16_count));
        *out_u16_actual = std::min<size_t>(std::get<0>(ret).size(), u16_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_u16_actual, out_u16_list);
    }

    virtual void VectorUint32(const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual) {
        std::tuple<std::vector<uint32_t>> ret = mock_uint32_.Call(std::vector<uint32_t>(u32_list, u32_list + u32_count));
        *out_u32_actual = std::min<size_t>(std::get<0>(ret).size(), u32_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_u32_actual, out_u32_list);
    }

    virtual void VectorUint64(const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual) {
        std::tuple<std::vector<uint64_t>> ret = mock_uint64_.Call(std::vector<uint64_t>(u64_list, u64_list + u64_count));
        *out_u64_actual = std::min<size_t>(std::get<0>(ret).size(), u64_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_u64_actual, out_u64_list);
    }

    virtual void VectorFloat32(const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual) {
        std::tuple<std::vector<float>> ret = mock_float32_.Call(std::vector<float>(f32_list, f32_list + f32_count));
        *out_f32_actual = std::min<size_t>(std::get<0>(ret).size(), f32_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_f32_actual, out_f32_list);
    }

    virtual void VectorFloat64(const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
        std::tuple<std::vector<double>> ret = mock_float64_.Call(std::vector<double>(u64_list, u64_list + u64_count));
        *out_f64_actual = std::min<size_t>(std::get<0>(ret).size(), f64_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_f64_actual, out_f64_list);
    }

    virtual void VectorHandle(const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
        std::tuple<std::vector<zx_handle_t>> ret = mock_handle_.Call(std::vector<zx_handle_t>(u64_list, u64_list + u64_count));
        *out_f64_actual = std::min<size_t>(std::get<0>(ret).size(), f64_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_f64_actual, out_f64_list);
    }

    mock_function::MockFunction<std::tuple<std::vector<bool>>, std::vector<bool>>& mock_bool() { return mock_bool_; }
    mock_function::MockFunction<std::tuple<std::vector<int8_t>>, std::vector<int8_t>>& mock_int8() { return mock_int8_; }
    mock_function::MockFunction<std::tuple<std::vector<int16_t>>, std::vector<int16_t>>& mock_int16() { return mock_int16_; }
    mock_function::MockFunction<std::tuple<std::vector<int32_t>>, std::vector<int32_t>>& mock_int32() { return mock_int32_; }
    mock_function::MockFunction<std::tuple<std::vector<int64_t>>, std::vector<int64_t>>& mock_int64() { return mock_int64_; }
    mock_function::MockFunction<std::tuple<std::vector<uint8_t>>, std::vector<uint8_t>>& mock_uint8() { return mock_uint8_; }
    mock_function::MockFunction<std::tuple<std::vector<uint16_t>>, std::vector<uint16_t>>& mock_uint16() { return mock_uint16_; }
    mock_function::MockFunction<std::tuple<std::vector<uint32_t>>, std::vector<uint32_t>>& mock_uint32() { return mock_uint32_; }
    mock_function::MockFunction<std::tuple<std::vector<uint64_t>>, std::vector<uint64_t>>& mock_uint64() { return mock_uint64_; }
    mock_function::MockFunction<std::tuple<std::vector<float>>, std::vector<float>>& mock_float32() { return mock_float32_; }
    mock_function::MockFunction<std::tuple<std::vector<double>>, std::vector<double>>& mock_float64() { return mock_float64_; }
    mock_function::MockFunction<std::tuple<std::vector<zx_handle_t>>, std::vector<zx_handle_t>>& mock_handle() { return mock_handle_; }

protected:
    mock_function::MockFunction<std::tuple<std::vector<bool>>, std::vector<bool>> mock_bool_;
    mock_function::MockFunction<std::tuple<std::vector<int8_t>>, std::vector<int8_t>> mock_int8_;
    mock_function::MockFunction<std::tuple<std::vector<int16_t>>, std::vector<int16_t>> mock_int16_;
    mock_function::MockFunction<std::tuple<std::vector<int32_t>>, std::vector<int32_t>> mock_int32_;
    mock_function::MockFunction<std::tuple<std::vector<int64_t>>, std::vector<int64_t>> mock_int64_;
    mock_function::MockFunction<std::tuple<std::vector<uint8_t>>, std::vector<uint8_t>> mock_uint8_;
    mock_function::MockFunction<std::tuple<std::vector<uint16_t>>, std::vector<uint16_t>> mock_uint16_;
    mock_function::MockFunction<std::tuple<std::vector<uint32_t>>, std::vector<uint32_t>> mock_uint32_;
    mock_function::MockFunction<std::tuple<std::vector<uint64_t>>, std::vector<uint64_t>> mock_uint64_;
    mock_function::MockFunction<std::tuple<std::vector<float>>, std::vector<float>> mock_float32_;
    mock_function::MockFunction<std::tuple<std::vector<double>>, std::vector<double>> mock_float64_;
    mock_function::MockFunction<std::tuple<std::vector<zx_handle_t>>, std::vector<zx_handle_t>> mock_handle_;

private:
    const vector_protocol_t proto_;
};

// This class mocks a device by providing a vector2_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockVector2 vector2;
//
// /* Set some expectations on the device by calling vector2.Expect... methods. */
//
// SomeDriver dut(vector2.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(vector2.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockVector2 : ddk::Vector2Protocol<MockVector2> {
public:
    MockVector2() : proto_{&vector2_protocol_ops_, this} {}

    virtual ~MockVector2() {}

    const vector2_protocol_t* GetProto() const { return &proto_; }

    virtual MockVector2& ExpectBool(std::vector<bool> b, std::vector<bool> out_b) {
        mock_bool_.ExpectCall({std::move(out_b)}, std::move(b));
        return *this;
    }

    virtual MockVector2& ExpectInt8(std::vector<int8_t> i8, std::vector<int8_t> out_i8) {
        mock_int8_.ExpectCall({std::move(out_i8)}, std::move(i8));
        return *this;
    }

    virtual MockVector2& ExpectInt16(std::vector<int16_t> i16, std::vector<int16_t> out_i16) {
        mock_int16_.ExpectCall({std::move(out_i16)}, std::move(i16));
        return *this;
    }

    virtual MockVector2& ExpectInt32(std::vector<int32_t> i32, std::vector<int32_t> out_i32) {
        mock_int32_.ExpectCall({std::move(out_i32)}, std::move(i32));
        return *this;
    }

    virtual MockVector2& ExpectInt64(std::vector<int64_t> i64, std::vector<int64_t> out_i64) {
        mock_int64_.ExpectCall({std::move(out_i64)}, std::move(i64));
        return *this;
    }

    virtual MockVector2& ExpectUint8(std::vector<uint8_t> u8, std::vector<uint8_t> out_u8) {
        mock_uint8_.ExpectCall({std::move(out_u8)}, std::move(u8));
        return *this;
    }

    virtual MockVector2& ExpectUint16(std::vector<uint16_t> u16, std::vector<uint16_t> out_u16) {
        mock_uint16_.ExpectCall({std::move(out_u16)}, std::move(u16));
        return *this;
    }

    virtual MockVector2& ExpectUint32(std::vector<uint32_t> u32, std::vector<uint32_t> out_u32) {
        mock_uint32_.ExpectCall({std::move(out_u32)}, std::move(u32));
        return *this;
    }

    virtual MockVector2& ExpectUint64(std::vector<uint64_t> u64, std::vector<uint64_t> out_u64) {
        mock_uint64_.ExpectCall({std::move(out_u64)}, std::move(u64));
        return *this;
    }

    virtual MockVector2& ExpectFloat32(std::vector<float> f32, std::vector<float> out_f32) {
        mock_float32_.ExpectCall({std::move(out_f32)}, std::move(f32));
        return *this;
    }

    virtual MockVector2& ExpectFloat64(std::vector<double> u64, std::vector<double> out_f64) {
        mock_float64_.ExpectCall({std::move(out_f64)}, std::move(u64));
        return *this;
    }

    virtual MockVector2& ExpectHandle(std::vector<zx_handle_t> u64, std::vector<zx_handle_t> out_f64) {
        mock_handle_.ExpectCall({std::move(out_f64)}, std::move(u64));
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

    virtual void Vector2Bool(const bool* b_list, size_t b_count, bool* out_b_list, size_t b_count, size_t* out_b_actual) {
        std::tuple<std::vector<bool>> ret = mock_bool_.Call(std::vector<bool>(b_list, b_list + b_count));
        *out_b_actual = std::min<size_t>(std::get<0>(ret).size(), b_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_b_actual, out_b_list);
    }

    virtual void Vector2Int8(const int8_t* i8_list, size_t i8_count, int8_t* out_i8_list, size_t i8_count, size_t* out_i8_actual) {
        std::tuple<std::vector<int8_t>> ret = mock_int8_.Call(std::vector<int8_t>(i8_list, i8_list + i8_count));
        *out_i8_actual = std::min<size_t>(std::get<0>(ret).size(), i8_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_i8_actual, out_i8_list);
    }

    virtual void Vector2Int16(const int16_t* i16_list, size_t i16_count, int16_t* out_i16_list, size_t i16_count, size_t* out_i16_actual) {
        std::tuple<std::vector<int16_t>> ret = mock_int16_.Call(std::vector<int16_t>(i16_list, i16_list + i16_count));
        *out_i16_actual = std::min<size_t>(std::get<0>(ret).size(), i16_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_i16_actual, out_i16_list);
    }

    virtual void Vector2Int32(const int32_t* i32_list, size_t i32_count, int32_t* out_i32_list, size_t i32_count, size_t* out_i32_actual) {
        std::tuple<std::vector<int32_t>> ret = mock_int32_.Call(std::vector<int32_t>(i32_list, i32_list + i32_count));
        *out_i32_actual = std::min<size_t>(std::get<0>(ret).size(), i32_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_i32_actual, out_i32_list);
    }

    virtual void Vector2Int64(const int64_t* i64_list, size_t i64_count, int64_t* out_i64_list, size_t i64_count, size_t* out_i64_actual) {
        std::tuple<std::vector<int64_t>> ret = mock_int64_.Call(std::vector<int64_t>(i64_list, i64_list + i64_count));
        *out_i64_actual = std::min<size_t>(std::get<0>(ret).size(), i64_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_i64_actual, out_i64_list);
    }

    virtual void Vector2Uint8(const uint8_t* u8_list, size_t u8_count, uint8_t* out_u8_list, size_t u8_count, size_t* out_u8_actual) {
        std::tuple<std::vector<uint8_t>> ret = mock_uint8_.Call(std::vector<uint8_t>(u8_list, u8_list + u8_count));
        *out_u8_actual = std::min<size_t>(std::get<0>(ret).size(), u8_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_u8_actual, out_u8_list);
    }

    virtual void Vector2Uint16(const uint16_t* u16_list, size_t u16_count, uint16_t* out_u16_list, size_t u16_count, size_t* out_u16_actual) {
        std::tuple<std::vector<uint16_t>> ret = mock_uint16_.Call(std::vector<uint16_t>(u16_list, u16_list + u16_count));
        *out_u16_actual = std::min<size_t>(std::get<0>(ret).size(), u16_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_u16_actual, out_u16_list);
    }

    virtual void Vector2Uint32(const uint32_t* u32_list, size_t u32_count, uint32_t* out_u32_list, size_t u32_count, size_t* out_u32_actual) {
        std::tuple<std::vector<uint32_t>> ret = mock_uint32_.Call(std::vector<uint32_t>(u32_list, u32_list + u32_count));
        *out_u32_actual = std::min<size_t>(std::get<0>(ret).size(), u32_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_u32_actual, out_u32_list);
    }

    virtual void Vector2Uint64(const uint64_t* u64_list, size_t u64_count, uint64_t* out_u64_list, size_t u64_count, size_t* out_u64_actual) {
        std::tuple<std::vector<uint64_t>> ret = mock_uint64_.Call(std::vector<uint64_t>(u64_list, u64_list + u64_count));
        *out_u64_actual = std::min<size_t>(std::get<0>(ret).size(), u64_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_u64_actual, out_u64_list);
    }

    virtual void Vector2Float32(const float* f32_list, size_t f32_count, float* out_f32_list, size_t f32_count, size_t* out_f32_actual) {
        std::tuple<std::vector<float>> ret = mock_float32_.Call(std::vector<float>(f32_list, f32_list + f32_count));
        *out_f32_actual = std::min<size_t>(std::get<0>(ret).size(), f32_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_f32_actual, out_f32_list);
    }

    virtual void Vector2Float64(const double* u64_list, size_t u64_count, double* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
        std::tuple<std::vector<double>> ret = mock_float64_.Call(std::vector<double>(u64_list, u64_list + u64_count));
        *out_f64_actual = std::min<size_t>(std::get<0>(ret).size(), f64_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_f64_actual, out_f64_list);
    }

    virtual void Vector2Handle(const zx_handle_t* u64_list, size_t u64_count, zx_handle_t* out_f64_list, size_t f64_count, size_t* out_f64_actual) {
        std::tuple<std::vector<zx_handle_t>> ret = mock_handle_.Call(std::vector<zx_handle_t>(u64_list, u64_list + u64_count));
        *out_f64_actual = std::min<size_t>(std::get<0>(ret).size(), f64_count);
        std::move(std::get<0>(ret).begin(), std::get<0>(ret).begin() + *out_f64_actual, out_f64_list);
    }

    mock_function::MockFunction<std::tuple<std::vector<bool>>, std::vector<bool>>& mock_bool() { return mock_bool_; }
    mock_function::MockFunction<std::tuple<std::vector<int8_t>>, std::vector<int8_t>>& mock_int8() { return mock_int8_; }
    mock_function::MockFunction<std::tuple<std::vector<int16_t>>, std::vector<int16_t>>& mock_int16() { return mock_int16_; }
    mock_function::MockFunction<std::tuple<std::vector<int32_t>>, std::vector<int32_t>>& mock_int32() { return mock_int32_; }
    mock_function::MockFunction<std::tuple<std::vector<int64_t>>, std::vector<int64_t>>& mock_int64() { return mock_int64_; }
    mock_function::MockFunction<std::tuple<std::vector<uint8_t>>, std::vector<uint8_t>>& mock_uint8() { return mock_uint8_; }
    mock_function::MockFunction<std::tuple<std::vector<uint16_t>>, std::vector<uint16_t>>& mock_uint16() { return mock_uint16_; }
    mock_function::MockFunction<std::tuple<std::vector<uint32_t>>, std::vector<uint32_t>>& mock_uint32() { return mock_uint32_; }
    mock_function::MockFunction<std::tuple<std::vector<uint64_t>>, std::vector<uint64_t>>& mock_uint64() { return mock_uint64_; }
    mock_function::MockFunction<std::tuple<std::vector<float>>, std::vector<float>>& mock_float32() { return mock_float32_; }
    mock_function::MockFunction<std::tuple<std::vector<double>>, std::vector<double>>& mock_float64() { return mock_float64_; }
    mock_function::MockFunction<std::tuple<std::vector<zx_handle_t>>, std::vector<zx_handle_t>>& mock_handle() { return mock_handle_; }

protected:
    mock_function::MockFunction<std::tuple<std::vector<bool>>, std::vector<bool>> mock_bool_;
    mock_function::MockFunction<std::tuple<std::vector<int8_t>>, std::vector<int8_t>> mock_int8_;
    mock_function::MockFunction<std::tuple<std::vector<int16_t>>, std::vector<int16_t>> mock_int16_;
    mock_function::MockFunction<std::tuple<std::vector<int32_t>>, std::vector<int32_t>> mock_int32_;
    mock_function::MockFunction<std::tuple<std::vector<int64_t>>, std::vector<int64_t>> mock_int64_;
    mock_function::MockFunction<std::tuple<std::vector<uint8_t>>, std::vector<uint8_t>> mock_uint8_;
    mock_function::MockFunction<std::tuple<std::vector<uint16_t>>, std::vector<uint16_t>> mock_uint16_;
    mock_function::MockFunction<std::tuple<std::vector<uint32_t>>, std::vector<uint32_t>> mock_uint32_;
    mock_function::MockFunction<std::tuple<std::vector<uint64_t>>, std::vector<uint64_t>> mock_uint64_;
    mock_function::MockFunction<std::tuple<std::vector<float>>, std::vector<float>> mock_float32_;
    mock_function::MockFunction<std::tuple<std::vector<double>>, std::vector<double>> mock_float64_;
    mock_function::MockFunction<std::tuple<std::vector<zx_handle_t>>, std::vector<zx_handle_t>> mock_handle_;

private:
    const vector2_protocol_t proto_;
};

} // namespace ddk
