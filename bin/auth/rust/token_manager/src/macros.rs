// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Yields the value of a `Result` if it was successful or returns with a boxed
/// future containing the error otherwise. This is essentially a Futures
/// version of the `try!` macro.
macro_rules! future_try {
    ($expr:expr) => {
        match $expr {
            Ok(val) => val,
            Err(err) => {
                return Box::new(Err(Into::into(err)).into_future());
            }
        }
    };
    ($expr:expr,) => {
        future_try!($expr)
    };
}
