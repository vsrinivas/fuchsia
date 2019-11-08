// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common utilities used by pseudo-file related tests.

use {
    crate::{common::AsyncFnOnce, directory::entry::DirectoryEntry},
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_io::{FileMarker, FileProxy},
    fuchsia_async::Executor,
    futures::{channel::mpsc, future::join, select, StreamExt},
    std::{future::Future, iter, task::Poll},
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
    let exec = Executor::new().expect("Executor creation failed");

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
    exec: Executor,
    server: impl DirectoryEntry,
    get_client: impl FnOnce(FileProxy) -> GetClientRes,
    executor: impl FnOnce(&mut dyn FnMut(bool) -> ()),
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
    exec: Executor,
    server: impl DirectoryEntry,
    get_client: impl FnOnce(FileProxy) -> GetClientRes,
    executor: impl FnOnce(&mut dyn FnMut(bool)),
) where
    GetClientRes: Future<Output = ()>,
{
    run_server_client_with_mode_and_executor_dyn(
        flags,
        mode,
        exec,
        Box::new(server),
        Box::new(|proxy| Box::pin(get_client(proxy))),
        Box::new(executor),
    )
}

// helper to avoid monomorphization
fn run_server_client_with_mode_and_executor_dyn<'a>(
    flags: u32,
    mode: u32,
    mut exec: Executor,
    mut server: Box<dyn DirectoryEntry + 'a>,
    get_client: AsyncFnOnce<'a, FileProxy, ()>,
    executor: Box<dyn FnOnce(&mut dyn FnMut(bool)) + 'a>,
) {
    let (client_proxy, server_end) =
        create_proxy::<FileMarker>().expect("Failed to create connection endpoints");

    server.open(flags, mode, &mut iter::empty(), server_end.into_channel().into());

    let client = get_client(client_proxy);

    // This wrapper lets us poll the server while also completing the server future if it's
    // is_terminated returns true, even though it's poll will never return Ready.
    let server_wrapper = async move {
        loop {
            select! {
                x = server => unreachable(x),
                complete => break,
            }
        }
    };

    let mut future = Box::pin(join(server_wrapper, client));

    // TODO: How to limit the execution time?  run_until_stalled() does not trigger timers, so
    // I can not do this:
    //
    //   let timeout = 300.millis();
    //   let future = future.on_timeout(
    //       timeout.after_now(),
    //       || panic!("Test did not finish in {}ms", timeout.millis()));

    executor(&mut |should_complete| {
        if should_complete {
            assert_eq!(
                exec.run_until_stalled(&mut future),
                Poll::Ready(((), ())),
                "future did not complete"
            );
        } else {
            assert_eq!(
                exec.run_until_stalled(&mut future),
                Poll::Pending,
                "future was not expected to complete"
            );
        }
    });
}

/// Holds arguments for a [`DirectoryEntry::open()`] call, assuming empty path.
pub type OpenRequestArgs = (u32, u32, ServerEnd<FileMarker>);

/// The sender end of a channel to proxy open requests.
pub type OpenRequestSender = mpsc::Sender<OpenRequestArgs>;

/// Similar to [`run_server_client()`] but does not automatically connect the server and the client
/// code.  Instead the client receives a sender end of an [`OpenRequestArgs`] queue, capable of
/// receiving arguments for the `open()` calls on the server.  This way the client can control when
/// the first `open()` call will happen on the server and/or perform additional `open()` calls on
/// the server.  When [`run_server_client()`] is used, the only way to gen a new connection to the
/// server is to `clone()` the existing one, which might be undesirable for a particular test.
pub fn run_server_client_with_open_requests_channel<GetClientRes>(
    server: impl DirectoryEntry,
    get_client: impl FnOnce(OpenRequestSender) -> GetClientRes,
) where
    GetClientRes: Future<Output = ()>,
{
    let exec = Executor::new().expect("Executor creation failed");

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
    exec: Executor,
    server: impl DirectoryEntry,
    get_client: impl FnOnce(OpenRequestSender) -> GetClientRes,
    executor: impl FnOnce(&mut dyn FnMut(bool)),
) where
    GetClientRes: Future<Output = ()>,
{
    run_server_client_with_open_requests_channel_and_executor_dyn(
        exec,
        Box::new(server),
        Box::new(|sender| Box::pin(get_client(sender))),
        Box::new(executor),
    )
}

// helper to avoid monomorphization
fn run_server_client_with_open_requests_channel_and_executor_dyn<'a>(
    mut exec: Executor,
    mut server: Box<dyn DirectoryEntry + 'a>,
    get_client: AsyncFnOnce<'a, OpenRequestSender, ()>,
    executor: Box<dyn FnOnce(&mut dyn FnMut(bool)) + 'a>,
) {
    let (open_requests_tx, open_requests_rx) = mpsc::channel::<OpenRequestArgs>(0);

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
    let mut future = Box::pin(join(server_wrapper, client));

    executor(&mut |should_complete| {
        if should_complete {
            assert_eq!(
                exec.run_until_stalled(&mut future),
                Poll::Ready(((), ())),
                "future did not complete"
            );
        } else {
            assert_eq!(
                exec.run_until_stalled(&mut future),
                Poll::Pending,
                "future was not expected to complete"
            );
        }
    });
}
