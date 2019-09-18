// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    fuchsia_async as fasync,
    fuchsia_component::client::{
        launch_with_options, launcher, App, AppBuilder, LaunchOptions, Stdio::MakePipe,
    },
    std::{fs::File, io::Write},
    tempfile::TempDir,
};

const DIR_CHECKER_CMX: &str =
    "fuchsia-pkg://fuchsia.com/component-launching-tests#meta/injected-directory-checker.cmx";

fn make_temp_dir_with_file() -> TempDir {
    let tmp_dir = TempDir::new().expect("tempdir creation");
    File::create(tmp_dir.path().join("injected_file"))
        .expect("tempdir open")
        .write_all("injected file contents".as_bytes())
        .expect("write to tempfile");
    tmp_dir
}

async fn assert_dir_checker_success(dir_checker: App) {
    let output = dir_checker.wait_with_output().await.expect("wait for injected-directory-checker");
    assert!(
        output.exit_status.success(),
        "status: {:?}, out: {:?}, err: {:?}",
        output.exit_status,
        String::from_utf8(output.stdout).expect("utf8 stdout"),
        String::from_utf8(output.stderr).expect("utf8 stderr")
    );
}

async fn assert_dir_checker_success_app_builder(dir_checker: AppBuilder) {
    let launcher = launcher().expect("get launcher");
    let dir_checker = dir_checker.spawn(&launcher).expect("injected-directory-checker to launch");
    assert_dir_checker_success(dir_checker).await;
}

async fn assert_dir_checker_success_launch_options(launch_options: LaunchOptions) {
    let launcher = launcher().expect("get launcher");
    let dir_checker =
        launch_with_options(&launcher, DIR_CHECKER_CMX.to_string(), None, launch_options)
            .expect("launch injected-directory-checker");
    assert_dir_checker_success(dir_checker).await;
}

#[fasync::run_singlethreaded(test)]
async fn test_app_builder_add_dir_to_namespace() {
    let injected_dir = make_temp_dir_with_file();
    let dir_checker = AppBuilder::new(DIR_CHECKER_CMX)
        .add_dir_to_namespace(
            "/injected_dir".to_string(),
            File::open(injected_dir.path()).expect("tempdir open"),
        )
        .expect("add_dir_to_namespace")
        .stdout(MakePipe)
        .stderr(MakePipe);

    assert_dir_checker_success_app_builder(dir_checker).await;
}

#[fasync::run_singlethreaded(test)]
async fn test_app_builder_add_handle_to_namespace() {
    let injected_dir = make_temp_dir_with_file();
    let dir_checker = AppBuilder::new(DIR_CHECKER_CMX)
        .add_handle_to_namespace(
            "/injected_dir".to_string(),
            fdio::transfer_fd(File::open(injected_dir.path()).expect("tempdir open"))
                .expect("extract handle from file"),
        )
        .stdout(MakePipe)
        .stderr(MakePipe);

    assert_dir_checker_success_app_builder(dir_checker).await;
}

#[fasync::run_singlethreaded(test)]
async fn test_launch_options_add_dir_to_namespace() {
    let injected_dir = make_temp_dir_with_file();
    let mut launch_options = LaunchOptions::new();
    launch_options
        .add_dir_to_namespace(
            "/injected_dir".to_string(),
            File::open(injected_dir.path()).expect("tempdir open"),
        )
        .expect("add_dir_to_namespace");

    assert_dir_checker_success_launch_options(launch_options).await;
}

#[fasync::run_singlethreaded(test)]
async fn test_launch_options_add_handle_to_namespace() {
    let injected_dir = make_temp_dir_with_file();
    let mut launch_options = LaunchOptions::new();
    launch_options.add_handle_to_namespace(
        "/injected_dir".to_string(),
        fdio::transfer_fd(File::open(injected_dir.path()).expect("tempdir open"))
            .expect("extract handle from file"),
    );

    assert_dir_checker_success_launch_options(launch_options).await;
}
