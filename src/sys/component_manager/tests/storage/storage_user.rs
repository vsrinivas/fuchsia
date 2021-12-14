// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fs::*;

#[fuchsia::component]
fn main() {
    // Write a file to the storage directory of this component
    let expected_content = "hippos_are_neat";
    write("/data/hippo", expected_content).unwrap();

    // Access the global storage of the memfs component and get the path
    // to the file from there
    let dir = read_dir("/memfs").unwrap();
    let entries: Vec<DirEntry> = dir.map(|e| e.unwrap()).collect();
    assert_eq!(entries.len(), 1);
    let entry = entries.get(0).unwrap();
    let file_path = entry.path().join("data/hippo");

    // Read the file back from the global storage path and compare it
    let actual_content = read_to_string(file_path).unwrap();
    assert_eq!(actual_content, expected_content);
}
