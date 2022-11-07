// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

use crate::types::Errno;

macro_rules! strace {
    (level = $level:ident, tag = $tag:expr, $task:expr, $fmt:expr $(, $($arg:tt)*)?) => {
        tracing::$level!(tag = $tag, concat!("{:?} ", $fmt), $task $(, $($arg)*)?);
    };
    (level = $level:ident, $task:expr, $fmt:expr $(, $($arg:tt)*)?) => {
        $crate::logging::strace!(level = $level, tag = "strace", $task, $fmt $(, $($arg)*)?)

    };
    ($task:expr, $fmt:expr $(, $($arg:tt)*)?) => {
        $crate::logging::strace!(level = trace, $task, $fmt $(, $($arg)*)?)
    };
}

macro_rules! not_implemented {
    ($task:expr, $fmt:expr $(, $($arg:tt)*)?) => (
        $crate::logging::strace!(level = warn, tag = "not_implemented", $task, $fmt $(, $($arg)*)?)
    )
}

macro_rules! not_implemented_log_once {
    ($($arg:tt)*) => (
        {
            static DID_LOG: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);
            if !DID_LOG.swap(true, std::sync::atomic::Ordering::AcqRel) {
                not_implemented!($($arg)*);
            }
        }
    )
}

// Public re-export of macros allows them to be used like regular rust items.
pub(crate) use not_implemented;
pub(crate) use not_implemented_log_once;
pub(crate) use strace;

// Call this when you get an error that should "never" happen, i.e. if it does that means the
// kernel was updated to produce some other error after this match was written.
// TODO(tbodt): find a better way to handle this than a panic.
#[track_caller]
pub fn impossible_error(status: zx::Status) -> Errno {
    panic!("encountered impossible error: {}", status);
}

fn truncate_name(name: &[u8]) -> std::ffi::CString {
    std::ffi::CString::from_vec_with_nul(
        name.iter()
            .map(|c| if *c == b'\0' { b'?' } else { *c })
            .take(zx::sys::ZX_MAX_NAME_LEN - 1)
            .chain(b"\0".iter().cloned())
            .collect(),
    )
    .expect("all the null bytes should have been replace with an escape")
}

pub fn set_zx_name(obj: &impl zx::AsHandleRef, name: &[u8]) {
    obj.set_name(&truncate_name(name)).map_err(impossible_error).unwrap();
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::testing::*;
    use std::ffi::{CStr, CString};
    use zx::{sys, AsHandleRef};

    #[test]
    fn test_truncate_name() {
        assert_eq!(truncate_name(b"foo").as_ref(), CStr::from_bytes_with_nul(b"foo\0").unwrap());
        assert_eq!(truncate_name(b"").as_ref(), CStr::from_bytes_with_nul(b"\0").unwrap());
        assert_eq!(
            truncate_name(b"1234567890123456789012345678901234567890").as_ref(),
            CStr::from_bytes_with_nul(b"1234567890123456789012345678901\0").unwrap()
        );
        assert_eq!(truncate_name(b"a\0b").as_ref(), CStr::from_bytes_with_nul(b"a?b\0").unwrap());
    }

    #[test]
    fn test_long_name() {
        let (_kernel, current_task) = create_kernel_and_task();
        let bytes = [1; sys::ZX_MAX_NAME_LEN];
        let name = CString::new(bytes).unwrap();

        let max_bytes = [1; sys::ZX_MAX_NAME_LEN - 1];
        let expected_name = CString::new(max_bytes).unwrap();

        set_zx_name(&current_task.thread_group.process, name.as_bytes());
        assert_eq!(current_task.thread_group.process.get_name(), Ok(expected_name));
    }

    #[test]
    fn test_max_length_name() {
        let (_kernel, current_task) = create_kernel_and_task();
        let bytes = [1; sys::ZX_MAX_NAME_LEN - 1];
        let name = CString::new(bytes).unwrap();

        set_zx_name(&current_task.thread_group.process, name.as_bytes());
        assert_eq!(current_task.thread_group.process.get_name(), Ok(name));
    }

    #[test]
    fn test_short_name() {
        let (_kernel, current_task) = create_kernel_and_task();
        let bytes = [1; sys::ZX_MAX_NAME_LEN - 10];
        let name = CString::new(bytes).unwrap();

        set_zx_name(&current_task.thread_group.process, name.as_bytes());
        assert_eq!(current_task.thread_group.process.get_name(), Ok(name));
    }
}
