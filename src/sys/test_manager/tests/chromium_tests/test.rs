// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests verify that dependencies needed by tests in the chromium realm are correctly
// routed.
// TODO(fxbug.dev/91934): once we can define the chromium realm out of tree, we should move
// the definition and remove these tests.

const BUILD_DIR: &str = "/build_info";

#[test]
fn can_read_build_info() {
    let entries: Vec<_> = std::fs::read_dir(BUILD_DIR).expect("read build directory").collect();
    assert!(!entries.is_empty());
}
