// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common utilities used by pseudo-file related tests.
//!
//! Most assertions are macros as they need to call async functions themselves.  As a typical test
//! will have multiple assertions, it save a bit of typing to write `assert_something!(arg)`
//! instead of `await!(assert_something(arg))`.

use crate::{directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path};

use {
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io::{FileMarker, FileProxy},
    fuchsia_async::Executor,
    futures::{Future, Poll},
    std::{pin::Pin, sync::Arc},
};

/// A helper to connect a pseudo fs server to a client and the run the client on a single threaded
/// executor. Execution is run until the executor reports the execution has stalled. The client
/// must complete it's execution at this point. It is a failure if the client is stalled.
///
/// See also [`run_server_client_with_mode()`], [`run_server_client_with_executor()`], and
/// [`run_server_client_with_mode_and_executor()`]. And also the `_client` family:
/// [`run_client()`], and [`run_client_with_executor()`].
pub fn run_server_client<GetClientRes>(
    flags: u32,
    server: Arc<DirectoryEntry>,
    get_client: impl FnOnce(FileProxy) -> GetClientRes,
) where
    GetClientRes: Future<Output = ()>,
{
    run_server_client_with_mode(flags, 0, server, get_client)
}

/// Similar to [`run_server_client()`] but does not automatically connect the server and the client.
/// The client is expected to connect to the server on it's own.  Otherwise behaviour is the same
/// as for [`run_server_client()`], except the executor is not implicit, but is specified via the
/// `exec` argument.
/// This way the client can control when the first `open()` call will happen on the server and/or
/// perform additional `open()` calls on the server.  With [`run_server_client()`] the first call
/// to `open()` is already finished by the time client code starts running.
/// server is to `clone()` the existing one, which might be undesirable for a particular test.
pub fn run_client<GetClientRes>(exec: Executor, get_client: impl FnOnce() -> GetClientRes)
where
    GetClientRes: Future<Output = ()>,
{
    run_client_with_executor(exec, get_client, |run_until_stalled_assert| {
        run_until_stalled_assert(true)
    })
}

/// Similar to [`run_server_client()`] except that allows to specify the `mode` argument value to
/// the `open()` call. See [`run_server_client()`] for details.
pub fn run_server_client_with_mode<GetClientRes>(
    flags: u32,
    mode: u32,
    server: Arc<DirectoryEntry>,
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
/// the execution of the test via the `coordinator`. A `coordinator` is given a reference to
/// `run_until_stalled` that asserts that the test should complete at a certain point or just
/// stall, depending on the provided boolean.
pub fn run_server_client_with_executor<GetClientRes>(
    flags: u32,
    exec: Executor,
    server: Arc<DirectoryEntry>,
    get_client: impl FnOnce(FileProxy) -> GetClientRes,
    coordinator: impl FnOnce(&mut FnMut(bool) -> ()),
) where
    GetClientRes: Future<Output = ()>,
{
    run_server_client_with_mode_and_executor(flags, 0, exec, server, get_client, coordinator);
}

/// Similar to [`run_server_client()`], adding the additional functionality of
/// [`run_server_client_with_mode()`] and [`run_server_client_with_executor()`].
pub fn run_server_client_with_mode_and_executor<'a, GetClientRes>(
    flags: u32,
    mode: u32,
    exec: Executor,
    server: Arc<DirectoryEntry>,
    get_client: impl FnOnce(FileProxy) -> GetClientRes + 'a,
    coordinator: impl FnOnce(&mut FnMut(bool) -> ()) + 'a,
) where
    GetClientRes: Future<Output = ()> + 'a,
{
    run_server_client_with_mode_and_executor_dyn(
        flags,
        mode,
        exec,
        server,
        Box::new(|proxy| Box::pin(get_client(proxy))),
        Box::new(coordinator),
    )
}

// Helper for `run_server_client_with_mode_and_executor` which uses trait objects rather than
// monomorphization to minimize code size and compile time.
pub fn run_server_client_with_mode_and_executor_dyn<'a>(
    flags: u32,
    mode: u32,
    mut exec: Executor,
    server: Arc<DirectoryEntry>,
    get_client: Box<dyn FnOnce(FileProxy) -> Pin<Box<dyn Future<Output = ()> + 'a>> + 'a>,
    coordinator: Box<dyn FnOnce(&mut FnMut(bool) -> ()) + 'a>,
) {
    let (client_proxy, server_end) =
        create_proxy::<FileMarker>().expect("Failed to create connection endpoints");

    let scope = ExecutionScope::new(Box::new(exec.ehandle()));
    server.open(scope, flags, mode, Path::empty(), server_end.into_channel().into());

    let mut client = get_client(client_proxy);

    // TODO: How to limit the execution time?  run_until_stalled() does not trigger timers, so
    // I can not do this:
    //
    //   let timeout = 300.millis();
    //   let client = client.on_timeout(
    //       timeout.after_now(),
    //       || panic!("Test did not finish in {}ms", timeout.millis()));

    coordinator(&mut |should_complete| {
        if should_complete {
            assert_eq!(
                exec.run_until_stalled(&mut client),
                Poll::Ready(()),
                "client did not complete"
            );
        } else {
            assert_eq!(
                exec.run_until_stalled(&mut client),
                Poll::Pending,
                "client was not expected to complete"
            );
        }
    });
}

pub fn run_client_with_executor<'a, GetClientRes>(
    exec: Executor,
    get_client: impl FnOnce() -> GetClientRes + 'a,
    coordinator: impl FnOnce(&mut FnMut(bool) -> ()) + 'a,
) where
    GetClientRes: Future<Output = ()> + 'a,
{
    run_client_with_executor_dyn(exec, Box::new(|| Box::pin(get_client())), Box::new(coordinator))
}

pub fn run_client_with_executor_dyn<'a>(
    mut exec: Executor,
    get_client: Box<dyn FnOnce() -> Pin<Box<dyn Future<Output = ()> + 'a>> + 'a>,
    coordinator: Box<dyn FnOnce(&mut FnMut(bool) -> ()) + 'a>,
) {
    let mut client = get_client();

    // TODO: How to limit the execution time?  run_until_stalled() does not trigger timers, so
    // I can not do this:
    //
    //   let timeout = 300.millis();
    //   let client = client.on_timeout(
    //       timeout.after_now(),
    //       || panic!("Test did not finish in {}ms", timeout.millis()));

    coordinator(&mut |should_complete| {
        if should_complete {
            assert_eq!(
                exec.run_until_stalled(&mut client),
                Poll::Ready(()),
                "client did not complete"
            );
        } else {
            assert_eq!(
                exec.run_until_stalled(&mut client),
                Poll::Pending,
                "client was not expected to complete"
            );
        }
    });
}
