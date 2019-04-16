// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common utilities used by directory related tests.
//!
//! Most assertions are macros as they need to call async functions themselves.  As a typical test
//! will have multiple assertions, it save a bit of typing to write `assert_something!(arg)`
//! instead of `await!(assert_something(arg))`.

#![cfg(test)]

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
    crate::directory::entry::DirectoryEntry,
    byteorder::{LittleEndian, WriteBytesExt},
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, NodeMarker, MAX_FILENAME},
    fuchsia_async::Executor,
    futures::channel::mpsc,
    futures::{future::FutureExt, select, stream::StreamExt, Future},
    pin_utils::pin_mut,
    std::{io::Write, iter},
    void::unreachable,
};

/// A helper to run a pseudo fs server and a client that needs to talk to this server.  This
/// function will create a channel and will pass the client side to `get_client`, while the server
/// side will be passed into an `open()` method on the server.  The server and the client will then
/// be executed on the same single threaded executor until they both stall.
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
    mut server: impl DirectoryEntry,
    get_client: impl FnOnce(DirectoryProxy) -> GetClientRes,
) where
    GetClientRes: Future<Output = ()>,
{
    let mut exec = Executor::new().expect("Executor creation failed");

    let (client_proxy, server_end) =
        create_proxy::<DirectoryMarker>().expect("Failed to create connection endpoints");

    server.open(
        flags,
        mode,
        &mut iter::empty(),
        ServerEnd::<NodeMarker>::new(server_end.into_channel()),
    );

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

/// Holds arguments for a [`DirectoryEntry::open()`] call.
pub type OpenRequestArgs<'path> =
    (u32, u32, Box<Iterator<Item = &'path str>>, ServerEnd<DirectoryMarker>);

/// Similar to [`run_server_client()`] but does not automatically connect the server and the client
/// code.  Instead the client receives a sender end of an [`OpenRequestArgs`] queue, capable of
/// receiving arguments for the `open()` calls on the server.  This way the client can control when
/// the first `open()` call will happen on the server and/or perform additional `open()` calls on
/// the server.  When [`run_server_client()`] is used, the only way to gen a new connection to the
/// server is to `clone()` the existing one, which might be undesirable for a particular test.
pub fn run_server_client_with_open_requests_channel<'path, GetClientRes>(
    mut server: impl DirectoryEntry,
    get_client: impl FnOnce(mpsc::Sender<OpenRequestArgs<'path>>) -> GetClientRes,
) where
    GetClientRes: Future<Output = ()>,
{
    let mut exec = Executor::new().expect("Executor creation failed");

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
    let future = server_wrapper.join(client);

    // As our clients are async generators, we need to pin this future explicitly.
    // All async generators are !Unpin by default.
    pin_mut!(future);
    let _ = exec.run_until_stalled(&mut future);
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

        let status = await!($proxy.rewind()).expect("rewind failed");
        assert_eq!(Status::from_raw(status), Status::OK);
    }};
}

/// Opens the specified path as a file and checks its content.  Also see all the `assert_*` macros
/// in `../test_utils.rs`.
#[macro_export]
macro_rules! open_as_file_assert_content {
    ($proxy:expr, $flags:expr, $path:expr, $expected_content:expr) => {
        let file = open_get_file_proxy_assert_ok!($proxy, $flags, $path);
        assert_read!(file, $expected_content);
        assert_close!(file);
    };
}

#[macro_export]
macro_rules! assert_watch {
    ($proxy:expr, $mask:expr) => {{
        use $crate::directory::test_utils::reexport::*;

        let (watcher_client, watcher_server) = zx::Channel::create().unwrap();
        let watcher_client = Channel::from_channel(watcher_client).unwrap();

        let status = await!($proxy.watch($mask, 0, watcher_server)).expect("watch failed");
        assert_eq!(Status::from_raw(status), Status::OK);

        watcher_client
    }};
}

#[macro_export]
macro_rules! assert_watch_err {
    ($proxy:expr, $mask:expr, $expected_status:expr) => {{
        use $crate::directory::test_utils::reexport::*;

        let (_watcher_client, watcher_server) = zx::Channel::create().unwrap();

        let status = await!($proxy.watch($mask, 0, watcher_server)).expect("watch failed");
        assert_eq!(Status::from_raw(status), $expected_status);
    }};
}

#[macro_export]
macro_rules! assert_watcher_one_message_watched_events {
    ($watcher:expr, $( { $type:tt, $name:expr $(,)* } ),* $(,)*) => {{
        use $crate::directory::test_utils::reexport::*;

        let mut buf = MessageBuf::new();
        await!($watcher.recv_msg(&mut buf)).unwrap();

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
