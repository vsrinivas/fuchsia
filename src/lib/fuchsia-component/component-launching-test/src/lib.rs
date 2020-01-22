// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::{Context, Error},
    fidl::endpoints::ServiceMarker,
    fidl_fidl_examples_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::{
        launch_with_options, launcher, App, AppBuilder, LaunchOptions, Stdio,
    },
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::{StreamExt, TryFutureExt, TryStreamExt},
    std::{
        fs::{read_to_string, File},
        io::{Read, Write},
    },
    tempfile::TempDir,
};

const DIR_CHECKER_CMX: &str =
    "fuchsia-pkg://fuchsia.com/component-launching-tests#meta/injected-directory-checker.cmx";
const STDIO_WRITER_CMX: &str =
    "fuchsia-pkg://fuchsia.com/component-launching-tests#meta/stdio-writer.cmx";
const ECHO_CHECKER_CMX: &str =
    "fuchsia-pkg://fuchsia.com/component-launching-tests#meta/echo-service-checker.cmx";

fn make_temp_dir_with_file() -> TempDir {
    let tmp_dir = TempDir::new().expect("tempdir creation");
    File::create(tmp_dir.path().join("injected_file"))
        .expect("tempdir open")
        .write_all("injected file contents".as_bytes())
        .expect("write to tempfile");
    tmp_dir
}

async fn assert_app_success(app: App) {
    let output = app.wait_with_output().await.unwrap();
    assert!(
        output.exit_status.success(),
        "status: {:?}, out: {:?}, err: {:?}",
        output.exit_status,
        String::from_utf8(output.stdout).expect("utf8 stdout"),
        String::from_utf8(output.stderr).expect("utf8 stderr")
    );
}

async fn assert_app_builder_success(app_builder: AppBuilder) {
    let launcher = launcher().expect("get launcher");
    let app = app_builder.spawn(&launcher).unwrap();
    assert_app_success(app).await;
}

async fn assert_dir_checker_success_launch_options(launch_options: LaunchOptions) {
    let launcher = launcher().expect("get launcher");
    let dir_checker =
        launch_with_options(&launcher, DIR_CHECKER_CMX.to_string(), None, launch_options)
            .expect("launch injected-directory-checker");
    assert_app_success(dir_checker).await;
}

async fn assert_echo_checker_success_launch_options(launch_options: LaunchOptions) {
    let launcher = launcher().expect("get launcher");
    let dir_checker =
        launch_with_options(&launcher, ECHO_CHECKER_CMX.to_string(), None, launch_options)
            .expect("launch echo-service-checker");
    assert_app_success(dir_checker).await;
}

#[fasync::run_singlethreaded(test)]
async fn app_builder_add_dir_to_namespace() {
    let injected_dir = make_temp_dir_with_file();
    let dir_checker = AppBuilder::new(DIR_CHECKER_CMX)
        .add_dir_to_namespace(
            "/injected_dir".to_string(),
            File::open(injected_dir.path()).expect("tempdir open"),
        )
        .expect("add_dir_to_namespace")
        .stdout(Stdio::MakePipe)
        .stderr(Stdio::MakePipe);

    assert_app_builder_success(dir_checker).await;
}

#[fasync::run_singlethreaded(test)]
async fn app_builder_add_handle_to_namespace() {
    let injected_dir = make_temp_dir_with_file();
    let dir_checker = AppBuilder::new(DIR_CHECKER_CMX)
        .add_handle_to_namespace(
            "/injected_dir".to_string(),
            fdio::transfer_fd(File::open(injected_dir.path()).expect("tempdir open"))
                .expect("extract handle from file"),
        )
        .stdout(Stdio::MakePipe)
        .stderr(Stdio::MakePipe);

    assert_app_builder_success(dir_checker).await;
}

#[fasync::run_singlethreaded(test)]
async fn launch_options_add_dir_to_namespace() {
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
async fn launch_options_add_handle_to_namespace() {
    let injected_dir = make_temp_dir_with_file();
    let mut launch_options = LaunchOptions::new();
    launch_options.add_handle_to_namespace(
        "/injected_dir".to_string(),
        fdio::transfer_fd(File::open(injected_dir.path()).expect("tempdir open"))
            .expect("extract handle from file"),
    );

    assert_dir_checker_success_launch_options(launch_options).await;
}

async fn serve_echo_stream(mut stream: fecho::EchoRequestStream) -> Result<(), Error> {
    while let Some(fecho::EchoRequest::EchoString { value, responder }) = stream.try_next().await? {
        responder.send(value.as_deref()).context("Error sending response")?;
    }
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn launch_options_set_additional_services() {
    let (client_chan, server_chan) = zx::Channel::create().unwrap();
    let mut launch_options = LaunchOptions::new();
    launch_options.set_additional_services(vec![fecho::EchoMarker::NAME.to_string()], client_chan);

    let mut fs = ServiceFs::new();
    fs.add_fidl_service_at(fecho::EchoMarker::NAME, |stream: fecho::EchoRequestStream| {
        fasync::spawn(
            serve_echo_stream(stream)
                .unwrap_or_else(|e| panic!("Error while serving echo service: {}", e)),
        );
    });
    fs.serve_connection(server_chan).unwrap();
    fasync::spawn(fs.collect());

    assert_echo_checker_success_launch_options(launch_options).await;
}

#[fasync::run_singlethreaded(test)]
async fn app_builder_stdio_handle_as_file() {
    let tmp_dir = TempDir::new().expect("tempdir creation");
    let stdout = fdio::transfer_fd(File::create(tmp_dir.path().join("stdout")).unwrap()).unwrap();
    let stderr = fdio::transfer_fd(File::create(tmp_dir.path().join("stderr")).unwrap()).unwrap();
    let stdio_writer = AppBuilder::new(STDIO_WRITER_CMX).stdout(stdout).stderr(stderr);

    assert_app_builder_success(stdio_writer).await;

    assert_eq!(read_to_string(tmp_dir.path().join("stdout")).unwrap(), "going to stdout\n");
    assert_eq!(read_to_string(tmp_dir.path().join("stderr")).unwrap(), "going to stderr\n");
}

#[fasync::run_singlethreaded(test)]
async fn app_builder_stdio_handle_as_socket() {
    let (mut stdout, stdout_socket) = fdio::pipe_half().unwrap();
    let (mut stderr, stderr_socket) = fdio::pipe_half().unwrap();
    let stdio_writer =
        AppBuilder::new(STDIO_WRITER_CMX).stdout(stdout_socket).stderr(stderr_socket);

    assert_app_builder_success(stdio_writer).await;

    let mut s = String::new();
    stdout.read_to_string(&mut s).unwrap();
    assert_eq!(s, "going to stdout\n");
    s.clear();
    stderr.read_to_string(&mut s).unwrap();
    assert_eq!(s, "going to stderr\n");
}
