// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {std::process::Command, tempfile::TempDir};

const FFX_TOOL_PATH: &str = env!("FFX_TOOL_PATH");
const BLOBFS_PATH: &str = env!("BLOBFS_PATH");
const ZBI_PATH: &str = env!("ZBI_PATH");
const RECOVERY_ZBI_PATH: &str = env!("RECOVERY_ZBI_PATH");

#[test]
fn extract_blobfs() {
    let tmp_dir = TempDir::new().unwrap();
    let tmp_path = tmp_dir.path().to_str().unwrap();
    assert!(Command::new(FFX_TOOL_PATH)
        .args(vec![
            "scrutiny",
            "shell",
            &format!("tool.blobfs.extract --input {} --output {}/blobfs", BLOBFS_PATH, tmp_path),
        ])
        .status()
        .unwrap()
        .success());
}

#[test]
fn extract_zbi() {
    let tmp_dir = TempDir::new().unwrap();
    let tmp_path = tmp_dir.path().to_str().unwrap();
    assert!(Command::new(FFX_TOOL_PATH)
        .args(vec![
            "scrutiny",
            "shell",
            &format!("tool.zbi.extract --input {} --output {}/zbi", ZBI_PATH, tmp_path),
        ])
        .status()
        .unwrap()
        .success());
}

#[test]
fn extract_recovery_zbi() {
    let tmp_dir = TempDir::new().unwrap();
    let tmp_path = tmp_dir.path().to_str().unwrap();
    assert!(Command::new(FFX_TOOL_PATH)
        .args(vec![
            "scrutiny",
            "shell",
            &format!("tool.zbi.extract --input {} --output {}/zbi", RECOVERY_ZBI_PATH, tmp_path),
        ])
        .status()
        .unwrap()
        .success());
}
