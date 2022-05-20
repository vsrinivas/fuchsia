// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(target_os = "fuchsia")]
macro_rules! duration {
    ($name:expr $(, $key:expr => $val:expr)*) => {
        ::fuchsia_trace::duration!("run_test_suite", $name $(, $key => $val)*);
    }
}

// On host we'll need to find another trace mechanism, but we'll disable this for now.
#[cfg(not(target_os = "fuchsia"))]
macro_rules! duration {
    ($name:expr $(, $key:expr => $val:expr)*) => {};
}

pub(crate) use duration;
