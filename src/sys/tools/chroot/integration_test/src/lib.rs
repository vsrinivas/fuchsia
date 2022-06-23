// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia::test]
pub fn chns() {
    // Launch test_writer using chns
    let status =
        std::process::Command::new("/pkg/bin/chns").arg("/pkg/bin/test_writer").status().unwrap();
    assert!(status.success());

    // test_writer should have made a new file under /ns
    let contents = std::fs::read_to_string("/ns/foo.txt").unwrap();
    assert_eq!(contents, "Hippos rule!");
}
