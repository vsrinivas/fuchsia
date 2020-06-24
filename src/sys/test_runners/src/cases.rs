// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! General-purpose structs related to test cases.

/// Internal representation of a test case, after it has been parsed from the data provided by a
/// test binary.
#[derive(Debug, Clone, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub struct TestCaseInfo {
    pub name: String,
    // TODO(kpozin): Additional fields.
}
