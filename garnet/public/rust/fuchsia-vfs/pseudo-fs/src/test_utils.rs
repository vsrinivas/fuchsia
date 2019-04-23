// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities used by tests in both file and directory modules.

#![cfg(test)]

#[doc(hidden)]
pub mod reexport {
    pub use {
        fidl_fuchsia_io::{
            DirectoryEvent, DirectoryMarker, FileEvent, FileMarker, FileObject, NodeInfo,
            SeekOrigin,
        },
        fuchsia_zircon::Status,
        futures::stream::StreamExt,
    };
}

use {
    fidl::endpoints::{create_proxy, ServerEnd, ServiceMarker},
    fidl_fuchsia_io::{DirectoryProxy, FileProxy, NodeMarker, NodeProxy},
};

// All of the macros in this file could instead be async functions, but then I would have to say
// `await!(assert_read(...))`, while with a macro it is `assert_read!(...)`.  As this is local to
// the testing part of this module, it is probably OK to use macros to save some repetition.

pub fn open_get_proxy<M>(proxy: &DirectoryProxy, flags: u32, mode: u32, path: &str) -> M::Proxy
where
    M: ServiceMarker,
{
    let (new_proxy, new_server_end) =
        create_proxy::<M>().expect("Failed to create connection endpoints");

    proxy
        .open(flags, mode, path, ServerEnd::<NodeMarker>::new(new_server_end.into_channel()))
        .unwrap();

    new_proxy
}

/// This trait is implemented by all the objects that should be clonable via clone_get_proxy().  In
/// particular NodeProxy, FileProxy and DirectoryProxy.  In FIDL, Node is the protocol that
/// specifies the "Clone()" method.  And File and Directory just compose it in.  So if we would
/// forward this formation from FIDL, then this trait would be unnecessary.
pub trait ClonableProxy {
    fn clone(&self, flags: u32, server_end: ServerEnd<NodeMarker>) -> Result<(), fidl::Error>;
}

/// Calls .clone() on the proxy object, and returns a client side of the connection passed into the
/// clone() method.
pub fn clone_get_proxy<Proxy, M>(proxy: &Proxy, flags: u32) -> M::Proxy
where
    M: ServiceMarker,
    Proxy: ClonableProxy,
{
    let (new_proxy, new_server_end) =
        create_proxy::<M>().expect("Failed to create connection endpoints");

    proxy.clone(flags, ServerEnd::<NodeMarker>::new(new_server_end.into_channel())).unwrap();

    new_proxy
}

impl ClonableProxy for NodeProxy {
    fn clone(&self, flags: u32, server_end: ServerEnd<NodeMarker>) -> Result<(), fidl::Error> {
        NodeProxy::clone(self, flags, server_end)
    }
}

impl ClonableProxy for FileProxy {
    fn clone(&self, flags: u32, server_end: ServerEnd<NodeMarker>) -> Result<(), fidl::Error> {
        FileProxy::clone(self, flags, server_end)
    }
}

impl ClonableProxy for DirectoryProxy {
    fn clone(&self, flags: u32, server_end: ServerEnd<NodeMarker>) -> Result<(), fidl::Error> {
        DirectoryProxy::clone(self, flags, server_end)
    }
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_read {
    ($proxy:expr, $expected:expr) => {{
        use $crate::test_utils::reexport::*;

        let (status, content) = await!($proxy.read($expected.len() as u64)).expect("read failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(content.as_slice(), $expected.as_bytes());
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_read_err {
    ($proxy:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::*;

        let (status, content) = await!($proxy.read(100)).expect("read failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(content.len(), 0);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_read_fidl_err {
    ($proxy:expr, $expected_error:pat) => {{
        use $crate::test_utils::reexport::*;

        match await!($proxy.read(100)) {
            Err($expected_error) => (),
            Err(error) => panic!("read() returned unexpected error: {:?}", error),
            Ok((status, content)) => {
                panic!("Read succeeded: status: {:?}, content: '{:?}'", status, content)
            }
        }
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_read_at {
    ($proxy:expr, $offset:expr, $expected:expr) => {{
        use $crate::test_utils::reexport::*;

        let (status, content) =
            await!($proxy.read_at($expected.len() as u64, $offset)).expect("read failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(content.as_slice(), $expected.as_bytes());
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_read_at_err {
    ($proxy:expr, $offset:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::*;

        let (status, content) = await!($proxy.read_at(100, $offset)).expect("read failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(content.len(), 0);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_write {
    ($proxy:expr, $content:expr) => {{
        use $crate::test_utils::reexport::*;

        let (status, len_written) =
            await!($proxy.write(&mut $content.bytes())).expect("write failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(len_written, $content.len() as u64);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_write_err {
    ($proxy:expr, $content:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::*;

        let (status, len_written) =
            await!($proxy.write(&mut $content.bytes())).expect("write failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(len_written, 0);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_write_fidl_err {
    ($proxy:expr, $content:expr, $expected_error:pat) => {
        match await!($proxy.write(&mut $content.bytes())) {
            Err($expected_error) => (),
            Err(error) => panic!("write() returned unexpected error: {:?}", error),
            Ok((status, actual)) => {
                panic!("Write succeeded: status: {:?}, actual: '{:?}'", status, actual)
            }
        }
    };
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_write_at {
    ($proxy:expr, $offset:expr, $content:expr) => {{
        use $crate::test_utils::reexport::*;

        let (status, len_written) =
            await!($proxy.write_at(&mut $content.bytes(), $offset)).expect("write failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(len_written, $content.len() as u64);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_write_at_err {
    ($proxy:expr, $offset:expr, $content:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::*;

        let (status, len_written) =
            await!($proxy.write_at(&mut $content.bytes(), $offset)).expect("write failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(len_written, 0);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_seek {
    ($proxy:expr, $pos:expr, Start) => {{
        use $crate::test_utils::reexport::*;

        let (status, actual) = await!($proxy.seek($pos, SeekOrigin::Start)).expect("seek failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(actual, $pos);
    }};
    ($proxy:expr, $pos:expr, $start:ident, $expected:expr) => {{
        use $crate::test_utils::reexport::*;

        let (status, actual) = await!($proxy.seek($pos, SeekOrigin::$start)).expect("seek failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(actual, $expected);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_seek_err {
    ($proxy:expr, $pos:expr, $start:ident, $expected_status:expr, $actual_pos:expr) => {{
        use $crate::test_utils::reexport::*;

        let (status, actual) = await!($proxy.seek($pos, SeekOrigin::$start)).expect("seek failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(actual, $actual_pos);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_truncate {
    ($proxy:expr, $length:expr) => {{
        use $crate::test_utils::reexport::*;

        let status = await!($proxy.truncate($length)).expect("truncate failed");

        assert_eq!(Status::from_raw(status), Status::OK);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_truncate_err {
    ($proxy:expr, $length:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::*;

        let status = await!($proxy.truncate($length)).expect("truncate failed");

        assert_eq!(Status::from_raw(status), $expected_status);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_get_attr {
    ($proxy:expr, $expected:expr) => {{
        use $crate::test_utils::reexport::*;

        let (status, attrs) = await!($proxy.get_attr()).expect("get_attr failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(attrs, $expected);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_describe {
    ($proxy:expr, $expected:expr) => {
        let node_info = await!($proxy.describe()).expect("describe failed");
        assert_eq!(node_info, $expected);
    };
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_close {
    ($proxy:expr) => {{
        use $crate::test_utils::reexport::*;

        let status = await!($proxy.close()).expect("close failed");

        assert_eq!(Status::from_raw(status), Status::OK);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_close_err {
    ($proxy:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::*;

        let status = await!($proxy.close()).expect("close failed");

        assert_eq!(Status::from_raw(status), $expected_status);
    }};
}

// PartialEq is not defined for FileEvent for the moment.
// Because of that I can not write a macro that would just accept a FileEvent instance to
// compare against:
//
//     assert_event!(proxy, FileEvent::OnOpen_ {
//         s: Status::SHOULD_WAIT.into_raw(),
//         info: Some(Box::new(NodeInfo::{File,Directory} { ... })),
//     });
//
// Instead, I need to split the assertion into a pattern and then additional assertions on what
// the pattern have matched.
#[macro_export]
macro_rules! assert_event {
    ($proxy:expr, $expected_pattern:pat, $expected_assertion:block) => {{
        use $crate::test_utils::reexport::*;

        let event_stream = $proxy.take_event_stream();
        match await!(event_stream.into_future()) {
            (Some(Ok($expected_pattern)), _) => $expected_assertion,
            (unexpected, _) => {
                panic!("Unexpected event: {:?}", unexpected);
            }
        }
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_no_event {
    ($proxy:expr) => {{
        use $crate::test_utils::reexport::*;

        let event_stream = $proxy.take_event_stream();
        match await!(event_stream.into_future()) {
            (None, _) => (),
            (unexpected, _) => {
                panic!("Unexpected event: {:?}", unexpected);
            }
        }
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! open_get_proxy_assert {
    ($proxy:expr, $flags:expr, $path:expr, $new_proxy_type:ty, $expected_pattern:pat,
     $expected_assertion:block) => {{
        let new_proxy =
            $crate::test_utils::open_get_proxy::<$new_proxy_type>($proxy, $flags, 0, $path);
        assert_event!(new_proxy, $expected_pattern, $expected_assertion);
        new_proxy
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! open_get_file_proxy_assert_ok {
    ($proxy:expr, $flags:expr, $path:expr) => {{
        use $crate::test_utils::reexport::*;

        open_get_proxy_assert!($proxy, $flags, $path, FileMarker, FileEvent::OnOpen_ { s, info }, {
            assert_eq!(Status::from_raw(s), Status::OK);
            assert_eq!(info, Some(Box::new(NodeInfo::File(FileObject { event: None }))),);
        })
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! open_as_file_assert_err {
    ($proxy:expr, $flags:expr, $path:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::*;

        open_get_proxy_assert!(
            $proxy,
            $flags,
            $path,
            FileMarker,
            FileEvent::OnOpen_ { s, info },
            {
                assert_eq!(Status::from_raw(s), $expected_status);
                assert_eq!(info, None);
            }
        );
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! open_get_directory_proxy_assert_ok {
    ($proxy:expr, $flags:expr, $path:expr) => {{
        use $crate::test_utils::reexport::*;

        open_get_proxy_assert!(
            $proxy,
            $flags,
            $path,
            DirectoryMarker,
            DirectoryEvent::OnOpen_ { s, info },
            {
                assert_eq!(Status::from_raw(s), Status::OK);
                assert_eq!(info, Some(Box::new(NodeInfo::Directory(DirectoryObject))),);
            }
        )
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! open_as_directory_assert_err {
    ($proxy:expr, $flags:expr, $path:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::*;

        open_get_proxy_assert!(
            $proxy,
            $flags,
            $path,
            DirectoryMarker,
            DirectoryEvent::OnOpen_ { s, info },
            {
                assert_eq!(Status::from_raw(s), $expected_status);
                assert_eq!(info, None);
            }
        );
    }};
}

#[macro_export]
macro_rules! clone_get_proxy_assert {
    ($proxy:expr, $flags:expr, $new_proxy_type:ty, $expected_pattern:pat,
     $expected_assertion:block) => {{
        let new_proxy = $crate::test_utils::clone_get_proxy::<_, $new_proxy_type>($proxy, $flags);
        assert_event!(new_proxy, $expected_pattern, $expected_assertion);
        new_proxy
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! clone_get_file_proxy_assert_ok {
    ($proxy:expr, $flags:expr) => {{
        use $crate::test_utils::reexport::*;

        clone_get_proxy_assert!($proxy, $flags, FileMarker, FileEvent::OnOpen_ { s, info }, {
            assert_eq!(Status::from_raw(s), Status::OK);
            assert_eq!(info, Some(Box::new(NodeInfo::File(FileObject { event: None }))),);
        })
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! clone_as_file_assert_err {
    ($proxy:expr, $flags:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::*;

        clone_get_proxy_assert!($proxy, $flags, FileMarker, FileEvent::OnOpen_ { s, info }, {
            assert_eq!(Status::from_raw(s), $expected_status);
            assert_eq!(info, None);
        })
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! clone_get_directory_proxy_assert_ok {
    ($proxy:expr, $flags:expr) => {{
        use $crate::test_utils::reexport::*;

        clone_get_proxy_assert!(
            $proxy,
            $flags,
            DirectoryMarker,
            DirectoryEvent::OnOpen_ { s, info },
            {
                assert_eq!(Status::from_raw(s), Status::OK);
                assert_eq!(info, Some(Box::new(NodeInfo::Directory(DirectoryObject))),);
            }
        )
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! clone_as_directory_assert_err {
    ($proxy:expr, $flags:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::*;

        clone_get_proxy_assert!(
            $proxy,
            $flags,
            DirectoryMarker,
            DirectoryEvent::OnOpen_ { s, info },
            {
                assert_eq!(Status::from_raw(s), $expected_status);
                assert_eq!(info, None);
            }
        );
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_read_dirents {
    ($proxy:expr, $max_bytes:expr, $expected:expr) => {{
        use $crate::test_utils::reexport::*;

        let (status, entries) =
            await!($proxy.read_dirents($max_bytes)).expect("read_dirents failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(entries, $expected);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_read_dirents_err {
    ($proxy:expr, $max_bytes:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::*;

        let (status, entries) =
            await!($proxy.read_dirents($max_bytes)).expect("read_dirents failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(entries.len(), 0);
    }};
}

#[macro_export]
macro_rules! vec_string {
    ($($x:expr),*) => (vec![$($x.to_string()),*]);
}
