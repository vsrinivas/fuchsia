// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    serde::{Deserialize, Serialize},
    std::cmp::{Eq, PartialEq},
    std::fmt::Debug,
};

#[derive(Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct TestList {
    #[allow(unused)]
    pub tests: Vec<TestListEntry>,
}

#[derive(Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct TestListEntry {
    // The name of the test.
    // MUST BE unique within the test list file.
    #[allow(unused)]
    pub name: String,

    // Arbitrary labels for this test list.
    // No format requirements are imposed on these labels,
    // but for GN this is typically a build label.
    #[allow(unused)]
    pub labels: Vec<String>,

    // Arbitrary tags for this test case.
    #[allow(unused)]
    pub tags: Vec<TestTag>,
}

#[derive(Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct TestTag {
    #[allow(unused)]
    pub key: String,

    #[allow(unused)]
    pub value: String,
}
