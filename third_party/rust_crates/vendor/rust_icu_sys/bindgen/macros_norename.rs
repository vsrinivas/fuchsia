// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This macro will pass the function name as is.  Use for linking with the ICU
// library that has been compiled with `--disable-renaming`.
//
// See README.md for details.
#[macro_export]
macro_rules! versioned_function {
    ($i:ident) => {
        $i
    };
}
