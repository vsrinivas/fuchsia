// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_export]
macro_rules! vlog {
    ($v:expr, $($arg:tt)+) => (::fuchsia_syslog::fx_vlog!(tag: "codec_stress_tests", $v, $($arg)+))
}
