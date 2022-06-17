// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    fdio::{SpawnAction, SpawnOptions},
    fidl::endpoints::Proxy,
    fuchsia_async as fasync,
    fuchsia_runtime::{job_default, HandleInfo, HandleType},
    fuchsia_zircon::{self as zx, ProcessInfo},
    futures::prelude::*,
    libc::{STDERR_FILENO, STDOUT_FILENO},
    std::{
        convert::TryInto,
        ffi::{CStr, CString},
        fs::File,
        path::Path,
    },
    tempfile::TempDir,
};

struct ProcessOutput {
    return_code: i64,
    stdout: Vec<u8>,
    stderr: Vec<u8>,
}

async fn run_far_tool(args: Vec<&str>, injected_dir: Option<&Path>) -> ProcessOutput {
    const BINARY_PATH: &str = "/pkg/bin/far";

    let (stdout_reader, stdout_writer) =
        zx::Socket::create(zx::SocketOpts::STREAM).expect("create stdout socket");
    let (stderr_reader, stderr_writer) =
        zx::Socket::create(zx::SocketOpts::STREAM).expect("create stderr socket");
    // The reader-ends should not write.
    let () = stdout_writer.half_close().expect("stdout_reader.half_close");
    let () = stdout_writer.half_close().expect("stderr_reader.half_close");

    let mut spawn_actions = vec![];

    let path = CString::new(BINARY_PATH).expect("cstring path");
    let path = path.as_c_str();

    let args: Vec<CString> = std::iter::once(BINARY_PATH)
        .chain(args.into_iter())
        .map(|a| {
            CString::new(a).unwrap_or_else(|e| panic!("failed to parse {} to CString: {}", a, e))
        })
        .collect();
    let args: Vec<&CStr> = args.iter().map(|s| s.as_c_str()).collect();

    let namespace_path = CString::new("/injected-dir").unwrap();
    if let Some(path) = injected_dir {
        let dir_channel = fuchsia_fs::directory::open_in_namespace(
            path.to_str().unwrap(),
            fuchsia_fs::OpenFlags::RIGHT_READABLE | fuchsia_fs::OpenFlags::RIGHT_WRITABLE,
        )
        .unwrap()
        .into_channel()
        .unwrap()
        .into_zx_channel();
        spawn_actions
            .push(fdio::SpawnAction::add_namespace_entry(&namespace_path, dir_channel.into()));
    }

    spawn_actions.push(SpawnAction::add_handle(
        HandleInfo::new(
            HandleType::FileDescriptor,
            STDOUT_FILENO.try_into().expect("STDOUT_FILENO.try_into"),
        ),
        stdout_writer.into(),
    ));
    spawn_actions.push(SpawnAction::add_handle(
        HandleInfo::new(
            HandleType::FileDescriptor,
            STDERR_FILENO.try_into().expect("STDERR_FILENO.try_into"),
        ),
        stderr_writer.into(),
    ));

    let process = fdio::spawn_etc(
        &job_default(),
        SpawnOptions::DEFAULT_LOADER,
        path,
        &args[..],
        None,
        &mut spawn_actions,
    )
    .expect("spawn process");

    let wait_for_process = async move {
        assert_eq!(
            fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
                .await
                .expect("wait for process termination"),
            zx::Signals::PROCESS_TERMINATED
        );
        let ProcessInfo { return_code, start_time: _, flags: _ } =
            process.info().expect("process info");
        return_code
    };
    let stdout = fasync::Socket::from_socket(stdout_reader).unwrap();
    let stderr = fasync::Socket::from_socket(stderr_reader).unwrap();

    let drain = |pipe: fasync::Socket| pipe.into_datagram_stream().try_concat().err_into();

    future::try_join3(
        wait_for_process.map(Result::<_, anyhow::Error>::Ok),
        drain(stdout),
        drain(stderr),
    )
    .map_ok(|(return_code, stdout, stderr)| ProcessOutput { return_code, stdout, stderr })
    .await
    .unwrap()
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
