// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
macro_rules! assert_match {
    ($actual:expr, $expected:pat) => {
        match $actual {
            $expected => {}
            _ => panic!(
                "assertion match failed (actual: `{:?}`, expected: `{}`)",
                $actual,
                stringify!($expected)
            ),
        }
    };
}
