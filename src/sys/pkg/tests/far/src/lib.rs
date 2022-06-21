// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    fuchsia_async as fasync,
    shell_process::ProcessOutput,
    std::{fs::File, path::Path},
    tempfile::TempDir,
};

async fn run_far_tool(args: Vec<&str>, injected_dir: Option<&Path>) -> ProcessOutput {
    const BINARY_PATH: &str = "/pkg/bin/far";
    if let Some(path) = injected_dir {
        let directory_proxy = fuchsia_fs::directory::open_in_namespace(
            path.to_str().unwrap(),
            fuchsia_fs::OpenFlags::RIGHT_READABLE | fuchsia_fs::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap();
        let proxies = vec![("/injected-dir", &directory_proxy)];
        return shell_process::run_process(BINARY_PATH, args, proxies).await;
    }
    shell_process::run_process(BINARY_PATH, args, vec![]).await
}

async fn assert_far_fails_to_open_missing_archive_file(
    args: Vec<&str>,
    injected_dir: Option<&Path>,
) {
    let output = run_far_tool(args, injected_dir).await;

    assert_eq!(output.return_code, -1);
    assert_eq!(String::from_utf8(output.stdout).unwrap(), "");
    assert_eq!(
        String::from_utf8(output.stderr).unwrap(),
        "error: unable to open file: missing-archive\n"
    );
}

#[fasync::run_singlethreaded(test)]
async fn list_fails_missing_archive() {
    assert_far_fails_to_open_missing_archive_file(vec!["list", "--archive=missing-archive"], None)
        .await
}

#[fasync::run_singlethreaded(test)]
async fn extract_fails_missing_archive() {
    assert_far_fails_to_open_missing_archive_file(
        vec!["extract", "--archive=missing-archive", "--output=missing-output"],
        None,
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn extract_file_fails_missing_archive() {
    assert_far_fails_to_open_missing_archive_file(
        vec![
            "extract-file",
            "--archive=missing-archive",
            "--file=missing-file",
            "--output=missing-output",
        ],
        None,
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn cat_fails_missing_archive() {
    assert_far_fails_to_open_missing_archive_file(
        vec!["cat", "--archive=missing-archive", "--file=missing-file"],
        None,
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn create_fails_missing_archive() {
    let injected_dir = TempDir::new().unwrap();
    let _injected_manifest = File::create(injected_dir.path().join("empty-manifest")).unwrap();
    assert_far_fails_to_open_missing_archive_file(
        vec!["create", "--archive=missing-archive", "--manifest=/injected-dir/empty-manifest"],
        Some(injected_dir.path()),
    )
    .await
}
