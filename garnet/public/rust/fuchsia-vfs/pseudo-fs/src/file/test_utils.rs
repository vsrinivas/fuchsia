// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! common utilities for pseudo-file-related tests.

#![cfg(test)]

use {
    crate::directory::entry::DirectoryEntry,
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_io::{FileMarker, FileProxy},
    fuchsia_async as fasync,
    futures::{channel::mpsc, future::{join, LocalFutureObj}, select, Future, Poll, StreamExt},
    pin_utils::pin_mut,
    std::iter,
    void::unreachable,
};

/// A helper to run a pseudo fs server and a client that needs to talk to this server.  This
/// function will create a channel and will pass the client side to `get_client`, while the server
/// side will be passed into an `open()` method on the server.  The server and the client will then
/// be executed on the same single threaded executor until they both stall, then it is asserted that
/// execution is complete and the future has returned. The server is wrapped in a wrapper that will
/// return if `is_terminated` returns true.
///
/// `flags` is passed into the `open()` call.
///
/// See also [`run_server_client_with_mode()`], [`run_server_client_with_open_requests_channel`],
/// and [`run_server_client_with_open_requests_channel_and_executor`].
pub fn run_server_client<GetClientRes>(
    flags: u32,
    server: impl DirectoryEntry,
    get_client: impl FnOnce(FileProxy) -> GetClientRes,
) where
    GetClientRes: Future<Output = ()>,
{
    run_server_client_with_mode(flags, 0, server, get_client)
}

/// Similar to [`run_server_client()`] except that allows to specify the `mode` argument value to
/// the `open()` call.  See [`run_server_client()`] for details.
pub fn run_server_client_with_mode<GetClientRes>(
    flags: u32,
    mode: u32,
    server: impl DirectoryEntry,
    get_client: impl FnOnce(FileProxy) -> GetClientRes,
) where
    GetClientRes: Future<Output = ()>,
{
    let exec = fasync::Executor::new().expect("Executor creation failed");

    run_server_client_with_mode_and_executor(
        flags,
        mode,
        exec,
        server,
        get_client,
        |run_until_stalled_assert| run_until_stalled_assert(true),
    )
}

/// Similar to [`run_server_client()`], except that it allows you to provide an executor and control
/// the execution of the futures. A closure is taken, which is given a reference to
/// run_until_stalled, and asserts that the future either completed or it didn't, depending on the
/// provided boolean.
pub fn run_server_client_with_executor<GetClientRes>(
    flags: u32,
    exec: fasync::Executor,
    server: impl DirectoryEntry,
    get_client: impl FnOnce(FileProxy) -> GetClientRes,
    executor: impl FnOnce(&mut FnMut(bool) -> ()),
) where
    GetClientRes: Future<Output = ()>,
{
    run_server_client_with_mode_and_executor(flags, 0, exec, server, get_client, executor);
}

/// Similar to [`run_server_client()`], adding the additional functionality of
/// [`run_server_client_with_mode()`] and [`run_server_client_with_executor()`].
pub fn run_server_client_with_mode_and_executor<GetClientRes>(
    flags: u32,
    mode: u32,
    mut exec: fasync::Executor,
    mut server: impl DirectoryEntry,
    get_client: impl FnOnce(FileProxy) -> GetClientRes,
    executor: impl FnOnce(&mut FnMut(bool) -> ()),
) where
    GetClientRes: Future<Output = ()>,
{
    let (client_proxy, server_end) =
        create_proxy::<FileMarker>().expect("Failed to create connection endpoints");

    server.open(flags, mode, &mut iter::empty(), ServerEnd::new(server_end.into_channel()));

    let client = get_client(client_proxy);

    // create a wrapper around a server using select!. this lets us poll the server while also
    // completing the server future if it's is_terminated returns true, even though it's poll will
    // never return Ready.
    let server_wrapper = async move {
        loop {
            select! {
                x = server => unreachable(x),
                complete => break,
            }
        }
    };

    let future = join(server_wrapper, client);
    // TODO: How to limit the execution time?  run_until_stalled() does not trigger timers, so
    // I can not do this:
    //
    //   let timeout = 300.millis();
    //   let future = future.on_timeout(
    //       timeout.after_now(),
    //       || panic!("Test did not finish in {}ms", timeout.millis()));

    // As our clients are async generators, we need to pin this future explicitly.
    // All async generators are !Unpin by default.
    pin_mut!(future);
    let mut obj = LocalFutureObj::new(future);
    executor(&mut |should_complete| {
        if should_complete {
            assert_eq!(
                exec.run_until_stalled(&mut obj),
                Poll::Ready(((), ())),
                "future did not complete"
            );
        } else {
            assert_eq!(
                exec.run_until_stalled(&mut obj),
                Poll::Pending,
                "future was not expected to complete"
            );
        }
    });
}

/// Similar to [`run_server_client()`] but does not automatically connect the server and the client
/// code.  Instead the client receives a sender end of an [`OpenRequestArgs`] queue, capable of
/// receiving arguments for the `open()` calls on the server.  This way the client can control when
/// the first `open()` call will happen on the server and/or perform additional `open()` calls on
/// the server.  When [`run_server_client()`] is used, the only way to gen a new connection to the
/// server is to `clone()` the existing one, which might be undesirable for a particular test.
pub fn run_server_client_with_open_requests_channel<GetClientRes>(
    server: impl DirectoryEntry,
    get_client: impl FnOnce(mpsc::Sender<(u32, u32, ServerEnd<FileMarker>)>) -> GetClientRes,
) where
    GetClientRes: Future<Output = ()>,
{
    let exec = fasync::Executor::new().expect("Executor creation failed");

    run_server_client_with_open_requests_channel_and_executor(
        exec,
        server,
        get_client,
        |run_until_stalled_assert| {
            run_until_stalled_assert(true);
        },
    );
}

/// Similar to [`run_server_client_with_open_requests_channel()`] but allows precise control of
/// execution order.  This is necessary when the test needs to make sure that both the server and the
/// client have reached a particular point.  In order to control the execution you would want to
/// share a oneshot channel or a queue between your test code and the executor closures.  The
/// executor closure get a `run_until_stalled_assert` as an argument.  It can use those channels and
/// `run_until_stalled_assert` to control the execution process of the client and the server.
/// `run_until_stalled_assert` asserts whether or not the future completed on that run according to
/// the provided boolean argument.
///
/// For example, a client that wants to make sure that it receives a particular response from the
/// server by certain point, in case the response is asynchronous.
///
/// The server is wrapped in an async block that returns if it's `is_terminated` method returns true.
///
/// See [`file::simple::mock_directory_with_one_file_and_two_connections`] for a usage example.
pub fn run_server_client_with_open_requests_channel_and_executor<GetClientRes>(
    mut exec: fasync::Executor,
    mut server: impl DirectoryEntry,
    get_client: impl FnOnce(mpsc::Sender<(u32, u32, ServerEnd<FileMarker>)>) -> GetClientRes,
    executor: impl FnOnce(&mut FnMut(bool) -> ()),
) where
    GetClientRes: Future<Output = ()>,
{
    let (open_requests_tx, open_requests_rx) =
        mpsc::channel::<(u32, u32, ServerEnd<FileMarker>)>(0);

    let server_wrapper = async move {
        let mut open_requests_rx = open_requests_rx.fuse();
        loop {
            select! {
                x = server => unreachable(x),
                open_req = open_requests_rx.next() => {
                    if let Some((flags, mode, server_end)) = open_req {
                        server
                            .open(flags, mode, &mut iter::empty(),
                                    ServerEnd::new(server_end.into_channel()));
                    }
                },
                complete => return,
            }
        }
    };

    let client = get_client(open_requests_tx);

    let future = join(server_wrapper, client);

    // As our clients are async generators, we need to pin this future explicitly.
    // All async generators are !Unpin by default.
    pin_mut!(future);
    let mut obj = LocalFutureObj::new(future);
    executor(&mut |should_complete| {
        if should_complete {
            assert_eq!(
                exec.run_until_stalled(&mut obj),
                Poll::Ready(((), ())),
                "future did not complete"
            );
        } else {
            assert_eq!(
                exec.run_until_stalled(&mut obj),
                Poll::Pending,
                "future was not expected to complete"
            );
        }
    });
}
