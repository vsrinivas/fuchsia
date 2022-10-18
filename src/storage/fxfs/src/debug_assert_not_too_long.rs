// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_export]
macro_rules! debug_assert_not_too_long {
    ($future:expr $(, $arg:tt)*) => {
        {
            #[cfg(debug_assertions)]
            {
                use futures::future::FutureExt;
                let future = $future.fuse();
                futures::pin_mut!(future);
                loop {
                    futures::select! {
                        () = fuchsia_async::Timer::new(std::time::Duration::from_secs(60)).fuse() =>
                            {
                                #[cfg(target_os = "fuchsia")]
                                backtrace_request::backtrace_request();
                                #[cfg(not(target_os = "fuchsia"))]
                                panic!($($arg),*);
                            }
                        result = future => break result,
                    }
                }
            }
            #[cfg(not(debug_assertions))]
            $future.await
        }
    };
}
