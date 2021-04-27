// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocolbase banjo file

#pragma once

#include <tuple>

#include <banjo/examples/protocolbase/cpp/banjo.h>
#include <lib/mock-function/mock-function.h>

namespace ddk {

// This class mocks a device by providing a synchronous_base_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockSynchronousBase synchronous_base;
//
// /* Set some expectations on the device by calling synchronous_base.Expect... methods. */
//
// SomeDriver dut(synchronous_base.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(synchronous_base.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockSynchronousBase : ddk::SynchronousBaseProtocol<MockSynchronousBase> {
public:
    MockSynchronousBase() : proto_{&synchronous_base_protocol_ops_, this} {}

    virtual ~MockSynchronousBase() {}

    const synchronous_base_protocol_t* GetProto() const { return &proto_; }

    virtual MockSynchronousBase& ExpectStatus(zx_status_t out_status, zx_status_t status, zx_status_t out_status_2) {
        mock_status_.ExpectCall({out_status, out_status_2}, status);
        return *this;
    }

    virtual MockSynchronousBase& ExpectTime(zx_time_t out_time, zx_time_t time, zx_time_t out_time_2) {
        mock_time_.ExpectCall({out_time, out_time_2}, time);
        return *this;
    }

    virtual MockSynchronousBase& ExpectDuration(zx_duration_t out_duration, zx_duration_t duration, zx_duration_t out_duration_2) {
        mock_duration_.ExpectCall({out_duration, out_duration_2}, duration);
        return *this;
    }

    virtual MockSynchronousBase& ExpectKoid(zx_koid_t out_koid, zx_koid_t koid, zx_koid_t out_koid_2) {
        mock_koid_.ExpectCall({out_koid, out_koid_2}, koid);
        return *this;
    }

    virtual MockSynchronousBase& ExpectVaddr(zx_vaddr_t out_vaddr, zx_vaddr_t vaddr, zx_vaddr_t out_vaddr_2) {
        mock_vaddr_.ExpectCall({out_vaddr, out_vaddr_2}, vaddr);
        return *this;
    }

    virtual MockSynchronousBase& ExpectPaddr(zx_paddr_t out_paddr, zx_paddr_t paddr, zx_paddr_t out_paddr_2) {
        mock_paddr_.ExpectCall({out_paddr, out_paddr_2}, paddr);
        return *this;
    }

    virtual MockSynchronousBase& ExpectPaddr32(zx_paddr32_t out_paddr32, zx_paddr32_t paddr32, zx_paddr32_t out_paddr32_2) {
        mock_paddr32_.ExpectCall({out_paddr32, out_paddr32_2}, paddr32);
        return *this;
    }

    virtual MockSynchronousBase& ExpectGpaddr(zx_gpaddr_t out_gpaddr, zx_gpaddr_t gpaddr, zx_gpaddr_t out_gpaddr_2) {
        mock_gpaddr_.ExpectCall({out_gpaddr, out_gpaddr_2}, gpaddr);
        return *this;
    }

    virtual MockSynchronousBase& ExpectOff(zx_off_t out_off, zx_off_t off, zx_off_t out_off_2) {
        mock_off_.ExpectCall({out_off, out_off_2}, off);
        return *this;
    }

    void VerifyAndClear() {
        mock_status_.VerifyAndClear();
        mock_time_.VerifyAndClear();
        mock_duration_.VerifyAndClear();
        mock_koid_.VerifyAndClear();
        mock_vaddr_.VerifyAndClear();
        mock_paddr_.VerifyAndClear();
        mock_paddr32_.VerifyAndClear();
        mock_gpaddr_.VerifyAndClear();
        mock_off_.VerifyAndClear();
    }

    virtual zx_status_t SynchronousBaseStatus(zx_status_t status, zx_status_t* out_status_2) {
        std::tuple<zx_status_t, zx_status_t> ret = mock_status_.Call(status);
        *out_status_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual zx_time_t SynchronousBaseTime(zx_time_t time, zx_time_t* out_time_2) {
        std::tuple<zx_time_t, zx_time_t> ret = mock_time_.Call(time);
        *out_time_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual zx_duration_t SynchronousBaseDuration(zx_duration_t duration, zx_duration_t* out_duration_2) {
        std::tuple<zx_duration_t, zx_duration_t> ret = mock_duration_.Call(duration);
        *out_duration_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual zx_koid_t SynchronousBaseKoid(zx_koid_t koid, zx_koid_t* out_koid_2) {
        std::tuple<zx_koid_t, zx_koid_t> ret = mock_koid_.Call(koid);
        *out_koid_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual zx_vaddr_t SynchronousBaseVaddr(zx_vaddr_t vaddr, zx_vaddr_t* out_vaddr_2) {
        std::tuple<zx_vaddr_t, zx_vaddr_t> ret = mock_vaddr_.Call(vaddr);
        *out_vaddr_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual zx_paddr_t SynchronousBasePaddr(zx_paddr_t paddr, zx_paddr_t* out_paddr_2) {
        std::tuple<zx_paddr_t, zx_paddr_t> ret = mock_paddr_.Call(paddr);
        *out_paddr_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual zx_paddr32_t SynchronousBasePaddr32(zx_paddr32_t paddr32, zx_paddr32_t* out_paddr32_2) {
        std::tuple<zx_paddr32_t, zx_paddr32_t> ret = mock_paddr32_.Call(paddr32);
        *out_paddr32_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual zx_gpaddr_t SynchronousBaseGpaddr(zx_gpaddr_t gpaddr, zx_gpaddr_t* out_gpaddr_2) {
        std::tuple<zx_gpaddr_t, zx_gpaddr_t> ret = mock_gpaddr_.Call(gpaddr);
        *out_gpaddr_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    virtual zx_off_t SynchronousBaseOff(zx_off_t off, zx_off_t* out_off_2) {
        std::tuple<zx_off_t, zx_off_t> ret = mock_off_.Call(off);
        *out_off_2 = std::get<1>(ret);
        return std::get<0>(ret);
    }

    mock_function::MockFunction<std::tuple<zx_status_t, zx_status_t>, zx_status_t>& mock_status() { return mock_status_; }
    mock_function::MockFunction<std::tuple<zx_time_t, zx_time_t>, zx_time_t>& mock_time() { return mock_time_; }
    mock_function::MockFunction<std::tuple<zx_duration_t, zx_duration_t>, zx_duration_t>& mock_duration() { return mock_duration_; }
    mock_function::MockFunction<std::tuple<zx_koid_t, zx_koid_t>, zx_koid_t>& mock_koid() { return mock_koid_; }
    mock_function::MockFunction<std::tuple<zx_vaddr_t, zx_vaddr_t>, zx_vaddr_t>& mock_vaddr() { return mock_vaddr_; }
    mock_function::MockFunction<std::tuple<zx_paddr_t, zx_paddr_t>, zx_paddr_t>& mock_paddr() { return mock_paddr_; }
    mock_function::MockFunction<std::tuple<zx_paddr32_t, zx_paddr32_t>, zx_paddr32_t>& mock_paddr32() { return mock_paddr32_; }
    mock_function::MockFunction<std::tuple<zx_gpaddr_t, zx_gpaddr_t>, zx_gpaddr_t>& mock_gpaddr() { return mock_gpaddr_; }
    mock_function::MockFunction<std::tuple<zx_off_t, zx_off_t>, zx_off_t>& mock_off() { return mock_off_; }

protected:
    mock_function::MockFunction<std::tuple<zx_status_t, zx_status_t>, zx_status_t> mock_status_;
    mock_function::MockFunction<std::tuple<zx_time_t, zx_time_t>, zx_time_t> mock_time_;
    mock_function::MockFunction<std::tuple<zx_duration_t, zx_duration_t>, zx_duration_t> mock_duration_;
    mock_function::MockFunction<std::tuple<zx_koid_t, zx_koid_t>, zx_koid_t> mock_koid_;
    mock_function::MockFunction<std::tuple<zx_vaddr_t, zx_vaddr_t>, zx_vaddr_t> mock_vaddr_;
    mock_function::MockFunction<std::tuple<zx_paddr_t, zx_paddr_t>, zx_paddr_t> mock_paddr_;
    mock_function::MockFunction<std::tuple<zx_paddr32_t, zx_paddr32_t>, zx_paddr32_t> mock_paddr32_;
    mock_function::MockFunction<std::tuple<zx_gpaddr_t, zx_gpaddr_t>, zx_gpaddr_t> mock_gpaddr_;
    mock_function::MockFunction<std::tuple<zx_off_t, zx_off_t>, zx_off_t> mock_off_;

private:
    const synchronous_base_protocol_t proto_;
};

// This class mocks a device by providing a async_base_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockAsyncBase async_base;
//
// /* Set some expectations on the device by calling async_base.Expect... methods. */
//
// SomeDriver dut(async_base.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(async_base.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockAsyncBase : ddk::AsyncBaseProtocol<MockAsyncBase> {
public:
    MockAsyncBase() : proto_{&async_base_protocol_ops_, this} {}

    virtual ~MockAsyncBase() {}

    const async_base_protocol_t* GetProto() const { return &proto_; }

    virtual MockAsyncBase& ExpectStatus(zx_status_t status, zx_status_t out_status, zx_status_t out_status_2) {
        mock_status_.ExpectCall({out_status, out_status_2}, status);
        return *this;
    }

    virtual MockAsyncBase& ExpectTime(zx_time_t time, zx_time_t out_time, zx_time_t out_time_2) {
        mock_time_.ExpectCall({out_time, out_time_2}, time);
        return *this;
    }

    virtual MockAsyncBase& ExpectDuration(zx_duration_t duration, zx_duration_t out_duration, zx_duration_t out_duration_2) {
        mock_duration_.ExpectCall({out_duration, out_duration_2}, duration);
        return *this;
    }

    virtual MockAsyncBase& ExpectKoid(zx_koid_t koid, zx_koid_t out_koid, zx_koid_t out_koid_2) {
        mock_koid_.ExpectCall({out_koid, out_koid_2}, koid);
        return *this;
    }

    virtual MockAsyncBase& ExpectVaddr(zx_vaddr_t vaddr, zx_vaddr_t out_vaddr, zx_vaddr_t out_vaddr_2) {
        mock_vaddr_.ExpectCall({out_vaddr, out_vaddr_2}, vaddr);
        return *this;
    }

    virtual MockAsyncBase& ExpectPaddr(zx_paddr_t paddr, zx_paddr_t out_paddr, zx_paddr_t out_paddr_2) {
        mock_paddr_.ExpectCall({out_paddr, out_paddr_2}, paddr);
        return *this;
    }

    virtual MockAsyncBase& ExpectPaddr32(zx_paddr32_t paddr32, zx_paddr32_t out_paddr32, zx_paddr32_t out_paddr32_2) {
        mock_paddr32_.ExpectCall({out_paddr32, out_paddr32_2}, paddr32);
        return *this;
    }

    virtual MockAsyncBase& ExpectGpaddr(zx_gpaddr_t gpaddr, zx_gpaddr_t out_gpaddr, zx_gpaddr_t out_gpaddr_2) {
        mock_gpaddr_.ExpectCall({out_gpaddr, out_gpaddr_2}, gpaddr);
        return *this;
    }

    virtual MockAsyncBase& ExpectOff(zx_off_t off, zx_off_t out_off, zx_off_t out_off_2) {
        mock_off_.ExpectCall({out_off, out_off_2}, off);
        return *this;
    }

    void VerifyAndClear() {
        mock_status_.VerifyAndClear();
        mock_time_.VerifyAndClear();
        mock_duration_.VerifyAndClear();
        mock_koid_.VerifyAndClear();
        mock_vaddr_.VerifyAndClear();
        mock_paddr_.VerifyAndClear();
        mock_paddr32_.VerifyAndClear();
        mock_gpaddr_.VerifyAndClear();
        mock_off_.VerifyAndClear();
    }

    virtual void AsyncBaseStatus(zx_status_t status, async_base_status_callback callback, void* cookie) {
        std::tuple<zx_status_t, zx_status_t> ret = mock_status_.Call(status);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncBaseTime(zx_time_t time, async_base_time_callback callback, void* cookie) {
        std::tuple<zx_time_t, zx_time_t> ret = mock_time_.Call(time);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncBaseDuration(zx_duration_t duration, async_base_duration_callback callback, void* cookie) {
        std::tuple<zx_duration_t, zx_duration_t> ret = mock_duration_.Call(duration);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncBaseKoid(zx_koid_t koid, async_base_koid_callback callback, void* cookie) {
        std::tuple<zx_koid_t, zx_koid_t> ret = mock_koid_.Call(koid);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncBaseVaddr(zx_vaddr_t vaddr, async_base_vaddr_callback callback, void* cookie) {
        std::tuple<zx_vaddr_t, zx_vaddr_t> ret = mock_vaddr_.Call(vaddr);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncBasePaddr(zx_paddr_t paddr, async_base_paddr_callback callback, void* cookie) {
        std::tuple<zx_paddr_t, zx_paddr_t> ret = mock_paddr_.Call(paddr);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncBasePaddr32(zx_paddr32_t paddr32, async_base_paddr32_callback callback, void* cookie) {
        std::tuple<zx_paddr32_t, zx_paddr32_t> ret = mock_paddr32_.Call(paddr32);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncBaseGpaddr(zx_gpaddr_t gpaddr, async_base_gpaddr_callback callback, void* cookie) {
        std::tuple<zx_gpaddr_t, zx_gpaddr_t> ret = mock_gpaddr_.Call(gpaddr);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    virtual void AsyncBaseOff(zx_off_t off, async_base_off_callback callback, void* cookie) {
        std::tuple<zx_off_t, zx_off_t> ret = mock_off_.Call(off);
        callback(cookie, std::get<0>(ret), std::get<1>(ret));
    }

    mock_function::MockFunction<std::tuple<zx_status_t, zx_status_t>, zx_status_t>& mock_status() { return mock_status_; }
    mock_function::MockFunction<std::tuple<zx_time_t, zx_time_t>, zx_time_t>& mock_time() { return mock_time_; }
    mock_function::MockFunction<std::tuple<zx_duration_t, zx_duration_t>, zx_duration_t>& mock_duration() { return mock_duration_; }
    mock_function::MockFunction<std::tuple<zx_koid_t, zx_koid_t>, zx_koid_t>& mock_koid() { return mock_koid_; }
    mock_function::MockFunction<std::tuple<zx_vaddr_t, zx_vaddr_t>, zx_vaddr_t>& mock_vaddr() { return mock_vaddr_; }
    mock_function::MockFunction<std::tuple<zx_paddr_t, zx_paddr_t>, zx_paddr_t>& mock_paddr() { return mock_paddr_; }
    mock_function::MockFunction<std::tuple<zx_paddr32_t, zx_paddr32_t>, zx_paddr32_t>& mock_paddr32() { return mock_paddr32_; }
    mock_function::MockFunction<std::tuple<zx_gpaddr_t, zx_gpaddr_t>, zx_gpaddr_t>& mock_gpaddr() { return mock_gpaddr_; }
    mock_function::MockFunction<std::tuple<zx_off_t, zx_off_t>, zx_off_t>& mock_off() { return mock_off_; }

protected:
    mock_function::MockFunction<std::tuple<zx_status_t, zx_status_t>, zx_status_t> mock_status_;
    mock_function::MockFunction<std::tuple<zx_time_t, zx_time_t>, zx_time_t> mock_time_;
    mock_function::MockFunction<std::tuple<zx_duration_t, zx_duration_t>, zx_duration_t> mock_duration_;
    mock_function::MockFunction<std::tuple<zx_koid_t, zx_koid_t>, zx_koid_t> mock_koid_;
    mock_function::MockFunction<std::tuple<zx_vaddr_t, zx_vaddr_t>, zx_vaddr_t> mock_vaddr_;
    mock_function::MockFunction<std::tuple<zx_paddr_t, zx_paddr_t>, zx_paddr_t> mock_paddr_;
    mock_function::MockFunction<std::tuple<zx_paddr32_t, zx_paddr32_t>, zx_paddr32_t> mock_paddr32_;
    mock_function::MockFunction<std::tuple<zx_gpaddr_t, zx_gpaddr_t>, zx_gpaddr_t> mock_gpaddr_;
    mock_function::MockFunction<std::tuple<zx_off_t, zx_off_t>, zx_off_t> mock_off_;

private:
    const async_base_protocol_t proto_;
};

} // namespace ddk
