// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_pkg::{BlobInfo, MetaPackage, PackageManifest, PackageManifestBuilder};
use std::fs::{File, Permissions};
use std::io::Write;
use std::os::unix::fs::PermissionsExt;
use std::path::Path;

/// Generate a file at |path| which can be executed by a test, and simply exits
/// with a success code.
pub fn generate_fake_tool_nop(path: impl AsRef<Path>) {
    generate_fake_tool(
        &path,
        r#"#!/bin/bash
        exit 0
    "#,
    );
}

/// Generate a file at |path| which can be executed by a test.
pub fn generate_fake_tool(path: impl AsRef<Path>, contents: impl AsRef<str>) {
    let mut tool = File::create(&path).unwrap();
    tool.set_permissions(Permissions::from_mode(0o500)).unwrap();
    tool.write_all(contents.as_ref().as_bytes()).unwrap();
    tool.sync_all().unwrap();
}

// Generates a package manifest to be used for testing. The `name` is used in the blob file
// names to make each manifest somewhat unique. If supplied, `file_path` will be used as the
// non-meta-far blob source path, which allows the tests to use a real file.
pub fn generate_test_manifest(name: &str, file_path: Option<&Path>) -> PackageManifest {
    let meta_source = format!("path/to/{}/meta.far", name);
    let file_source = match file_path {
        Some(path) => path.to_string_lossy().into_owned(),
        _ => format!("path/to/{}/file.txt", name),
    };
    let builder = PackageManifestBuilder::new(MetaPackage::from_name(name.parse().unwrap()));
    let builder = builder.repository("testrepository.com");
    let builder = builder.add_blob(BlobInfo {
        source_path: meta_source,
        path: "meta/".into(),
        merkle: "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap(),
        size: 1,
    });
    let builder = builder.add_blob(BlobInfo {
        source_path: file_source,
        path: "data/file.txt".into(),
        merkle: "1111111111111111111111111111111111111111111111111111111111111111".parse().unwrap(),
        size: 1,
    });
    builder.build()
}

#[cfg(test)]
mod tests {
    use super::*;
    use serial_test::serial;
    use std::process::Command;
    use tempfile::TempDir;

    // These tests must be ran serially, because otherwise they will affect each
    // other through process spawming. If a test spawns a process while the
    // other test has an open file, then the spawned process will get a copy of
    // the open file descriptor, preventing the other test from executing it.
    #[test]
    #[serial]
    fn tool_nop() {
        let dir = TempDir::new().unwrap();
        let tool_path = dir.path().join("mytool.sh");
        generate_fake_tool_nop(&tool_path);
        let status = Command::new(&tool_path).status().unwrap();
        assert!(status.success());
    }

    #[test]
    #[serial]
    fn tool_with_error() {
        let dir = TempDir::new().unwrap();
        let tool_path = dir.path().join("mytool.sh");
        generate_fake_tool(
            &tool_path,
            r#"#!/bin/bash
            exit 100
        "#,
        );
        let status = Command::new(&tool_path).status().unwrap();
        assert_eq!(status.success(), false);
        assert_eq!(status.code().unwrap(), 100);
    }
}
