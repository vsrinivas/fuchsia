// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocolhandle banjo file

#pragma once

#include <tuple>

#include <banjo/examples/protocolhandle/cpp/banjo.h>
#include <lib/mock-function/mock-function.h>

namespace ddk {

// This class mocks a device by providing a synchronous_handle_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockSynchronousHandle synchronous_handle;
//
// /* Set some expectations on the device by calling synchronous_handle.Expect... methods. */
//
// SomeDriver dut(synchronous_handle.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(synchronous_handle.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockSynchronousHandle : ddk::SynchronousHandleProtocol<MockSynchronousHandle> {
public:
    MockSynchronousHandle() : proto_{&synchronous_handle_protocol_ops_, this} {}

    virtual ~MockSynchronousHandle() {}

    const synchronous_handle_protocol_t* GetProto() const { return &proto_; }

    virtual MockSynchronousHandle& ExpectHandle(const zx::handle& h, zx::handle out_h, zx::handle out_h2) {
        mock_handle_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockSynchronousHandle& ExpectProcess(const zx::process& h, zx::process out_h, zx::process out_h2) {
        mock_process_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockSynchronousHandle& ExpectThread(const zx::thread& h, zx::thread out_h, zx::thread out_h2) {
        mock_thread_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockSynchronousHandle& ExpectVmo(const zx::vmo& h, zx::vmo out_h, zx::vmo out_h2) {
        mock_vmo_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockSynchronousHandle& ExpectChannel(const zx::channel& h, zx::channel out_h, zx::channel out_h2) {
        mock_channel_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockSynchronousHandle& ExpectEvent(const zx::event& h, zx::event out_h, zx::event out_h2) {
        mock_event_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockSynchronousHandle& ExpectPort(const zx::port& h, zx::port out_h, zx::port out_h2) {
        mock_port_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockSynchronousHandle& ExpectInterrupt(const zx::interrupt& h, zx::interrupt out_h, zx::interrupt out_h2) {
        mock_interrupt_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockSynchronousHandle& ExpectSocket(const zx::socket& h, zx::socket out_h, zx::socket out_h2) {
        mock_socket_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockSynchronousHandle& ExpectResource(const zx::resource& h, zx::resource out_h, zx::resource out_h2) {
        mock_resource_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockSynchronousHandle& ExpectEventPair(const zx::eventpair& h, zx::eventpair out_h, zx::eventpair out_h2) {
        mock_event_pair_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockSynchronousHandle& ExpectJob(const zx::job& h, zx::job out_h, zx::job out_h2) {
        mock_job_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockSynchronousHandle& ExpectVmar(const zx::vmar& h, zx::vmar out_h, zx::vmar out_h2) {
        mock_vmar_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockSynchronousHandle& ExpectFifo(const zx::fifo& h, zx::fifo out_h, zx::fifo out_h2) {
        mock_fifo_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockSynchronousHandle& ExpectGuest(const zx::guest& h, zx::guest out_h, zx::guest out_h2) {
        mock_guest_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockSynchronousHandle& ExpectTimer(const zx::timer& h, zx::timer out_h, zx::timer out_h2) {
        mock_timer_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockSynchronousHandle& ExpectProfile(const zx::profile& h, zx::profile out_h, zx::profile out_h2) {
        mock_profile_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    void VerifyAndClear() {
        mock_handle_.VerifyAndClear();
        mock_process_.VerifyAndClear();
        mock_thread_.VerifyAndClear();
        mock_vmo_.VerifyAndClear();
        mock_channel_.VerifyAndClear();
        mock_event_.VerifyAndClear();
        mock_port_.VerifyAndClear();
        mock_interrupt_.VerifyAndClear();
        mock_socket_.VerifyAndClear();
        mock_resource_.VerifyAndClear();
        mock_event_pair_.VerifyAndClear();
        mock_job_.VerifyAndClear();
        mock_vmar_.VerifyAndClear();
        mock_fifo_.VerifyAndClear();
        mock_guest_.VerifyAndClear();
        mock_timer_.VerifyAndClear();
        mock_profile_.VerifyAndClear();
    }

    virtual void SynchronousHandleHandle(zx::handle h, zx::handle* out_h, zx::handle* out_h2) {
        std::tuple<zx::handle, zx::handle> ret = mock_handle_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    virtual void SynchronousHandleProcess(zx::process h, zx::process* out_h, zx::process* out_h2) {
        std::tuple<zx::process, zx::process> ret = mock_process_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    virtual void SynchronousHandleThread(zx::thread h, zx::thread* out_h, zx::thread* out_h2) {
        std::tuple<zx::thread, zx::thread> ret = mock_thread_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    virtual void SynchronousHandleVmo(zx::vmo h, zx::vmo* out_h, zx::vmo* out_h2) {
        std::tuple<zx::vmo, zx::vmo> ret = mock_vmo_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    virtual void SynchronousHandleChannel(zx::channel h, zx::channel* out_h, zx::channel* out_h2) {
        std::tuple<zx::channel, zx::channel> ret = mock_channel_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    virtual void SynchronousHandleEvent(zx::event h, zx::event* out_h, zx::event* out_h2) {
        std::tuple<zx::event, zx::event> ret = mock_event_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    virtual void SynchronousHandlePort(zx::port h, zx::port* out_h, zx::port* out_h2) {
        std::tuple<zx::port, zx::port> ret = mock_port_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    virtual void SynchronousHandleInterrupt(zx::interrupt h, zx::interrupt* out_h, zx::interrupt* out_h2) {
        std::tuple<zx::interrupt, zx::interrupt> ret = mock_interrupt_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    virtual void SynchronousHandleSocket(zx::socket h, zx::socket* out_h, zx::socket* out_h2) {
        std::tuple<zx::socket, zx::socket> ret = mock_socket_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    virtual void SynchronousHandleResource(zx::resource h, zx::resource* out_h, zx::resource* out_h2) {
        std::tuple<zx::resource, zx::resource> ret = mock_resource_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    virtual void SynchronousHandleEventPair(zx::eventpair h, zx::eventpair* out_h, zx::eventpair* out_h2) {
        std::tuple<zx::eventpair, zx::eventpair> ret = mock_event_pair_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    virtual void SynchronousHandleJob(zx::job h, zx::job* out_h, zx::job* out_h2) {
        std::tuple<zx::job, zx::job> ret = mock_job_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    virtual void SynchronousHandleVmar(zx::vmar h, zx::vmar* out_h, zx::vmar* out_h2) {
        std::tuple<zx::vmar, zx::vmar> ret = mock_vmar_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    virtual void SynchronousHandleFifo(zx::fifo h, zx::fifo* out_h, zx::fifo* out_h2) {
        std::tuple<zx::fifo, zx::fifo> ret = mock_fifo_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    virtual void SynchronousHandleGuest(zx::guest h, zx::guest* out_h, zx::guest* out_h2) {
        std::tuple<zx::guest, zx::guest> ret = mock_guest_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    virtual void SynchronousHandleTimer(zx::timer h, zx::timer* out_h, zx::timer* out_h2) {
        std::tuple<zx::timer, zx::timer> ret = mock_timer_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    virtual void SynchronousHandleProfile(zx::profile h, zx::profile* out_h, zx::profile* out_h2) {
        std::tuple<zx::profile, zx::profile> ret = mock_profile_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    mock_function::MockFunction<std::tuple<zx::handle, zx::handle>, zx::handle>& mock_handle() { return mock_handle_; }
    mock_function::MockFunction<std::tuple<zx::process, zx::process>, zx::process>& mock_process() { return mock_process_; }
    mock_function::MockFunction<std::tuple<zx::thread, zx::thread>, zx::thread>& mock_thread() { return mock_thread_; }
    mock_function::MockFunction<std::tuple<zx::vmo, zx::vmo>, zx::vmo>& mock_vmo() { return mock_vmo_; }
    mock_function::MockFunction<std::tuple<zx::channel, zx::channel>, zx::channel>& mock_channel() { return mock_channel_; }
    mock_function::MockFunction<std::tuple<zx::event, zx::event>, zx::event>& mock_event() { return mock_event_; }
    mock_function::MockFunction<std::tuple<zx::port, zx::port>, zx::port>& mock_port() { return mock_port_; }
    mock_function::MockFunction<std::tuple<zx::interrupt, zx::interrupt>, zx::interrupt>& mock_interrupt() { return mock_interrupt_; }
    mock_function::MockFunction<std::tuple<zx::socket, zx::socket>, zx::socket>& mock_socket() { return mock_socket_; }
    mock_function::MockFunction<std::tuple<zx::resource, zx::resource>, zx::resource>& mock_resource() { return mock_resource_; }
    mock_function::MockFunction<std::tuple<zx::eventpair, zx::eventpair>, zx::eventpair>& mock_event_pair() { return mock_event_pair_; }
    mock_function::MockFunction<std::tuple<zx::job, zx::job>, zx::job>& mock_job() { return mock_job_; }
    mock_function::MockFunction<std::tuple<zx::vmar, zx::vmar>, zx::vmar>& mock_vmar() { return mock_vmar_; }
    mock_function::MockFunction<std::tuple<zx::fifo, zx::fifo>, zx::fifo>& mock_fifo() { return mock_fifo_; }
    mock_function::MockFunction<std::tuple<zx::guest, zx::guest>, zx::guest>& mock_guest() { return mock_guest_; }
    mock_function::MockFunction<std::tuple<zx::timer, zx::timer>, zx::timer>& mock_timer() { return mock_timer_; }
    mock_function::MockFunction<std::tuple<zx::profile, zx::profile>, zx::profile>& mock_profile() { return mock_profile_; }

protected:
    mock_function::MockFunction<std::tuple<zx::handle, zx::handle>, zx::handle> mock_handle_;
    mock_function::MockFunction<std::tuple<zx::process, zx::process>, zx::process> mock_process_;
    mock_function::MockFunction<std::tuple<zx::thread, zx::thread>, zx::thread> mock_thread_;
    mock_function::MockFunction<std::tuple<zx::vmo, zx::vmo>, zx::vmo> mock_vmo_;
    mock_function::MockFunction<std::tuple<zx::channel, zx::channel>, zx::channel> mock_channel_;
    mock_function::MockFunction<std::tuple<zx::event, zx::event>, zx::event> mock_event_;
    mock_function::MockFunction<std::tuple<zx::port, zx::port>, zx::port> mock_port_;
    mock_function::MockFunction<std::tuple<zx::interrupt, zx::interrupt>, zx::interrupt> mock_interrupt_;
    mock_function::MockFunction<std::tuple<zx::socket, zx::socket>, zx::socket> mock_socket_;
    mock_function::MockFunction<std::tuple<zx::resource, zx::resource>, zx::resource> mock_resource_;
    mock_function::MockFunction<std::tuple<zx::eventpair, zx::eventpair>, zx::eventpair> mock_event_pair_;
    mock_function::MockFunction<std::tuple<zx::job, zx::job>, zx::job> mock_job_;
    mock_function::MockFunction<std::tuple<zx::vmar, zx::vmar>, zx::vmar> mock_vmar_;
    mock_function::MockFunction<std::tuple<zx::fifo, zx::fifo>, zx::fifo> mock_fifo_;
    mock_function::MockFunction<std::tuple<zx::guest, zx::guest>, zx::guest> mock_guest_;
    mock_function::MockFunction<std::tuple<zx::timer, zx::timer>, zx::timer> mock_timer_;
    mock_function::MockFunction<std::tuple<zx::profile, zx::profile>, zx::profile> mock_profile_;

private:
    const synchronous_handle_protocol_t proto_;
};

// This class mocks a device by providing a async_handle_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockAsyncHandle async_handle;
//
// /* Set some expectations on the device by calling async_handle.Expect... methods. */
//
// SomeDriver dut(async_handle.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(async_handle.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockAsyncHandle : ddk::AsyncHandleProtocol<MockAsyncHandle> {
public:
    MockAsyncHandle() : proto_{&async_handle_protocol_ops_, this} {}

    virtual ~MockAsyncHandle() {}

    const async_handle_protocol_t* GetProto() const { return &proto_; }

    virtual MockAsyncHandle& ExpectHandle(const zx::handle& h, zx::handle out_h, zx::handle out_h2) {
        mock_handle_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockAsyncHandle& ExpectProcess(const zx::process& h, zx::process out_h, zx::process out_h2) {
        mock_process_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockAsyncHandle& ExpectThread(const zx::thread& h, zx::thread out_h, zx::thread out_h2) {
        mock_thread_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockAsyncHandle& ExpectVmo(const zx::vmo& h, zx::vmo out_h, zx::vmo out_h2) {
        mock_vmo_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockAsyncHandle& ExpectChannel(const zx::channel& h, zx::channel out_h, zx::channel out_h2) {
        mock_channel_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockAsyncHandle& ExpectEvent(const zx::event& h, zx::event out_h, zx::event out_h2) {
        mock_event_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockAsyncHandle& ExpectPort(const zx::port& h, zx::port out_h, zx::port out_h2) {
        mock_port_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockAsyncHandle& ExpectInterrupt(const zx::interrupt& h, zx::interrupt out_h, zx::interrupt out_h2) {
        mock_interrupt_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockAsyncHandle& ExpectSocket(const zx::socket& h, zx::socket out_h, zx::socket out_h2) {
        mock_socket_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockAsyncHandle& ExpectResource(const zx::resource& h, zx::resource out_h, zx::resource out_h2) {
        mock_resource_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockAsyncHandle& ExpectEventPair(const zx::eventpair& h, zx::eventpair out_h, zx::eventpair out_h2) {
        mock_event_pair_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockAsyncHandle& ExpectJob(const zx::job& h, zx::job out_h, zx::job out_h2) {
        mock_job_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockAsyncHandle& ExpectVmar(const zx::vmar& h, zx::vmar out_h, zx::vmar out_h2) {
        mock_vmar_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockAsyncHandle& ExpectFifo(const zx::fifo& h, zx::fifo out_h, zx::fifo out_h2) {
        mock_fifo_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockAsyncHandle& ExpectGuest(const zx::guest& h, zx::guest out_h, zx::guest out_h2) {
        mock_guest_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockAsyncHandle& ExpectTimer(const zx::timer& h, zx::timer out_h, zx::timer out_h2) {
        mock_timer_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    virtual MockAsyncHandle& ExpectProfile(const zx::profile& h, zx::profile out_h, zx::profile out_h2) {
        mock_profile_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    void VerifyAndClear() {
        mock_handle_.VerifyAndClear();
        mock_process_.VerifyAndClear();
        mock_thread_.VerifyAndClear();
        mock_vmo_.VerifyAndClear();
        mock_channel_.VerifyAndClear();
        mock_event_.VerifyAndClear();
        mock_port_.VerifyAndClear();
        mock_interrupt_.VerifyAndClear();
        mock_socket_.VerifyAndClear();
        mock_resource_.VerifyAndClear();
        mock_event_pair_.VerifyAndClear();
        mock_job_.VerifyAndClear();
        mock_vmar_.VerifyAndClear();
        mock_fifo_.VerifyAndClear();
        mock_guest_.VerifyAndClear();
        mock_timer_.VerifyAndClear();
        mock_profile_.VerifyAndClear();
    }

    virtual void AsyncHandleHandle(zx::handle h, async_handle_handle_callback callback, void* cookie) {
        std::tuple<zx::handle, zx::handle> ret = mock_handle_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    virtual void AsyncHandleProcess(zx::process h, async_handle_process_callback callback, void* cookie) {
        std::tuple<zx::process, zx::process> ret = mock_process_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    virtual void AsyncHandleThread(zx::thread h, async_handle_thread_callback callback, void* cookie) {
        std::tuple<zx::thread, zx::thread> ret = mock_thread_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    virtual void AsyncHandleVmo(zx::vmo h, async_handle_vmo_callback callback, void* cookie) {
        std::tuple<zx::vmo, zx::vmo> ret = mock_vmo_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    virtual void AsyncHandleChannel(zx::channel h, async_handle_channel_callback callback, void* cookie) {
        std::tuple<zx::channel, zx::channel> ret = mock_channel_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    virtual void AsyncHandleEvent(zx::event h, async_handle_event_callback callback, void* cookie) {
        std::tuple<zx::event, zx::event> ret = mock_event_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    virtual void AsyncHandlePort(zx::port h, async_handle_port_callback callback, void* cookie) {
        std::tuple<zx::port, zx::port> ret = mock_port_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    virtual void AsyncHandleInterrupt(zx::interrupt h, async_handle_interrupt_callback callback, void* cookie) {
        std::tuple<zx::interrupt, zx::interrupt> ret = mock_interrupt_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    virtual void AsyncHandleSocket(zx::socket h, async_handle_socket_callback callback, void* cookie) {
        std::tuple<zx::socket, zx::socket> ret = mock_socket_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    virtual void AsyncHandleResource(zx::resource h, async_handle_resource_callback callback, void* cookie) {
        std::tuple<zx::resource, zx::resource> ret = mock_resource_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    virtual void AsyncHandleEventPair(zx::eventpair h, async_handle_event_pair_callback callback, void* cookie) {
        std::tuple<zx::eventpair, zx::eventpair> ret = mock_event_pair_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    virtual void AsyncHandleJob(zx::job h, async_handle_job_callback callback, void* cookie) {
        std::tuple<zx::job, zx::job> ret = mock_job_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    virtual void AsyncHandleVmar(zx::vmar h, async_handle_vmar_callback callback, void* cookie) {
        std::tuple<zx::vmar, zx::vmar> ret = mock_vmar_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    virtual void AsyncHandleFifo(zx::fifo h, async_handle_fifo_callback callback, void* cookie) {
        std::tuple<zx::fifo, zx::fifo> ret = mock_fifo_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    virtual void AsyncHandleGuest(zx::guest h, async_handle_guest_callback callback, void* cookie) {
        std::tuple<zx::guest, zx::guest> ret = mock_guest_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    virtual void AsyncHandleTimer(zx::timer h, async_handle_timer_callback callback, void* cookie) {
        std::tuple<zx::timer, zx::timer> ret = mock_timer_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    virtual void AsyncHandleProfile(zx::profile h, async_handle_profile_callback callback, void* cookie) {
        std::tuple<zx::profile, zx::profile> ret = mock_profile_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    mock_function::MockFunction<std::tuple<zx::handle, zx::handle>, zx::handle>& mock_handle() { return mock_handle_; }
    mock_function::MockFunction<std::tuple<zx::process, zx::process>, zx::process>& mock_process() { return mock_process_; }
    mock_function::MockFunction<std::tuple<zx::thread, zx::thread>, zx::thread>& mock_thread() { return mock_thread_; }
    mock_function::MockFunction<std::tuple<zx::vmo, zx::vmo>, zx::vmo>& mock_vmo() { return mock_vmo_; }
    mock_function::MockFunction<std::tuple<zx::channel, zx::channel>, zx::channel>& mock_channel() { return mock_channel_; }
    mock_function::MockFunction<std::tuple<zx::event, zx::event>, zx::event>& mock_event() { return mock_event_; }
    mock_function::MockFunction<std::tuple<zx::port, zx::port>, zx::port>& mock_port() { return mock_port_; }
    mock_function::MockFunction<std::tuple<zx::interrupt, zx::interrupt>, zx::interrupt>& mock_interrupt() { return mock_interrupt_; }
    mock_function::MockFunction<std::tuple<zx::socket, zx::socket>, zx::socket>& mock_socket() { return mock_socket_; }
    mock_function::MockFunction<std::tuple<zx::resource, zx::resource>, zx::resource>& mock_resource() { return mock_resource_; }
    mock_function::MockFunction<std::tuple<zx::eventpair, zx::eventpair>, zx::eventpair>& mock_event_pair() { return mock_event_pair_; }
    mock_function::MockFunction<std::tuple<zx::job, zx::job>, zx::job>& mock_job() { return mock_job_; }
    mock_function::MockFunction<std::tuple<zx::vmar, zx::vmar>, zx::vmar>& mock_vmar() { return mock_vmar_; }
    mock_function::MockFunction<std::tuple<zx::fifo, zx::fifo>, zx::fifo>& mock_fifo() { return mock_fifo_; }
    mock_function::MockFunction<std::tuple<zx::guest, zx::guest>, zx::guest>& mock_guest() { return mock_guest_; }
    mock_function::MockFunction<std::tuple<zx::timer, zx::timer>, zx::timer>& mock_timer() { return mock_timer_; }
    mock_function::MockFunction<std::tuple<zx::profile, zx::profile>, zx::profile>& mock_profile() { return mock_profile_; }

protected:
    mock_function::MockFunction<std::tuple<zx::handle, zx::handle>, zx::handle> mock_handle_;
    mock_function::MockFunction<std::tuple<zx::process, zx::process>, zx::process> mock_process_;
    mock_function::MockFunction<std::tuple<zx::thread, zx::thread>, zx::thread> mock_thread_;
    mock_function::MockFunction<std::tuple<zx::vmo, zx::vmo>, zx::vmo> mock_vmo_;
    mock_function::MockFunction<std::tuple<zx::channel, zx::channel>, zx::channel> mock_channel_;
    mock_function::MockFunction<std::tuple<zx::event, zx::event>, zx::event> mock_event_;
    mock_function::MockFunction<std::tuple<zx::port, zx::port>, zx::port> mock_port_;
    mock_function::MockFunction<std::tuple<zx::interrupt, zx::interrupt>, zx::interrupt> mock_interrupt_;
    mock_function::MockFunction<std::tuple<zx::socket, zx::socket>, zx::socket> mock_socket_;
    mock_function::MockFunction<std::tuple<zx::resource, zx::resource>, zx::resource> mock_resource_;
    mock_function::MockFunction<std::tuple<zx::eventpair, zx::eventpair>, zx::eventpair> mock_event_pair_;
    mock_function::MockFunction<std::tuple<zx::job, zx::job>, zx::job> mock_job_;
    mock_function::MockFunction<std::tuple<zx::vmar, zx::vmar>, zx::vmar> mock_vmar_;
    mock_function::MockFunction<std::tuple<zx::fifo, zx::fifo>, zx::fifo> mock_fifo_;
    mock_function::MockFunction<std::tuple<zx::guest, zx::guest>, zx::guest> mock_guest_;
    mock_function::MockFunction<std::tuple<zx::timer, zx::timer>, zx::timer> mock_timer_;
    mock_function::MockFunction<std::tuple<zx::profile, zx::profile>, zx::profile> mock_profile_;

private:
    const async_handle_protocol_t proto_;
};

// This class mocks a device by providing a another_synchronous_handle_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::MockAnotherSynchronousHandle another_synchronous_handle;
//
// /* Set some expectations on the device by calling another_synchronous_handle.Expect... methods. */
//
// SomeDriver dut(another_synchronous_handle.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(another_synchronous_handle.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class MockAnotherSynchronousHandle : ddk::AnotherSynchronousHandleProtocol<MockAnotherSynchronousHandle> {
public:
    MockAnotherSynchronousHandle() : proto_{&another_synchronous_handle_protocol_ops_, this} {}

    virtual ~MockAnotherSynchronousHandle() {}

    const another_synchronous_handle_protocol_t* GetProto() const { return &proto_; }

    virtual MockAnotherSynchronousHandle& ExpectHandle(const zx::handle& h, zx::handle out_h, zx::handle out_h2) {
        mock_handle_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    void VerifyAndClear() {
        mock_handle_.VerifyAndClear();
    }

    virtual void AnotherSynchronousHandleHandle(zx::handle h, zx::handle* out_h, zx::handle* out_h2) {
        std::tuple<zx::handle, zx::handle> ret = mock_handle_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    mock_function::MockFunction<std::tuple<zx::handle, zx::handle>, zx::handle>& mock_handle() { return mock_handle_; }

protected:
    mock_function::MockFunction<std::tuple<zx::handle, zx::handle>, zx::handle> mock_handle_;

private:
    const another_synchronous_handle_protocol_t proto_;
};

} // namespace ddk
