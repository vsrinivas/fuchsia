// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common utilities used by directory related tests.
//!
//! Most assertions are macros as they need to call async functions themselves.  As a typical test
//! will have multiple assertions, it save a bit of typing to write `assert_something!(arg)`
//! instead of `assert_something(arg).await`.

#[doc(hidden)]
pub mod reexport {
    pub use {
        fidl_fuchsia_io::{
            WatchedEvent, WATCH_EVENT_ADDED, WATCH_EVENT_EXISTING, WATCH_EVENT_IDLE,
            WATCH_EVENT_REMOVED, WATCH_MASK_ADDED, WATCH_MASK_EXISTING, WATCH_MASK_IDLE,
            WATCH_MASK_REMOVED,
        },
        fuchsia_async::Channel,
        fuchsia_zircon::{self as zx, MessageBuf, Status},
    };
}

use crate::{directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path};

use {
    byteorder::{LittleEndian, WriteBytesExt},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, MAX_FILENAME},
    fuchsia_async::Executor,
    futures::{Future, Poll},
    std::{io::Write, pin::Pin, sync::Arc},
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
    server: Arc<dyn DirectoryEntry>,
    get_client: impl FnOnce(DirectoryProxy) -> GetClientRes,
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
    server: Arc<dyn DirectoryEntry>,
    get_client: impl FnOnce(DirectoryProxy) -> GetClientRes,
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
    server: Arc<dyn DirectoryEntry>,
    get_client: impl FnOnce(DirectoryProxy) -> GetClientRes,
    coordinator: impl FnOnce(&mut dyn FnMut(bool) -> ()),
) where
    GetClientRes: Future<Output = ()>,
{
    run_server_client_with_mode_and_executor(flags, 0, exec, server, get_client, coordinator);
}

/// Similar to [`run_server_client()`], adding the additional functionality of
/// [`run_server_client_with_mode()`] and [`run_server_client_with_executor()`].
pub fn run_server_client_with_mode_and_executor<GetClientRes>(
    flags: u32,
    mode: u32,
    exec: Executor,
    server: Arc<dyn DirectoryEntry>,
    get_client: impl FnOnce(DirectoryProxy) -> GetClientRes,
    coordinator: impl FnOnce(&mut dyn FnMut(bool) -> ()),
) where
    GetClientRes: Future<Output = ()>,
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
    server: Arc<dyn DirectoryEntry>,
    get_client: Box<dyn FnOnce(DirectoryProxy) -> Pin<Box<dyn Future<Output = ()> + 'a>> + 'a>,
    coordinator: Box<dyn FnOnce(&mut dyn FnMut(bool) -> ()) + 'a>,
) {
    let (client_proxy, server_end) =
        create_proxy::<DirectoryMarker>().expect("Failed to create connection endpoints");

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

pub fn run_client_with_executor<GetClientRes>(
    exec: Executor,
    get_client: impl FnOnce() -> GetClientRes,
    coordinator: impl FnOnce(&mut dyn FnMut(bool) -> ()),
) where
    GetClientRes: Future<Output = ()>,
{
    run_client_with_executor_dyn(exec, Box::new(|| Box::pin(get_client())), Box::new(coordinator))
}

pub fn run_client_with_executor_dyn<'a>(
    mut exec: Executor,
    get_client: Box<dyn FnOnce() -> Pin<Box<dyn Future<Output = ()> + 'a>> + 'a>,
    coordinator: Box<dyn FnOnce(&mut dyn FnMut(bool) -> ()) + 'a>,
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

/// A helper to build the "expected" output for a `ReadDirents` call from the Directory protocol in
/// io.fidl.
pub struct DirentsSameInodeBuilder {
    expected: Vec<u8>,
    inode: u64,
}

impl DirentsSameInodeBuilder {
    pub fn new(inode: u64) -> Self {
        DirentsSameInodeBuilder { expected: vec![], inode }
    }

    pub fn add(&mut self, type_: u8, name: &[u8]) -> &mut Self {
        assert!(
            name.len() < MAX_FILENAME as usize,
            "Expected entry name should not exceed MAX_FILENAME ({}) bytes.\n\
             Got: {:?}\n\
             Length: {} bytes",
            MAX_FILENAME,
            name,
            name.len()
        );

        self.expected.write_u64::<LittleEndian>(self.inode).unwrap();
        self.expected.write_u8(name.len() as u8).unwrap();
        self.expected.write_u8(type_).unwrap();
        self.expected.write(name).unwrap();

        self
    }

    pub fn into_vec(self) -> Vec<u8> {
        self.expected
    }
}

/// Calls `rewind` on the provided `proxy`, checking that the result status is Status::OK.
#[macro_export]
macro_rules! assert_rewind {
    ($proxy:expr) => {{
        use $crate::directory::test_utils::reexport::Status;

        let status = $proxy.rewind().await.expect("rewind failed");
        assert_eq!(Status::from_raw(status), Status::OK);
    }};
}

/// Opens the specified path as a file and checks its content.  Also see all the `assert_*` macros
/// in `../test_utils.rs`.
#[macro_export]
macro_rules! open_as_file_assert_content {
    ($proxy:expr, $flags:expr, $path:expr, $expected_content:expr) => {{
        let file = open_get_file_proxy_assert_ok!($proxy, $flags, $path);
        assert_read!(file, $expected_content);
        assert_close!(file);
    }};
}

#[macro_export]
macro_rules! assert_watch {
    ($proxy:expr, $mask:expr) => {{
        use $crate::directory::test_utils::reexport::{zx, Channel, Status};

        let (watcher_client, watcher_server) = zx::Channel::create().unwrap();
        let watcher_client = Channel::from_channel(watcher_client).unwrap();

        let status = $proxy.watch($mask, 0, watcher_server).await.expect("watch failed");
        assert_eq!(Status::from_raw(status), Status::OK);

        watcher_client
    }};
}

#[macro_export]
macro_rules! assert_watch_err {
    ($proxy:expr, $mask:expr, $expected_status:expr) => {{
        use $crate::directory::test_utils::reexport::{zx, Status};

        let (_watcher_client, watcher_server) = zx::Channel::create().unwrap();

        let status = $proxy.watch($mask, 0, watcher_server).await.expect("watch failed");
        assert_eq!(Status::from_raw(status), $expected_status);
    }};
}

#[macro_export]
macro_rules! assert_watcher_one_message_watched_events {
    ($watcher:expr, $( { $type:tt, $name:expr $(,)* } ),* $(,)*) => {{
        #[allow(unused)]
        use $crate::directory::test_utils::reexport::{MessageBuf, WATCH_EVENT_EXISTING,
            WATCH_EVENT_IDLE, WATCH_EVENT_ADDED, WATCH_EVENT_REMOVED};

        let mut buf = MessageBuf::new();
        $watcher.recv_msg(&mut buf).await.unwrap();

        let (bytes, handles) = buf.split();
        assert_eq!(
            handles.len(),
            0,
            "Received buffer with handles.\n\
             Handle count: {}\n\
             Buffer: {:X?}",
            handles.len(),
            bytes
        );

        let expected = &mut vec![];
        $({
            let type_ = assert_watcher_one_message_watched_events!(@expand_event_type $type);
            let name = Vec::<u8>::from($name);
            assert!(name.len() <= std::u8::MAX as usize);

            expected.push(type_);
            expected.push(name.len() as u8);
            expected.extend_from_slice(&name);
        })*

        assert_eq!(bytes, *expected, "Received buffer does not match the expected");
    }};

    (@expand_event_type EXISTING) => { WATCH_EVENT_EXISTING };
    (@expand_event_type IDLE) => { WATCH_EVENT_IDLE };
    (@expand_event_type ADDED) => { WATCH_EVENT_ADDED };
    (@expand_event_type REMOVED) => { WATCH_EVENT_REMOVED };
}
