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
        ftest::RunOptions {
            include_disabled_tests: self.include_disabled_tests,
            parallel: self.parallel,
            arguments: self.arguments.clone(),
            ..ftest::RunOptions::empty()
        }
    }
}

impl CloneExt for ftest::Invocation {
    fn clone(&self) -> Self {
        ftest::Invocation {
            name: self.name.clone(),
            tag: self.tag.clone(),
            ..ftest::Invocation::empty()
        }
    }
}

impl CloneExt for Vec<ftest::Invocation> {
    fn clone(&self) -> Self {
        self.iter().map(|i| i.clone()).collect()
    }
}
