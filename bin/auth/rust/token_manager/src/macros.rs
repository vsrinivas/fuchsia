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
                return FutureObj::new(Box::new(::futures::future::ready(Err(Into::into(err)))));
            }
        }
    };
    ($expr:expr,) => {
        future_try!($expr)
    };
}
