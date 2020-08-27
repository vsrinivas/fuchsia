// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Extensions for `fidl_fuchsia_test`.

use fidl_fuchsia_test as ftest;

/// Extension trait that provides a manual implementation of `std::clone::Clone`.
pub trait CloneExt {
    fn clone(&self) -> Self;
}

impl CloneExt for ftest::RunOptions {
    fn clone(&self) -> Self {
        ftest::RunOptions { include_disabled_tests: self.include_disabled_tests, parallel: None }
    }
}
