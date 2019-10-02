// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities used by tests in both file and directory modules.

#[doc(hidden)]
pub mod reexport {
    pub use {
        crate::directory::test_utils::DirentsSameInodeBuilder,
        fidl_fuchsia_io::{
            DirectoryEvent, DirectoryMarker, DirectoryObject, FileEvent, FileMarker, FileObject,
            NodeInfo, SeekOrigin, DIRENT_TYPE_BLOCK_DEVICE, DIRENT_TYPE_DIRECTORY,
            DIRENT_TYPE_FILE, DIRENT_TYPE_SERVICE, DIRENT_TYPE_SOCKET, DIRENT_TYPE_UNKNOWN,
            INO_UNKNOWN, OPEN_FLAG_DESCRIBE, OPEN_RIGHT_READABLE,
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
// `assert_read(...).await`, while with a macro it is `assert_read!(...)`.  As this is local to
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
pub fn clone_get_proxy<M, Proxy>(proxy: &Proxy, flags: u32) -> M::Proxy
where
    M: ServiceMarker,
    Proxy: ClonableProxy,
{
    let (new_proxy, new_server_end) =
        create_proxy::<M>().expect("Failed to create connection endpoints");

    proxy.clone(flags, new_server_end.into_channel().into()).unwrap();

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
        use $crate::test_utils::reexport::Status;

        let (status, content) = $proxy.read($expected.len() as u64).await.expect("read failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(content.as_slice(), $expected.as_bytes());
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_read_err {
    ($proxy:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::Status;

        let (status, content) = $proxy.read(100).await.expect("read failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(content.len(), 0);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_read_fidl_err {
    ($proxy:expr, $expected_error:pat) => {{
        match $proxy.read(100).await {
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
macro_rules! assert_read_fidl_err_closed {
    ($proxy:expr) => {{
        match $proxy.read(100).await {
            Err(error) if error.is_closed() => (),
            Err(error) => panic!("read() returned unexpected error: {:?}", error),
            Ok((status, content)) => {
                panic!("Read succeeded: status: {:?}, content: '{:?}'", status, content)
            }
        }
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_get_buffer {
    ($proxy:expr, $expected:expr) => {{
        use $crate::test_utils::reexport::{Status, OPEN_RIGHT_READABLE};

        let (status, buffer) =
            $proxy.get_buffer(OPEN_RIGHT_READABLE).await.expect("get buffer failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert!(buffer.is_some());
        let buffer = buffer.unwrap();

        let mut buf = vec![0; buffer.size as usize];
        buffer.vmo.read(&mut buf, 0).expect("VMO read failed");
        assert_eq!(buf.as_slice(), $expected.as_bytes());
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_get_buffer_err {
    ($proxy:expr, $flags:expr, $expected_status: expr) => {{
        use $crate::test_utils::reexport::Status;

        let (status, buffer) = $proxy.get_buffer($flags).await.expect("get buffer failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert!(buffer.is_none());
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_read_at {
    ($proxy:expr, $offset:expr, $expected:expr) => {{
        use $crate::test_utils::reexport::Status;

        let (status, content) =
            $proxy.read_at($expected.len() as u64, $offset).await.expect("read failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(content.as_slice(), $expected.as_bytes());
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_read_at_err {
    ($proxy:expr, $offset:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::Status;

        let (status, content) = $proxy.read_at(100, $offset).await.expect("read failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(content.len(), 0);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_write {
    ($proxy:expr, $content:expr) => {{
        use $crate::test_utils::reexport::Status;

        let (status, len_written) =
            $proxy.write(&mut $content.bytes()).await.expect("write failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(len_written, $content.len() as u64);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_write_err {
    ($proxy:expr, $content:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::Status;

        let (status, len_written) =
            $proxy.write(&mut $content.bytes()).await.expect("write failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(len_written, 0);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_write_fidl_err {
    ($proxy:expr, $content:expr, $expected_error:pat) => {
        match $proxy.write(&mut $content.bytes()).await {
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
macro_rules! assert_write_fidl_err_closed {
    ($proxy:expr, $content:expr) => {
        match $proxy.write(&mut $content.bytes()).await {
            Err(error) if error.is_closed() => (),
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
        use $crate::test_utils::reexport::Status;

        let (status, len_written) =
            $proxy.write_at(&mut $content.bytes(), $offset).await.expect("write failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(len_written, $content.len() as u64);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_write_at_err {
    ($proxy:expr, $offset:expr, $content:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::Status;

        let (status, len_written) =
            $proxy.write_at(&mut $content.bytes(), $offset).await.expect("write failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(len_written, 0);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_seek {
    ($proxy:expr, $pos:expr, Start) => {{
        use $crate::test_utils::reexport::{SeekOrigin, Status};

        let (status, actual) = $proxy.seek($pos, SeekOrigin::Start).await.expect("seek failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(actual, $pos);
    }};
    ($proxy:expr, $pos:expr, $start:ident, $expected:expr) => {{
        use $crate::test_utils::reexport::{SeekOrigin, Status};

        let (status, actual) = $proxy.seek($pos, SeekOrigin::$start).await.expect("seek failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(actual, $expected);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_seek_err {
    ($proxy:expr, $pos:expr, $start:ident, $expected_status:expr, $actual_pos:expr) => {{
        use $crate::test_utils::reexport::{SeekOrigin, Status};

        let (status, actual) = $proxy.seek($pos, SeekOrigin::$start).await.expect("seek failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(actual, $actual_pos);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_truncate {
    ($proxy:expr, $length:expr) => {{
        use $crate::test_utils::reexport::Status;

        let status = $proxy.truncate($length).await.expect("truncate failed");

        assert_eq!(Status::from_raw(status), Status::OK);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_truncate_err {
    ($proxy:expr, $length:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::Status;

        let status = $proxy.truncate($length).await.expect("truncate failed");

        assert_eq!(Status::from_raw(status), $expected_status);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_get_attr {
    ($proxy:expr, $expected:expr) => {{
        use $crate::test_utils::reexport::Status;

        let (status, attrs) = $proxy.get_attr().await.expect("get_attr failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(attrs, $expected);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_describe {
    ($proxy:expr, $expected:expr) => {
        let node_info = $proxy.describe().await.expect("describe failed");
        assert_eq!(node_info, $expected);
    };
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_close {
    ($proxy:expr) => {{
        use $crate::test_utils::reexport::Status;

        let status = $proxy.close().await.expect("close failed");

        assert_eq!(Status::from_raw(status), Status::OK);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_close_err {
    ($proxy:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::Status;

        let status = $proxy.close().await.expect("close failed");

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
        use $crate::test_utils::reexport::StreamExt;

        let event_stream = $proxy.take_event_stream();
        match event_stream.into_future().await {
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
        use $crate::test_utils::reexport::StreamExt;

        let event_stream = $proxy.take_event_stream();
        match event_stream.into_future().await {
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
        use $crate::test_utils::reexport::{FileEvent, FileMarker, FileObject, NodeInfo, Status};

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
        use $crate::test_utils::reexport::{FileEvent, FileMarker, Status};

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
        use $crate::test_utils::reexport::{
            DirectoryEvent, DirectoryMarker, DirectoryObject, NodeInfo, Status,
        };

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
        use $crate::test_utils::reexport::{DirectoryEvent, DirectoryMarker, Status};

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
        let new_proxy = $crate::test_utils::clone_get_proxy::<$new_proxy_type, _>($proxy, $flags);
        assert_event!(new_proxy, $expected_pattern, $expected_assertion);
        new_proxy
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! clone_get_file_proxy_assert_ok {
    ($proxy:expr, $flags:expr) => {{
        use $crate::test_utils::reexport::{FileEvent, FileMarker, FileObject, NodeInfo, Status};

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
        use $crate::test_utils::reexport::{FileEvent, FileMarker, Status};

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
        use $crate::test_utils::reexport::{
            DirectoryEvent, DirectoryMarker, DirectoryObject, NodeInfo, Status,
        };

        clone_get_proxy_assert!(
            $proxy,
            $flags,
            DirectoryMarker,
            DirectoryEvent::OnOpen_ { s, info },
            {
                assert_eq!(Status::from_raw(s), Status::OK);
                assert_eq!(info, Some(Box::new(NodeInfo::Directory(DirectoryObject))));
            }
        )
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! clone_as_directory_assert_err {
    ($proxy:expr, $flags:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::{DirectoryEvent, DirectoryMarker, Status};

        clone_get_proxy_assert!(
            $proxy,
            $flags,
            DirectoryMarker,
            DirectoryEvent::OnOpen_ { s, info },
            {
                assert_eq!(Status::from_raw(s), $expected_status);
                assert_eq!(info, None);
            }
        )
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_read_dirents {
    ($proxy:expr, $max_bytes:expr, $expected:expr) => {{
        use $crate::test_utils::reexport::Status;

        let (status, entries) = $proxy.read_dirents($max_bytes).await.expect("read_dirents failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(entries, $expected);
    }};
}

#[macro_export]
macro_rules! assert_read_dirents_one_listing {
    ($proxy:expr, $max_bytes:expr, $( { $type:tt, $name:expr $(,)* } ),* $(,)*) => {{
        use $crate::test_utils::reexport::{DirentsSameInodeBuilder, INO_UNKNOWN};

        #[allow(unused)]
        use $crate::test_utils::reexport::{
            DIRENT_TYPE_UNKNOWN, DIRENT_TYPE_DIRECTORY, DIRENT_TYPE_BLOCK_DEVICE, DIRENT_TYPE_FILE,
            DIRENT_TYPE_SOCKET, DIRENT_TYPE_SERVICE,
        };

        let mut expected = DirentsSameInodeBuilder::new(INO_UNKNOWN);
        expected
            $(.add(assert_read_dirents_one_listing!(@expand_dirent_type $type), $name))*
            ;

        assert_read_dirents!($proxy, $max_bytes, expected.into_vec());
    }};

    (@expand_dirent_type UNKNOWN) => { DIRENT_TYPE_UNKNOWN };
    (@expand_dirent_type DIRECTORY) => { DIRENT_TYPE_DIRECTORY };
    (@expand_dirent_type BLOCK_DEVICE) => { DIRENT_TYPE_BLOCK_DEVICE };
    (@expand_dirent_type FILE) => { DIRENT_TYPE_FILE };
    (@expand_dirent_type SOCKET) => { DIRENT_TYPE_SOCKET };
    (@expand_dirent_type SERVICE) => { DIRENT_TYPE_SERVICE };
}

#[macro_export]
macro_rules! assert_read_dirents_path_one_listing {
    ($proxy:expr, $path:expr, $max_bytes:expr, $( { $type:tt, $name:expr $(,)* } ),* $(,)*) => {{
        use $crate::test_utils::reexport::OPEN_FLAG_DESCRIBE;

        let flags = OPEN_FLAG_DESCRIBE;
        let path = open_get_directory_proxy_assert_ok!($proxy, flags, $path);

        assert_read_dirents_one_listing!(path, $max_bytes, $( { $type, $name }, )*);
        assert_close!(path);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_read_dirents_err {
    ($proxy:expr, $max_bytes:expr, $expected_status:expr) => {{
        use $crate::test_utils::reexport::Status;

        let (status, entries) = $proxy.read_dirents($max_bytes).await.expect("read_dirents failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(entries.len(), 0);
    }};
}

#[macro_export]
macro_rules! vec_string {
    ($($x:expr),*) => (vec![$($x.to_string()),*]);
}
