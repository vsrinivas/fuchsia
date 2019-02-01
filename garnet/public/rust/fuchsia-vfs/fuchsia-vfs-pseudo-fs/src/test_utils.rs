// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities used by tests in both file and directory modules.

#![cfg(test)]

// All of the macros in this file could instead be async functions, but then I would have to say
// `await!(open_get_proxy(...))`, while with a macro it is `open_get_proxy!(...)`.  As this is
// local to the testing part of this module, it is probably OK to use macros to save some
// repetition.

// See comment at the top of the file for why this is a macro.
macro_rules! open_get_proxy {
    ($proxy:expr, $flags:expr, $mode:expr, $path:expr, $new_proxy_type:ty) => {{
        let (new_proxy, new_server_end) =
            create_proxy::<$new_proxy_type>().expect("Failed to create connection endpoints");

        $proxy
            .open(
                $flags,
                $mode,
                $path,
                ServerEnd::<NodeMarker>::new(new_server_end.into_channel()),
            )
            .unwrap();

        new_proxy
    }};
    ($proxy:expr, $flags:expr, $path:expr, $new_proxy_type:ty) => {
        open_get_proxy!($proxy, $flags, 0, $path, $new_proxy_type)
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_read {
    ($proxy:expr, $expected:expr) => {
        let (status, content) = await!($proxy.read($expected.len() as u64)).expect("read failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(content.as_slice(), $expected.as_bytes());
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_read_err {
    ($proxy:expr, $expected_status:expr) => {
        let (status, content) = await!($proxy.read(100)).expect("read failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(content.len(), 0);
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_read_fidl_err {
    ($proxy:expr, $expected_error:pat) => {
        match await!($proxy.read(100)) {
            Err($expected_error) => (),
            Err(error) => panic!("read() returned unexpected error: {:?}", error),
            Ok((status, content)) => panic!(
                "Read succeeded: status: {:?}, content: '{:?}'",
                status, content
            ),
        }
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_read_at {
    ($proxy:expr, $offset:expr, $expected:expr) => {
        let (status, content) =
            await!($proxy.read_at($expected.len() as u64, $offset)).expect("read failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(content.as_slice(), $expected.as_bytes());
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_read_at_err {
    ($proxy:expr, $offset:expr, $expected_status:expr) => {
        let (status, content) = await!($proxy.read_at(100, $offset)).expect("read failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(content.len(), 0);
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_write {
    ($proxy:expr, $content:expr) => {
        let (status, len_written) =
            await!($proxy.write(&mut $content.bytes())).expect("write failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(len_written, $content.len() as u64);
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_write_err {
    ($proxy:expr, $content:expr, $expected_status:expr) => {
        let (status, len_written) =
            await!($proxy.write(&mut $content.bytes())).expect("write failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(len_written, 0);
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_write_fidl_err {
    ($proxy:expr, $content:expr, $expected_error:pat) => {
        match await!($proxy.write(&mut $content.bytes())) {
            Err($expected_error) => (),
            Err(error) => panic!("write() returned unexpected error: {:?}", error),
            Ok((status, actual)) => panic!(
                "Write succeeded: status: {:?}, actual: '{:?}'",
                status, actual
            ),
        }
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_write_at {
    ($proxy:expr, $offset:expr, $content:expr) => {
        let (status, len_written) =
            await!($proxy.write_at(&mut $content.bytes(), $offset)).expect("write failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(len_written, $content.len() as u64);
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_write_at_err {
    ($proxy:expr, $offset:expr, $content:expr, $expected_status:expr) => {
        let (status, len_written) =
            await!($proxy.write_at(&mut $content.bytes(), $offset)).expect("write failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(len_written, 0);
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_seek {
    ($proxy:expr, $pos:expr, Start) => {
        let (status, actual) = await!($proxy.seek($pos, SeekOrigin::Start)).expect("seek failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(actual, $pos);
    };
    ($proxy:expr, $pos:expr, $start:ident, $expected:expr) => {
        let (status, actual) = await!($proxy.seek($pos, SeekOrigin::$start)).expect("seek failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(actual, $expected);
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_seek_err {
    ($proxy:expr, $pos:expr, $start:ident, $expected_status:expr, $actual_pos:expr) => {
        let (status, actual) = await!($proxy.seek($pos, SeekOrigin::$start)).expect("seek failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(actual, $actual_pos);
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_truncate {
    ($proxy:expr, $length:expr) => {
        let status = await!($proxy.truncate($length)).expect("truncate failed");

        assert_eq!(Status::from_raw(status), Status::OK);
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_truncate_err {
    ($proxy:expr, $length:expr, $expected_status:expr) => {
        let status = await!($proxy.truncate($length)).expect("truncate failed");

        assert_eq!(Status::from_raw(status), $expected_status);
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_get_attr {
    ($proxy:expr, $expected:expr) => {
        let (status, attrs) = await!($proxy.get_attr()).expect("get_attr failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(attrs, $expected);
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_describe {
    ($proxy:expr, $expected:expr) => {
        let node_info = await!($proxy.describe()).expect("describe failed");
        assert_eq!(node_info, $expected);
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_close {
    ($proxy:expr) => {
        let status = await!($proxy.close()).expect("close failed");

        assert_eq!(Status::from_raw(status), Status::OK);
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_close_err {
    ($proxy:expr, $expected_status:expr) => {
        let status = await!($proxy.close()).expect("close failed");

        assert_eq!(Status::from_raw(status), $expected_status);
    };
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
macro_rules! assert_event {
    ($proxy:expr, $expected_pattern:pat, $expected_assertion:block) => {
        let event_stream = $proxy.take_event_stream();
        match await!(event_stream.into_future()) {
            (Some(Ok($expected_pattern)), _) => $expected_assertion,
            (unexpected, _) => {
                panic!("Unexpected event: {:?}", unexpected);
            }
        }
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_no_event {
    ($proxy:expr) => {
        let event_stream = $proxy.take_event_stream();
        match await!(event_stream.into_future()) {
            (None, _) => (),
            (unexpected, _) => {
                panic!("Unexpected event: {:?}", unexpected);
            }
        }
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! open_get_proxy_assert {
    ($proxy:expr, $flags:expr, $path:expr, $new_proxy_type:ty, $expected_pattern:pat,
     $expected_assertion:block) => {{
        let new_proxy = open_get_proxy!($proxy, $flags, $path, $new_proxy_type);
        assert_event!(new_proxy, $expected_pattern, $expected_assertion);
        new_proxy
    }};
}

// See comment at the top of the file for why this is a macro.
macro_rules! open_get_file_proxy_assert_ok {
    ($proxy:expr, $flags:expr, $path:expr) => {
        open_get_proxy_assert!(
            $proxy,
            $flags,
            $path,
            FileMarker,
            FileEvent::OnOpen_ { s, info },
            {
                assert_eq!(Status::from_raw(s), Status::OK);
                assert_eq!(
                    info,
                    Some(Box::new(NodeInfo::File(FileObject { event: None }))),
                );
            }
        )
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! open_as_file_assert_err {
    ($proxy:expr, $flags:expr, $path:expr, $expected_status:expr) => {
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
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! open_get_directory_proxy_assert_ok {
    ($proxy:expr, $flags:expr, $path:expr) => {
        open_get_proxy_assert!(
            $proxy,
            $flags,
            $path,
            DirectoryMarker,
            DirectoryEvent::OnOpen_ { s, info },
            {
                assert_eq!(Status::from_raw(s), Status::OK);
                assert_eq!(
                    info,
                    Some(Box::new(NodeInfo::Directory(DirectoryObject {
                        reserved: 0
                    }))),
                );
            }
        )
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! open_as_directory_assert_err {
    ($proxy:expr, $flags:expr, $path:expr, $expected_status:expr) => {
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
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_read_dirents {
    ($proxy:expr, $max_bytes:expr, $expected:expr) => {
        let (status, entries) =
            await!($proxy.read_dirents($max_bytes)).expect("read_dirents failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(entries, $expected);
    };
}

// See comment at the top of the file for why this is a macro.
macro_rules! assert_read_dirents_err {
    ($proxy:expr, $max_bytes:expr, $expected_status:expr) => {
        let (status, entries) =
            await!($proxy.read_dirents($max_bytes)).expect("read_dirents failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(entries.len(), 0);
    };
}
