// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocol.handle banjo file

#pragma once

#include <tuple>

#include <banjo/examples/protocol/handle.h>
#include <lib/mock-function/mock-function.h>

namespace ddk {

class MockSynchronousHandle : ddk::SynchronousHandleProtocol<MockSynchronousHandle> {
public:
    MockSynchronousHandle() : proto_{&synchronous_handle_protocol_ops_, this} {}

    const synchronous_handle_protocol_t* GetProto() const { return &proto_; }

    MockSynchronousHandle& ExpectHandle(const zx::handle& h, zx::handle out_h, zx::handle out_h2) {
        mock_handle_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockSynchronousHandle& ExpectProcess(const zx::process& h, zx::process out_h, zx::process out_h2) {
        mock_process_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockSynchronousHandle& ExpectThread(const zx::thread& h, zx::thread out_h, zx::thread out_h2) {
        mock_thread_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockSynchronousHandle& ExpectVmo(const zx::vmo& h, zx::vmo out_h, zx::vmo out_h2) {
        mock_vmo_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockSynchronousHandle& ExpectChannel(const zx::channel& h, zx::channel out_h, zx::channel out_h2) {
        mock_channel_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockSynchronousHandle& ExpectEvent(const zx::event& h, zx::event out_h, zx::event out_h2) {
        mock_event_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockSynchronousHandle& ExpectPort(const zx::port& h, zx::port out_h, zx::port out_h2) {
        mock_port_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockSynchronousHandle& ExpectInterrupt(const zx::interrupt& h, zx::interrupt out_h, zx::interrupt out_h2) {
        mock_interrupt_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockSynchronousHandle& ExpectDebugLog(const zx::debuglog& h, zx::debuglog out_h, zx::debuglog out_h2) {
        mock_debug_log_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockSynchronousHandle& ExpectSocket(const zx::socket& h, zx::socket out_h, zx::socket out_h2) {
        mock_socket_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockSynchronousHandle& ExpectResource(const zx::resource& h, zx::resource out_h, zx::resource out_h2) {
        mock_resource_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockSynchronousHandle& ExpectEventPair(const zx::eventpair& h, zx::eventpair out_h, zx::eventpair out_h2) {
        mock_event_pair_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockSynchronousHandle& ExpectJob(const zx::job& h, zx::job out_h, zx::job out_h2) {
        mock_job_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockSynchronousHandle& ExpectVmar(const zx::vmar& h, zx::vmar out_h, zx::vmar out_h2) {
        mock_vmar_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockSynchronousHandle& ExpectFifo(const zx::fifo& h, zx::fifo out_h, zx::fifo out_h2) {
        mock_fifo_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockSynchronousHandle& ExpectGuest(const zx::guest& h, zx::guest out_h, zx::guest out_h2) {
        mock_guest_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockSynchronousHandle& ExpectTimer(const zx::timer& h, zx::timer out_h, zx::timer out_h2) {
        mock_timer_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockSynchronousHandle& ExpectProfile(const zx::profile& h, zx::profile out_h, zx::profile out_h2) {
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
        mock_debug_log_.VerifyAndClear();
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

    void SynchronousHandleHandle(zx::handle h, zx::handle* out_h, zx::handle* out_h2) {
        std::tuple<zx::handle, zx::handle> ret = mock_handle_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    void SynchronousHandleProcess(zx::process h, zx::process* out_h, zx::process* out_h2) {
        std::tuple<zx::process, zx::process> ret = mock_process_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    void SynchronousHandleThread(zx::thread h, zx::thread* out_h, zx::thread* out_h2) {
        std::tuple<zx::thread, zx::thread> ret = mock_thread_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    void SynchronousHandleVmo(zx::vmo h, zx::vmo* out_h, zx::vmo* out_h2) {
        std::tuple<zx::vmo, zx::vmo> ret = mock_vmo_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    void SynchronousHandleChannel(zx::channel h, zx::channel* out_h, zx::channel* out_h2) {
        std::tuple<zx::channel, zx::channel> ret = mock_channel_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    void SynchronousHandleEvent(zx::event h, zx::event* out_h, zx::event* out_h2) {
        std::tuple<zx::event, zx::event> ret = mock_event_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    void SynchronousHandlePort(zx::port h, zx::port* out_h, zx::port* out_h2) {
        std::tuple<zx::port, zx::port> ret = mock_port_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    void SynchronousHandleInterrupt(zx::interrupt h, zx::interrupt* out_h, zx::interrupt* out_h2) {
        std::tuple<zx::interrupt, zx::interrupt> ret = mock_interrupt_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    void SynchronousHandleDebugLog(zx::debuglog h, zx::debuglog* out_h, zx::debuglog* out_h2) {
        std::tuple<zx::debuglog, zx::debuglog> ret = mock_debug_log_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    void SynchronousHandleSocket(zx::socket h, zx::socket* out_h, zx::socket* out_h2) {
        std::tuple<zx::socket, zx::socket> ret = mock_socket_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    void SynchronousHandleResource(zx::resource h, zx::resource* out_h, zx::resource* out_h2) {
        std::tuple<zx::resource, zx::resource> ret = mock_resource_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    void SynchronousHandleEventPair(zx::eventpair h, zx::eventpair* out_h, zx::eventpair* out_h2) {
        std::tuple<zx::eventpair, zx::eventpair> ret = mock_event_pair_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    void SynchronousHandleJob(zx::job h, zx::job* out_h, zx::job* out_h2) {
        std::tuple<zx::job, zx::job> ret = mock_job_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    void SynchronousHandleVmar(zx::vmar h, zx::vmar* out_h, zx::vmar* out_h2) {
        std::tuple<zx::vmar, zx::vmar> ret = mock_vmar_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    void SynchronousHandleFifo(zx::fifo h, zx::fifo* out_h, zx::fifo* out_h2) {
        std::tuple<zx::fifo, zx::fifo> ret = mock_fifo_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    void SynchronousHandleGuest(zx::guest h, zx::guest* out_h, zx::guest* out_h2) {
        std::tuple<zx::guest, zx::guest> ret = mock_guest_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    void SynchronousHandleTimer(zx::timer h, zx::timer* out_h, zx::timer* out_h2) {
        std::tuple<zx::timer, zx::timer> ret = mock_timer_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

    void SynchronousHandleProfile(zx::profile h, zx::profile* out_h, zx::profile* out_h2) {
        std::tuple<zx::profile, zx::profile> ret = mock_profile_.Call(std::move(h));
        *out_h = std::move(std::get<0>(ret));
        *out_h2 = std::move(std::get<1>(ret));
    }

private:
    const synchronous_handle_protocol_t proto_;
    mock_function::MockFunction<std::tuple<zx::handle, zx::handle>, zx::handle> mock_handle_;
    mock_function::MockFunction<std::tuple<zx::process, zx::process>, zx::process> mock_process_;
    mock_function::MockFunction<std::tuple<zx::thread, zx::thread>, zx::thread> mock_thread_;
    mock_function::MockFunction<std::tuple<zx::vmo, zx::vmo>, zx::vmo> mock_vmo_;
    mock_function::MockFunction<std::tuple<zx::channel, zx::channel>, zx::channel> mock_channel_;
    mock_function::MockFunction<std::tuple<zx::event, zx::event>, zx::event> mock_event_;
    mock_function::MockFunction<std::tuple<zx::port, zx::port>, zx::port> mock_port_;
    mock_function::MockFunction<std::tuple<zx::interrupt, zx::interrupt>, zx::interrupt> mock_interrupt_;
    mock_function::MockFunction<std::tuple<zx::debuglog, zx::debuglog>, zx::debuglog> mock_debug_log_;
    mock_function::MockFunction<std::tuple<zx::socket, zx::socket>, zx::socket> mock_socket_;
    mock_function::MockFunction<std::tuple<zx::resource, zx::resource>, zx::resource> mock_resource_;
    mock_function::MockFunction<std::tuple<zx::eventpair, zx::eventpair>, zx::eventpair> mock_event_pair_;
    mock_function::MockFunction<std::tuple<zx::job, zx::job>, zx::job> mock_job_;
    mock_function::MockFunction<std::tuple<zx::vmar, zx::vmar>, zx::vmar> mock_vmar_;
    mock_function::MockFunction<std::tuple<zx::fifo, zx::fifo>, zx::fifo> mock_fifo_;
    mock_function::MockFunction<std::tuple<zx::guest, zx::guest>, zx::guest> mock_guest_;
    mock_function::MockFunction<std::tuple<zx::timer, zx::timer>, zx::timer> mock_timer_;
    mock_function::MockFunction<std::tuple<zx::profile, zx::profile>, zx::profile> mock_profile_;
};

class MockAsyncHandle : ddk::AsyncHandleProtocol<MockAsyncHandle> {
public:
    MockAsyncHandle() : proto_{&async_handle_protocol_ops_, this} {}

    const async_handle_protocol_t* GetProto() const { return &proto_; }

    MockAsyncHandle& ExpectHandle(const zx::handle& h, zx::handle out_h, zx::handle out_h2) {
        mock_handle_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockAsyncHandle& ExpectProcess(const zx::process& h, zx::process out_h, zx::process out_h2) {
        mock_process_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockAsyncHandle& ExpectThread(const zx::thread& h, zx::thread out_h, zx::thread out_h2) {
        mock_thread_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockAsyncHandle& ExpectVmo(const zx::vmo& h, zx::vmo out_h, zx::vmo out_h2) {
        mock_vmo_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockAsyncHandle& ExpectChannel(const zx::channel& h, zx::channel out_h, zx::channel out_h2) {
        mock_channel_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockAsyncHandle& ExpectEvent(const zx::event& h, zx::event out_h, zx::event out_h2) {
        mock_event_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockAsyncHandle& ExpectPort(const zx::port& h, zx::port out_h, zx::port out_h2) {
        mock_port_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockAsyncHandle& ExpectInterrupt(const zx::interrupt& h, zx::interrupt out_h, zx::interrupt out_h2) {
        mock_interrupt_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockAsyncHandle& ExpectDebugLog(const zx::debuglog& h, zx::debuglog out_h, zx::debuglog out_h2) {
        mock_debug_log_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockAsyncHandle& ExpectSocket(const zx::socket& h, zx::socket out_h, zx::socket out_h2) {
        mock_socket_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockAsyncHandle& ExpectResource(const zx::resource& h, zx::resource out_h, zx::resource out_h2) {
        mock_resource_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockAsyncHandle& ExpectEventPair(const zx::eventpair& h, zx::eventpair out_h, zx::eventpair out_h2) {
        mock_event_pair_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockAsyncHandle& ExpectJob(const zx::job& h, zx::job out_h, zx::job out_h2) {
        mock_job_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockAsyncHandle& ExpectVmar(const zx::vmar& h, zx::vmar out_h, zx::vmar out_h2) {
        mock_vmar_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockAsyncHandle& ExpectFifo(const zx::fifo& h, zx::fifo out_h, zx::fifo out_h2) {
        mock_fifo_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockAsyncHandle& ExpectGuest(const zx::guest& h, zx::guest out_h, zx::guest out_h2) {
        mock_guest_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockAsyncHandle& ExpectTimer(const zx::timer& h, zx::timer out_h, zx::timer out_h2) {
        mock_timer_.ExpectCall({std::move(out_h), std::move(out_h2)}, h.get());
        return *this;
    }

    MockAsyncHandle& ExpectProfile(const zx::profile& h, zx::profile out_h, zx::profile out_h2) {
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
        mock_debug_log_.VerifyAndClear();
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

    void AsyncHandleHandle(zx::handle h, async_handle_handle_callback callback, void* cookie) {
        std::tuple<zx::handle, zx::handle> ret = mock_handle_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    void AsyncHandleProcess(zx::process h, async_handle_process_callback callback, void* cookie) {
        std::tuple<zx::process, zx::process> ret = mock_process_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    void AsyncHandleThread(zx::thread h, async_handle_thread_callback callback, void* cookie) {
        std::tuple<zx::thread, zx::thread> ret = mock_thread_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    void AsyncHandleVmo(zx::vmo h, async_handle_vmo_callback callback, void* cookie) {
        std::tuple<zx::vmo, zx::vmo> ret = mock_vmo_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    void AsyncHandleChannel(zx::channel h, async_handle_channel_callback callback, void* cookie) {
        std::tuple<zx::channel, zx::channel> ret = mock_channel_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    void AsyncHandleEvent(zx::event h, async_handle_event_callback callback, void* cookie) {
        std::tuple<zx::event, zx::event> ret = mock_event_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    void AsyncHandlePort(zx::port h, async_handle_port_callback callback, void* cookie) {
        std::tuple<zx::port, zx::port> ret = mock_port_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    void AsyncHandleInterrupt(zx::interrupt h, async_handle_interrupt_callback callback, void* cookie) {
        std::tuple<zx::interrupt, zx::interrupt> ret = mock_interrupt_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    void AsyncHandleDebugLog(zx::debuglog h, async_handle_debug_log_callback callback, void* cookie) {
        std::tuple<zx::debuglog, zx::debuglog> ret = mock_debug_log_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    void AsyncHandleSocket(zx::socket h, async_handle_socket_callback callback, void* cookie) {
        std::tuple<zx::socket, zx::socket> ret = mock_socket_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    void AsyncHandleResource(zx::resource h, async_handle_resource_callback callback, void* cookie) {
        std::tuple<zx::resource, zx::resource> ret = mock_resource_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    void AsyncHandleEventPair(zx::eventpair h, async_handle_event_pair_callback callback, void* cookie) {
        std::tuple<zx::eventpair, zx::eventpair> ret = mock_event_pair_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    void AsyncHandleJob(zx::job h, async_handle_job_callback callback, void* cookie) {
        std::tuple<zx::job, zx::job> ret = mock_job_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    void AsyncHandleVmar(zx::vmar h, async_handle_vmar_callback callback, void* cookie) {
        std::tuple<zx::vmar, zx::vmar> ret = mock_vmar_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    void AsyncHandleFifo(zx::fifo h, async_handle_fifo_callback callback, void* cookie) {
        std::tuple<zx::fifo, zx::fifo> ret = mock_fifo_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    void AsyncHandleGuest(zx::guest h, async_handle_guest_callback callback, void* cookie) {
        std::tuple<zx::guest, zx::guest> ret = mock_guest_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    void AsyncHandleTimer(zx::timer h, async_handle_timer_callback callback, void* cookie) {
        std::tuple<zx::timer, zx::timer> ret = mock_timer_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

    void AsyncHandleProfile(zx::profile h, async_handle_profile_callback callback, void* cookie) {
        std::tuple<zx::profile, zx::profile> ret = mock_profile_.Call(std::move(h));
        callback(cookie, std::move(std::get<0>(ret)), std::move(std::get<1>(ret)));
    }

private:
    const async_handle_protocol_t proto_;
    mock_function::MockFunction<std::tuple<zx::handle, zx::handle>, zx::handle> mock_handle_;
    mock_function::MockFunction<std::tuple<zx::process, zx::process>, zx::process> mock_process_;
    mock_function::MockFunction<std::tuple<zx::thread, zx::thread>, zx::thread> mock_thread_;
    mock_function::MockFunction<std::tuple<zx::vmo, zx::vmo>, zx::vmo> mock_vmo_;
    mock_function::MockFunction<std::tuple<zx::channel, zx::channel>, zx::channel> mock_channel_;
    mock_function::MockFunction<std::tuple<zx::event, zx::event>, zx::event> mock_event_;
    mock_function::MockFunction<std::tuple<zx::port, zx::port>, zx::port> mock_port_;
    mock_function::MockFunction<std::tuple<zx::interrupt, zx::interrupt>, zx::interrupt> mock_interrupt_;
    mock_function::MockFunction<std::tuple<zx::debuglog, zx::debuglog>, zx::debuglog> mock_debug_log_;
    mock_function::MockFunction<std::tuple<zx::socket, zx::socket>, zx::socket> mock_socket_;
    mock_function::MockFunction<std::tuple<zx::resource, zx::resource>, zx::resource> mock_resource_;
    mock_function::MockFunction<std::tuple<zx::eventpair, zx::eventpair>, zx::eventpair> mock_event_pair_;
    mock_function::MockFunction<std::tuple<zx::job, zx::job>, zx::job> mock_job_;
    mock_function::MockFunction<std::tuple<zx::vmar, zx::vmar>, zx::vmar> mock_vmar_;
    mock_function::MockFunction<std::tuple<zx::fifo, zx::fifo>, zx::fifo> mock_fifo_;
    mock_function::MockFunction<std::tuple<zx::guest, zx::guest>, zx::guest> mock_guest_;
    mock_function::MockFunction<std::tuple<zx::timer, zx::timer>, zx::timer> mock_timer_;
    mock_function::MockFunction<std::tuple<zx::profile, zx::profile>, zx::profile> mock_profile_;
};

} // namespace ddk
