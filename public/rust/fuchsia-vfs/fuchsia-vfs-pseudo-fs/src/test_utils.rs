// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utlities used by tests in both file and directory modules.

#![cfg(test)]

// This can be an async function, but then I would have to say `await!(assert_read(...))`,
// while with a macro it is `assert_read!(...)`.  As this is local to the testing part of this
// module, it is probalby OK to use macros to save some repeatition.
macro_rules! assert_read {
    ($proxy:expr, $expected:expr) => {
        let (status, content) = await!($proxy.read($expected.len() as u64)).expect("read failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(content.as_slice(), $expected.as_bytes());
    };
}

// See comment above assert_read above for why this is a macro.
macro_rules! assert_read_err {
    ($proxy:expr, $expected_status:expr) => {
        let (status, content) = await!($proxy.read(100)).expect("read failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(content.len(), 0);
    };
}

// See comment above assert_read above for why this is a macro.
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

// See comment above assert_read above for why this is a macro.
macro_rules! assert_read_at {
    ($proxy:expr, $offset:expr, $expected:expr) => {
        let (status, content) =
            await!($proxy.read_at($expected.len() as u64, $offset)).expect("read failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(content.as_slice(), $expected.as_bytes());
    };
}

// See comment above assert_read above for why this is a macro.
macro_rules! assert_read_at_err {
    ($proxy:expr, $offset:expr, $expected_status:expr) => {
        let (status, content) = await!($proxy.read_at(100, $offset)).expect("read failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(content.len(), 0);
    };
}

// See comment above assert_read above for why this is a macro.
macro_rules! assert_write {
    ($proxy:expr, $content:expr) => {
        let (status, len_written) =
            await!($proxy.write(&mut $content.bytes())).expect("write failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(len_written, $content.len() as u64);
    };
}

// See comment above assert_read above for why this is a macro.
macro_rules! assert_write_err {
    ($proxy:expr, $content:expr, $expected_status:expr) => {
        let (status, len_written) =
            await!($proxy.write(&mut $content.bytes())).expect("write failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(len_written, 0);
    };
}

// See comment above assert_read above for why this is a macro.
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

// See comment above assert_read above for why this is a macro.
macro_rules! assert_write_at {
    ($proxy:expr, $offset:expr, $content:expr) => {
        let (status, len_written) =
            await!($proxy.write_at(&mut $content.bytes(), $offset)).expect("write failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(len_written, $content.len() as u64);
    };
}

// See comment above assert_read above for why this is a macro.
macro_rules! assert_write_at_err {
    ($proxy:expr, $offset:expr, $content:expr, $expected_status:expr) => {
        let (status, len_written) =
            await!($proxy.write_at(&mut $content.bytes(), $offset)).expect("write failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(len_written, 0);
    };
}

// See comment above assert_read above for why this is a macro.
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

// See comment above assert_read above for why this is a macro.
macro_rules! assert_seek_err {
    ($proxy:expr, $pos:expr, $start:ident, $expected_status:expr, $actual_pos:expr) => {
        let (status, actual) = await!($proxy.seek($pos, SeekOrigin::$start)).expect("seek failed");

        assert_eq!(Status::from_raw(status), $expected_status);
        assert_eq!(actual, $actual_pos);
    };
}

// See comment above assert_read above for why this is a macro.
macro_rules! assert_truncate {
    ($proxy:expr, $length:expr) => {
        let status = await!($proxy.truncate($length)).expect("truncate failed");

        assert_eq!(Status::from_raw(status), Status::OK);
    };
}

// See comment above assert_read above for why this is a macro.
macro_rules! assert_truncate_err {
    ($proxy:expr, $length:expr, $expected_status:expr) => {
        let status = await!($proxy.truncate($length)).expect("truncate failed");

        assert_eq!(Status::from_raw(status), $expected_status);
    };
}

// See comment above assert_read above for why this is a macro.
macro_rules! assert_get_attr {
    ($proxy:expr, $expected:expr) => {
        let (status, attrs) = await!($proxy.get_attr()).expect("get_attr failed");

        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(attrs, $expected);
    };
}

// See comment above assert_read above for why this is a macro.
macro_rules! assert_close {
    ($proxy:expr) => {
        let status = await!($proxy.close()).expect("close failed");

        assert_eq!(Status::from_raw(status), Status::OK);
    };
}

// See comment above assert_read above for why this is a macro.
macro_rules! assert_close_err {
    ($proxy:expr, $expected_status:expr) => {
        let status = await!($proxy.close()).expect("close failed");

        assert_eq!(Status::from_raw(status), $expected_status);
    };
}
