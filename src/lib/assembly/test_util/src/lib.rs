// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
