// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

fn main() {
    let contents =
        std::fs::read_to_string("/injected_dir/injected_file").expect("read injected file");
    assert_eq!(contents, "injected file contents");
}
