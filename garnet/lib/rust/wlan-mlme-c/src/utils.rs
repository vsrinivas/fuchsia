// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Unwraps a given `Result` yielding content of `Ok`.
/// If Result carries an `Err` its content will be logged and the function will early return with
/// the raw value of the given `zx::Status`.
/// This macro is comparable to Rust's try macro.
macro_rules! unwrap_or_bail {
    ($result:expr, $e:expr) => {
        match $result {
            Ok(x) => x,
            Err(e) => {
                error!("error: {}", e);
                return $e.into();
            }
        }
    };
}

pub unsafe fn as_slice<'a>(data: *const u8, len: usize) -> &'a [u8] {
    if data.is_null() {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(data, len) }
    }
}
