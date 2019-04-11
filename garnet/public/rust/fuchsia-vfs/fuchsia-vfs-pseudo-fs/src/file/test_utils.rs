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
    futures::{channel::mpsc, future::LocalFutureObj, select, Future, FutureExt, Poll, StreamExt},
    pin_utils::pin_mut,
    std::iter,
    void::unreachable,
};

/// A helper to run a pseudo fs server and a client that needs to talk to this server.  This
/// function will create a channel and will pass the client side to `get_client`, while the server
/// side will be passed into an `open()` method on the server.  The server and the client will then
/// be executed on the same single threaded executor until they both stall.
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
    mut server: impl DirectoryEntry,
    get_client: impl FnOnce(FileProxy) -> GetClientRes,
) where
    GetClientRes: Future<Output = ()>,
{
    let mut exec = fasync::Executor::new().expect("Executor creation failed");

    let (client_proxy, server_end) =
        create_proxy::<FileMarker>().expect("Failed to create connection endpoints");

    server.open(flags, mode, &mut iter::empty(), ServerEnd::new(server_end.into_channel()));

    let client = get_client(client_proxy);

    let future = server.join(client);
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
    let _ = exec.run_until_stalled(&mut future);
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
        |run_until_stalled| {
            let _ = run_until_stalled();
        },
    );
}

/// Similar to [`run_server_client_with_open_requests_channel()`] but allows precise control of
/// execution order.  This is necessary when the test needs to make sure that both the server and the
/// client have reached a particular point.  In order to control the execution you would want to
/// share a oneshot channel or a queue between your test code and the executor closures.  The
/// executor closure get a `run_until_stalled` as an argument.  It can use those channels and
/// `run_until_stalled` to control the execution process of the client and the server.
///
/// For example, a client that wants to make sure that it receives a particular response from the
/// server by certain point, in case the response is asynchronous.
///
/// See [`file::simple::mock_directory_with_one_file_and_two_connections`] for a usage example.
pub fn run_server_client_with_open_requests_channel_and_executor<GetClientRes>(
    mut exec: fasync::Executor,
    mut server: impl DirectoryEntry,
    get_client: impl FnOnce(mpsc::Sender<(u32, u32, ServerEnd<FileMarker>)>) -> GetClientRes,
    executor: impl FnOnce(&mut FnMut() -> Poll<((), ())>),
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

    let future = server_wrapper.join(client);

    // As our clients are async generators, we need to pin this future explicitly.
    // All async generators are !Unpin by default.
    pin_mut!(future);
    let mut obj = LocalFutureObj::new(future);
    executor(&mut || exec.run_until_stalled(&mut obj));
}
