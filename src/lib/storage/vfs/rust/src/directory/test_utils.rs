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

use crate::{
    directory::entry::DirectoryEntry,
    test_utils::run::{self, AsyncServerClientTestParams},
};

use {
    byteorder::{LittleEndian, WriteBytesExt},
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, MAX_FILENAME},
    futures::Future,
    std::{io::Write, sync::Arc},
};

pub use run::{run_client, test_client};

/// A thin wrapper around [`test_utils::run::run_server_client`] that sets the `Marker` to be
/// [`DirectoryMarker`], and providing explicit type for the `get_client` closure argument.  This
/// makes it possible for the caller not to provide explicit types.
pub fn run_server_client<GetClientRes>(
    flags: u32,
    server: Arc<dyn DirectoryEntry>,
    get_client: impl FnOnce(DirectoryProxy) -> GetClientRes,
) where
    GetClientRes: Future<Output = ()>,
{
    run::run_server_client::<DirectoryMarker, _, _>(flags, server, get_client)
}

/// A thin wrapper around [`test_utils::run::test_server_client`] that sets the `Marker` to be
/// [`DirectoryMarker`], and providing explicit type for the `get_client` closure argument.  This
/// makes it possible for the caller not to provide explicit types.
pub fn test_server_client<'test_refs, GetClientRes>(
    flags: u32,
    server: Arc<dyn DirectoryEntry>,
    get_client: impl FnOnce(DirectoryProxy) -> GetClientRes + 'test_refs,
) -> AsyncServerClientTestParams<'test_refs, DirectoryMarker>
where
    GetClientRes: Future<Output = ()> + 'test_refs,
{
    run::test_server_client::<DirectoryMarker, _, _>(flags, server, get_client)
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
            name.len() <= MAX_FILENAME as usize,
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

        assert!(bytes == *expected,
                "Received buffer does not match the expectation.\n\
                 Expected: {:X?}\n\
                 Received: {:X?}\n\
                 Expected as UTF-8 lossy: {:?}\n\
                 Received as UTF-8 lossy: {:?}",
                *expected, bytes,
                String::from_utf8_lossy(expected), String::from_utf8_lossy(&bytes));
    }};

    (@expand_event_type EXISTING) => { WATCH_EVENT_EXISTING };
    (@expand_event_type IDLE) => { WATCH_EVENT_IDLE };
    (@expand_event_type ADDED) => { WATCH_EVENT_ADDED };
    (@expand_event_type REMOVED) => { WATCH_EVENT_REMOVED };
}
