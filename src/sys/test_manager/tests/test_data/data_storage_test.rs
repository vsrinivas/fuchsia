// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::PathBuf;

#[fuchsia::test]
fn test_data_storage() {
    let dir_path = PathBuf::from("/data");
    assert!(dir_path.exists());
    assert!(dir_path.is_dir());

    // make sure directory is empty as start of the test
    let is_empty = dir_path.read_dir().unwrap().next().is_none();
    assert!(is_empty);

    // make sure it is writable.
    let file_path = dir_path.join("my_file");
    std::fs::write(&file_path, "my_content").unwrap();

    assert_eq!("my_content", std::fs::read_to_string(&file_path).unwrap());
}
