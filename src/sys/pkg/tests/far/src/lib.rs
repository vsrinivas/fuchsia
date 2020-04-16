// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    fuchsia_async as fasync,
    fuchsia_component::client::{launcher, AppBuilder, Stdio},
    std::fs::File,
    tempfile::TempDir,
};

const FAR_CMX: &str = "fuchsia-pkg://fuchsia.com/far-integration-tests#meta/far.cmx";

async fn assert_far_fails_to_open_missing_archive_file(
    args: Vec<&str>,
    injected_dir: Option<File>,
) {
    let mut far =
        AppBuilder::new(FAR_CMX).args(args).stdout(Stdio::MakePipe).stderr(Stdio::MakePipe);
    if let Some(dir) = injected_dir {
        far = far.add_dir_to_namespace("/injected-dir".to_string(), dir).unwrap()
    }
    let far = far.spawn(&launcher().unwrap()).unwrap();
    let output = far.wait_with_output().await.unwrap();
    assert!(output.exit_status.exited(), "status: {:?}", output.exit_status);
    assert_eq!(output.exit_status.code(), -1, "status: {:?}", output.exit_status);
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
        Some(File::open(injected_dir.path()).unwrap()),
    )
    .await
}
