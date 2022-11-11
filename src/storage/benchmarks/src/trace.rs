// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A wrapper around `fuchsia_trace::duration` that gets removed in non-fuchsia builds.
#[macro_export]
macro_rules! trace_duration {
    ($category:expr, $name:expr $(, $key:expr => $val:expr)*) => {
        #[cfg(target_os = "fuchsia")]
        ::fuchsia_trace::duration!($category, $name $(,$key => $val)*);

        // Make a no-op use of all values to avoid unused variable errors on non-fuchsia platforms
        // when a variable only exists for tracing.
        #[cfg(not(target_os = "fuchsia"))]
        $(let _ = $val;)*
    }
}
