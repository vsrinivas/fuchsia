// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Assertion helpers common to both file and directory tests.

#[doc(hidden)]
pub mod reexport {
    pub use {
        crate::directory::test_utils::DirentsSameInodeBuilder,
        fidl_fuchsia_io::{
            DirectoryEvent, DirectoryMarker, DirectoryObject, FileEvent, FileMarker, FileObject,
            NodeInfo, SeekOrigin, Service, DIRENT_TYPE_BLOCK_DEVICE, DIRENT_TYPE_DIRECTORY,
            DIRENT_TYPE_FILE, DIRENT_TYPE_SERVICE, DIRENT_TYPE_SOCKET, DIRENT_TYPE_UNKNOWN,
            INO_UNKNOWN, OPEN_FLAG_DESCRIBE, OPEN_RIGHT_READABLE, WATCH_EVENT_ADDED,
            WATCH_EVENT_EXISTING, WATCH_EVENT_IDLE, WATCH_EVENT_REMOVED,
        },
        fuchsia_zircon::{MessageBuf, Status},
        futures::stream::StreamExt,
    };
}

// All of the macros in this file should be async functions.  There are two reasons they are not:
//   1. It is shorter to write (`assert_read!(...)` instead of `assert_read(...).await`).
//   2. Until we get proper backtraces we only see the line number of the line with the assertion.
//      So macros produce better error messages.
//
// As soon as the second item is fixed, we should start migrating to functions.  We may consider
// still using "thin" macro wrappers to remove the repetition of `await`. Also, fxbug.dev/34036 is tracking
// compile time degradation due to repetition macros are introducing.

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_read {
    ($proxy:expr, $expected:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

        let (status, content) = $proxy.read($expected.len() as u64).await.expect("read failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(content.as_slice(), $expected.as_bytes());
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_read_err {
    ($proxy:expr, $expected_status:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

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
macro_rules! assert_read_at {
    ($proxy:expr, $offset:expr, $expected:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

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
        use $crate::test_utils::assertions::reexport::Status;

        let (status, content) = $proxy.read_at(100, $offset).await.expect("read failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(content.len(), 0);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_write {
    ($proxy:expr, $content:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

        let (status, len_written) = $proxy.write($content.as_bytes()).await.expect("write failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(len_written, $content.len() as u64);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_write_err {
    ($proxy:expr, $content:expr, $expected_status:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

        let (status, len_written) = $proxy.write($content.as_bytes()).await.expect("write failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(len_written, 0);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_write_fidl_err {
    ($proxy:expr, $content:expr, $expected_error:pat) => {
        match $proxy.write($content.as_bytes()).await {
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
        match $proxy.write($content.as_bytes()).await {
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
        use $crate::test_utils::assertions::reexport::Status;

        let (status, len_written) =
            $proxy.write_at($content.as_bytes(), $offset).await.expect("write failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(len_written, $content.len() as u64);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_write_at_err {
    ($proxy:expr, $offset:expr, $content:expr, $expected_status:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

        let (status, len_written) =
            $proxy.write_at($content.as_bytes(), $offset).await.expect("write failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(len_written, 0);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_seek {
    ($proxy:expr, $pos:expr, Start) => {{
        use $crate::test_utils::assertions::reexport::{SeekOrigin, Status};

        let (status, actual) = $proxy.seek($pos, SeekOrigin::Start).await.expect("seek failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(actual, $pos);
    }};
    ($proxy:expr, $pos:expr, $start:ident, $expected:expr) => {{
        use $crate::test_utils::assertions::reexport::{SeekOrigin, Status};

        let (status, actual) = $proxy.seek($pos, SeekOrigin::$start).await.expect("seek failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(actual, $expected);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_seek_err {
    ($proxy:expr, $pos:expr, $start:ident, $expected_status:expr, $actual_pos:expr) => {{
        use $crate::test_utils::assertions::reexport::{SeekOrigin, Status};

        let (status, actual) = $proxy.seek($pos, SeekOrigin::$start).await.expect("seek failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(actual, $actual_pos);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_truncate {
    ($proxy:expr, $length:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

        let status = $proxy.truncate($length).await.expect("truncate failed");

        assert_eq!(Status::from_raw(status), Status::OK);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_truncate_err {
    ($proxy:expr, $length:expr, $expected_status:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

        let status = $proxy.truncate($length).await.expect("truncate failed");

        assert_eq!(Status::from_raw(status), $expected_status);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_get_attr {
    ($proxy:expr, $expected:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

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
        use $crate::test_utils::assertions::reexport::Status;

        let status = $proxy.close().await.expect("close failed");

        assert_eq!(Status::from_raw(status), Status::OK);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_close_err {
    ($proxy:expr, $expected_status:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

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
        use $crate::test_utils::assertions::reexport::StreamExt;

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
        use $crate::test_utils::assertions::reexport::StreamExt;

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
        use $crate::test_utils::node::open_get_proxy;
        let new_proxy = open_get_proxy::<$new_proxy_type>($proxy, $flags, 0, $path);
        assert_event!(new_proxy, $expected_pattern, $expected_assertion);
        new_proxy
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! open_get_file_proxy_assert_ok {
    ($proxy:expr, $flags:expr, $path:expr) => {{
        use $crate::test_utils::assertions::reexport::{
            FileEvent, FileMarker, FileObject, NodeInfo, Status,
        };

        open_get_proxy_assert!($proxy, $flags, $path, FileMarker, FileEvent::OnOpen_ { s, info }, {
            assert_eq!(Status::from_raw(s), Status::OK);
            assert_eq!(
                info,
                Some(Box::new(NodeInfo::File(FileObject { event: None, stream: None }))),
            );
        })
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! open_as_file_assert_err {
    ($proxy:expr, $flags:expr, $path:expr, $expected_status:expr) => {{
        use $crate::test_utils::assertions::reexport::{FileEvent, FileMarker, Status};

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
        use $crate::test_utils::assertions::reexport::{
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
        use $crate::test_utils::assertions::reexport::{DirectoryEvent, DirectoryMarker, Status};

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
        use $crate::test_utils::node::clone_get_proxy;
        let new_proxy = clone_get_proxy::<$new_proxy_type, _>($proxy, $flags);
        assert_event!(new_proxy, $expected_pattern, $expected_assertion);
        new_proxy
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! clone_get_file_proxy_assert_ok {
    ($proxy:expr, $flags:expr) => {{
        use $crate::test_utils::assertions::reexport::{
            FileEvent, FileMarker, FileObject, NodeInfo, Status,
        };

        clone_get_proxy_assert!($proxy, $flags, FileMarker, FileEvent::OnOpen_ { s, info }, {
            assert_eq!(Status::from_raw(s), Status::OK);
            assert_eq!(
                info,
                Some(Box::new(NodeInfo::File(FileObject { event: None, stream: None }))),
            );
        })
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! clone_as_file_assert_err {
    ($proxy:expr, $flags:expr, $expected_status:expr) => {{
        use $crate::test_utils::assertions::reexport::{FileEvent, FileMarker, Status};

        clone_get_proxy_assert!($proxy, $flags, FileMarker, FileEvent::OnOpen_ { s, info }, {
            assert_eq!(Status::from_raw(s), $expected_status);
            assert_eq!(info, None);
        })
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! clone_get_service_proxy_assert_ok {
    ($proxy:expr, $flags:expr) => {{
        use $crate::test_utils::assertions::reexport::{
            FileEvent, FileMarker, NodeInfo, Service, Status,
        };

        clone_get_proxy_assert!($proxy, $flags, FileMarker, FileEvent::OnOpen_ { s, info }, {
            assert_eq!(Status::from_raw(s), Status::OK);
            assert_eq!(info, Some(Box::new(NodeInfo::Service(Service {}))),);
        })
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! clone_get_directory_proxy_assert_ok {
    ($proxy:expr, $flags:expr) => {{
        use $crate::test_utils::assertions::reexport::{
            DirectoryEvent, DirectoryMarker, NodeInfo, Status,
        };

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
        use $crate::test_utils::assertions::reexport::{DirectoryEvent, DirectoryMarker, Status};

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
        use $crate::test_utils::assertions::reexport::Status;

        let expected = $expected as Vec<u8>;

        let (status, entries) = $proxy.read_dirents($max_bytes).await.expect("read_dirents failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert!(
            entries == expected,
            "Read entries do not match the expectation.\n\
             Expected entries: {:?}\n\
             Actual entries:   {:?}\n\
             Expected as UTF-8 lossy: {:?}\n\
             Received as UTF-8 lossy: {:?}",
            expected,
            entries,
            String::from_utf8_lossy(&expected),
            String::from_utf8_lossy(&entries),
        );
    }};
}

#[macro_export]
macro_rules! assert_read_dirents_one_listing {
    ($proxy:expr, $max_bytes:expr, $( { $type:tt, $name:expr $(,)* } ),* $(,)*) => {{
        use $crate::test_utils::assertions::reexport::{DirentsSameInodeBuilder, INO_UNKNOWN};

        #[allow(unused)]
        use $crate::test_utils::assertions::reexport::{
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
        use $crate::test_utils::assertions::reexport::OPEN_FLAG_DESCRIBE;

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
        use $crate::test_utils::assertions::reexport::Status;

        let (status, entries) = $proxy.read_dirents($max_bytes).await.expect("read_dirents failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert!(
            entries.len() == 0,
            "`read_dirents` returned non-empty list of entries along with an error.\n\
             Got {} entries.\n\
             Content: {:?}\n\
             Content as UTF-8 lossy: {:?}",
            entries.len(),
            entries,
            String::from_utf8_lossy(&entries),
        );
    }};
}

#[macro_export]
macro_rules! vec_string {
    ($($x:expr),*) => (vec![$($x.to_string()),*]);
}

#[macro_export]
macro_rules! assert_channel_closed {
    ($channel:expr) => {{
        use $crate::test_utils::assertions::reexport::{MessageBuf, Status};

        // Allows $channel to be a temporary.
        let channel = &$channel;

        let mut msg = MessageBuf::new();
        match channel.recv_msg(&mut msg).await {
            Ok(()) => panic!(
                "'{}' received a message, instead of been closed: {:?}",
                stringify!($channel),
                msg.bytes(),
            ),
            Err(Status::PEER_CLOSED) => (),
            Err(status) => panic!("'{}' closed with status: {}", stringify!($channel), status),
        }
    }};
}

#[macro_export]
macro_rules! assert_get_buffer {
    ($proxy:expr, $flags:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

        let (status, buffer) = $proxy.get_buffer($flags).await.expect("`get_buffer()` failed");
        assert_eq!(Status::from_raw(status), Status::OK);
        buffer
    }};
}

#[macro_export]
macro_rules! assert_get_buffer_err {
    ($proxy:expr, $flags:expr, $expected_status:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

        let (status, buffer) = $proxy.get_buffer($flags).await.expect("`get_buffer()` failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert!(
            buffer.is_none(),
            "`get_buffer` returned a buffer along with an error code.\n\
             buffer: {:?}",
            buffer
        );
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_unlink {
    ($proxy:expr, $path:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

        let status = $proxy.unlink($path).await.expect("unlink failed");

        assert_eq!(Status::from_raw(status), Status::OK);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_unlink_err {
    ($proxy:expr, $path:expr, $expected_status:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

        let status = $proxy.unlink($path).await.expect("unlink failed");

        assert_eq!(Status::from_raw(status), $expected_status);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_get_token {
    ($proxy:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

        let (status, o_token) = $proxy.get_token().await.expect("get_token failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        match o_token {
            None => panic!("`get_token` returned Status::OK, but no token"),
            Some(token) => token,
        }
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_get_token_err {
    ($proxy:expr, $expected_status:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

        let (status, token) = $proxy.get_token().await.expect("get_token failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert!(
            token.is_none(),
            "`get_token` returned a token along with an error code.\n\
             token: {:?}",
            token
        );
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_rename {
    ($proxy:expr, $src:expr, $dst_parent_token:expr, $dst:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

        let status = $proxy.rename($src, $dst_parent_token, $dst).await.expect("rename failed");

        assert_eq!(Status::from_raw(status), Status::OK);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_rename_err {
    ($proxy:expr, $src:expr, $dst_parent_token:expr, $dst:expr, $expected_status:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

        let status = $proxy.rename($src, $dst_parent_token, $dst).await.expect("rename failed");

        assert_eq!(Status::from_raw(status), $expected_status);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_link {
    ($proxy:expr, $src:expr, $dst_parent_token:expr, $dst:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

        let status = $proxy.link($src, $dst_parent_token, $dst).await.expect("link failed");

        assert_eq!(Status::from_raw(status), Status::OK);
    }};
}

// See comment at the top of the file for why this is a macro.
#[macro_export]
macro_rules! assert_link_err {
    ($proxy:expr, $src:expr, $dst_parent_token:expr, $dst:expr, $expected_status:expr) => {{
        use $crate::test_utils::assertions::reexport::Status;

        let status = $proxy.link($src, $dst_parent_token, $dst).await.expect("link failed");

        assert_eq!(Status::from_raw(status), $expected_status);
    }};
}
