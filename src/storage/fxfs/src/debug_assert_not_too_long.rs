// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_export]
macro_rules! debug_assert_not_too_long {
    ($future:expr $(, $arg:tt)*) => {
        if cfg!(debug_assertions) {
            use futures::future::FutureExt;
            futures::select! {
                () = fuchsia_async::Timer::new(std::time::Duration::from_secs(60)).fuse() =>
                    panic!($($arg),*),
                result = $future.fuse() => result,
            }
        } else {
            $future.await
        }
    }
}
