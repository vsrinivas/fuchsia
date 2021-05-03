// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_export]
macro_rules! not_implemented {
    ($($arg:tt)+) => (
        log::warn!(target: "not_implemented", $($arg)+);
    )
}

#[macro_export]
macro_rules! strace {
    ($($arg:tt)+) => (
        log::info!(target: "strace", $($arg)+);
    )
}
