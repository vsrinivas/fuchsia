// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

use crate::types::Errno;

#[macro_export]
macro_rules! not_implemented {
    ($($arg:tt)+) => (
        log::warn!(target: "not_implemented", $($arg)+)
    )
}

#[macro_export]
macro_rules! strace {
    ($task:expr, $fmt:expr $(, $($arg:tt)*)?) => (
        log::debug!(target: "strace", concat!("{}[{}] ", $fmt), $task.id, $task.command.read().to_string_lossy() $(, $($arg)*)?);
    )
}

// Call this when you get an error that should "never" happen, i.e. if it does that means the
// kernel was updated to produce some other error after this match was written.
// TODO(tbodt): find a better way to handle this than a panic.
pub fn impossible_error(status: zx::Status) -> Errno {
    panic!("encountered impossible error: {}", status);
}
