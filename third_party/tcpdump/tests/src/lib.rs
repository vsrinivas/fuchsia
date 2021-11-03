// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fdio::{SpawnAction, SpawnOptions};
use fuchsia_async as fasync;
use fuchsia_runtime::{job_default, HandleInfo, HandleType};
use fuchsia_zircon::{self as zx, ProcessInfo};
use futures::io::{AsyncBufReadExt as _, AsyncReadExt as _, BufReader};
use libc::{STDERR_FILENO, STDOUT_FILENO};
use std::{convert::TryInto as _, ffi::CString};

const BINARY_PATH: &str = "/pkg/bin/tcpdump";

#[fuchsia::test]
async fn version_test() {
    let (stdout_reader, stdout_writer) =
        zx::Socket::create(zx::SocketOpts::STREAM).expect("create stdout socket");
    let (stderr_reader, stderr_writer) =
        zx::Socket::create(zx::SocketOpts::STREAM).expect("create stderr socket");

    // The reader-ends should not write.
    let () = stdout_reader.half_close().expect("stdout_reader.half_close");
    let () = stderr_reader.half_close().expect("stderr_reader.half_close");

    let path = CString::new(BINARY_PATH).expect("cstring path");
    let path = path.as_c_str();

    let process = fdio::spawn_etc(
        &job_default(),
        SpawnOptions::DEFAULT_LOADER,
        path,
        &[path, CString::new("--version").expect("cstring versiona arg").as_c_str()][..],
        None,
        &mut [
            // Provide the socket that TCPDump should use for stdout.
            SpawnAction::add_handle(
                HandleInfo::new(
                    HandleType::FileDescriptor,
                    STDOUT_FILENO.try_into().expect("STDOUT_FILENO.try_into"),
                ),
                stdout_writer.into(),
            ),
            // Provide the socket that TCPDump should use for stderr.
            SpawnAction::add_handle(
                HandleInfo::new(
                    HandleType::FileDescriptor,
                    STDERR_FILENO.try_into().expect("STDERR_FILENO.try_into"),
                ),
                stderr_writer.into(),
            ),
        ],
    )
    .expect("spawn tcpdump");
    assert_eq!(
        fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
            .await
            .expect("wait for process termination"),
        zx::Signals::PROCESS_TERMINATED
    );
    let ProcessInfo { return_code, start_time: _, flags: _ } =
        process.info().expect("process info");
    assert_eq!(return_code, 0);

    let mut stdout_reader =
        BufReader::new(fasync::Socket::from_socket(stdout_reader).expect("async socket stdout"));

    let mut patterns = vec![
        "tcpdump version tcpdump-4.99.0 (2fef164426273c4c610a3c060b7990db0e99914d)",
        "libpcap version libpcap-1.10.1 (c7642e2cc0c5bd65754685b160d25dc23c76c6bd)",
    ];

    while !patterns.is_empty() {
        let mut stdout = String::new();
        let read_bytes = stdout_reader.read_line(&mut stdout).await.expect("stdout read_line");
        if read_bytes == 0 {
            let mut stderr = String::new();
            let _read_bytes: usize = fasync::Socket::from_socket(stderr_reader)
                .expect("async socket stdout")
                .read_to_string(&mut stderr)
                .await
                .expect("read stderr to string");
            panic!(
                "failed to match all patterns from stdout; patterns = {:?}\nSTDERR:\n{}",
                patterns, stderr
            );
        }

        // Trim the trailing new line.
        let stdout = &stdout[..stdout.len() - 1];
        let () = patterns.retain(|pattern| &stdout != pattern);
    }
}
