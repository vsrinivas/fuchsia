// Copyright 2018 The Fuchsia Authors. All rights reserved.
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

use {
    crate::{common::AsyncFnOnce, directory::entry::DirectoryEntry},
    byteorder::{LittleEndian, WriteBytesExt},
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, MAX_FILENAME},
    fuchsia_async::Executor,
    futures::{channel::mpsc, future::join, select, StreamExt},
    std::{future::Future, io::Write, iter, task::Poll},
    void::unreachable,
};

/// A helper to run a pseudo fs server and a client that needs to talk to this server.  This
/// function will create a channel and will pass the client side to `get_client`, while the server
/// side will be passed into an `open()` method on the server.  The server and the client will then
/// be executed on the same single threaded executor until they both stall, then it is asserted
/// that execution is complete and the future has returned. The server is wrapped in a wrapper that
/// will return if `is_terminated` returns true.
///
/// `flags` is passed into the `open()` call.
///
/// See also [`run_server_client_with_mode()`] and
/// [`run_server_client_with_open_requests_channel`].
pub fn run_server_client<GetClientRes>(
    flags: u32,
    server: impl DirectoryEntry,
    get_client: impl FnOnce(DirectoryProxy) -> GetClientRes,
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
/// the execution of the futures. A closure is taken, which is given a reference to
/// run_until_stalled, and asserts that the future either completed or it didn't, depending on the
/// provided boolean.
pub fn run_server_client_with_executor<GetClientRes>(
    flags: u32,
    exec: Executor,
    server: impl DirectoryEntry,
    get_client: impl FnOnce(DirectoryProxy) -> GetClientRes,
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
    get_client: impl FnOnce(DirectoryProxy) -> GetClientRes,
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

// helper to prevent unnecessary monomorphization
fn run_server_client_with_mode_and_executor_dyn<'a>(
    flags: u32,
    mode: u32,
    mut exec: Executor,
    mut server: Box<dyn DirectoryEntry + 'a>,
    get_client: AsyncFnOnce<'a, DirectoryProxy, ()>,
    executor: Box<dyn FnOnce(&mut dyn FnMut(bool)) + 'a>,
) {
    let (client_proxy, server_end) =
        create_proxy::<DirectoryMarker>().expect("Failed to create connection endpoints");

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

/// Holds arguments for a [`DirectoryEntry::open()`] call.
pub type OpenRequestArgs<'path> =
    (u32, u32, Box<dyn Iterator<Item = &'path str>>, ServerEnd<DirectoryMarker>);

/// The sender end of a channel to proxy open requests.
pub type OpenRequestSender<'path> = mpsc::Sender<OpenRequestArgs<'path>>;

/// Similar to [`run_server_client()`] but does not automatically connect the server and the client
/// code.  Instead the client receives a sender end of an [`OpenRequestArgs`] queue, capable of
/// receiving arguments for the `open()` calls on the server.  This way the client can control when
/// the first `open()` call will happen on the server and/or perform additional `open()` calls on
/// the server.  When [`run_server_client()`] is used, the only way to gen a new connection to the
/// server is to `clone()` the existing one, which might be undesirable for a particular test.
pub fn run_server_client_with_open_requests_channel<'path, GetClientRes>(
    server: impl DirectoryEntry,
    get_client: impl FnOnce(OpenRequestSender<'path>) -> GetClientRes,
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
/// execution order.  This is necessary when the test needs to make sure that both the server and
/// the client have reached a particular point.  In order to control the execution you would want
/// to share a oneshot channel or a queue between your test code and the executor closures.  The
/// executor closure get a `run_until_stalled_assert` as an argument.  It can use those channels
/// and `run_until_stalled_assert` to control the execution process of the client and the server.
/// `run_until_stalled_assert` asserts whether or not the future completed on that run according to
/// the provided boolean argument.
///
/// For example, a client that wants to make sure that it receives a particular response from the
/// server by certain point, in case the response is asynchronous.
///
/// The server is wrapped in an async block that returns if it's `is_terminated` method returns
/// true.
pub fn run_server_client_with_open_requests_channel_and_executor<'path, GetClientRes>(
    exec: Executor,
    server: impl DirectoryEntry,
    get_client: impl FnOnce(OpenRequestSender<'path>) -> GetClientRes,
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

// helper to prevent monomorphization
fn run_server_client_with_open_requests_channel_and_executor_dyn<'a, 'path: 'a>(
    mut exec: Executor,
    mut server: Box<dyn DirectoryEntry + 'a>,
    get_client: AsyncFnOnce<'a, OpenRequestSender<'path>, ()>,
    executor: Box<dyn FnOnce(&mut dyn FnMut(bool)) + 'a>,
) {
    let (open_requests_tx, open_requests_rx) = mpsc::channel::<OpenRequestArgs<'path>>(0);

    let server_wrapper = async move {
        let mut open_requests_rx = open_requests_rx.fuse();
        loop {
            select! {
                x = server => unreachable(x),
                open_req = open_requests_rx.next() => {
                    if let Some((flags, mode, mut path, server_end)) = open_req {
                        server
                            .open(flags, mode, &mut path,
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
        use $crate::directory::test_utils::reexport::*;

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
        use $crate::directory::test_utils::reexport::*;

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
        use $crate::directory::test_utils::reexport::*;

        let (_watcher_client, watcher_server) = zx::Channel::create().unwrap();

        let status = $proxy.watch($mask, 0, watcher_server).await.expect("watch failed");
        assert_eq!(Status::from_raw(status), $expected_status);
    }};
}

#[macro_export]
macro_rules! assert_watcher_one_message_watched_events {
    ($watcher:expr, $( { $type:tt, $name:expr $(,)* } ),* $(,)*) => {{
        use $crate::directory::test_utils::reexport::*;

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
