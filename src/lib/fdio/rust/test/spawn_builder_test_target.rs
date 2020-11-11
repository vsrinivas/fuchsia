// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

fn main() {
    // Validate args
    assert_eq!(vec!["arg0".to_string(), "arg1".to_string()], std::env::args().collect::<Vec<_>>());

    // Validate injected directory
    assert_eq!(std::fs::read_to_string("/injected-dir/injected-file").unwrap(), "some-contents");
}
