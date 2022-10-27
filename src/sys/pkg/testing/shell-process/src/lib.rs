// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]
#![deny(missing_docs)]

//! Some methods to help run command line processes.
//!
//! Usage:
//!    let mut fs = ServiceFs::new();
//!    let (svc_client_end, svc_server_end) = zx::Channel::create().expect("create channel");
//!    let svc_proxy = fidl_fuchsia_io::DirectoryProxy::from_channel(
//!        fuchsia_async::Channel::from_channel(svc_client_end).unwrap(),
//!    );
//!    let env = fs.serve_connection(svc_server_end);
//!    ...
//!    let output = shell_process::run_process(
//!        "someprocess",
//!        ["--arg", "foo"],
//!        [("/svc", &svc_proxy)],
//!    );
//!    assert!(output.is_ok());
//!

use {
    fdio::{SpawnAction, SpawnOptions},
    fuchsia_async::{self as fasync},
    fuchsia_runtime::{job_default, HandleInfo, HandleType},
    fuchsia_zircon::{self as zx, HandleBased, ProcessInfo},
    futures::prelude::*,
    libc::{STDERR_FILENO, STDOUT_FILENO},
    std::ffi::{CStr, CString},
};

/// A struct to contain the results of a shell process.
pub struct ProcessOutput {
    /// The return code from a process.
    pub return_code: i64,
    /// The stderr text output of a process.
    pub stderr: Vec<u8>,
    /// The stdout text output of a process.
    pub stdout: Vec<u8>,
}

impl ProcessOutput {
    /// Whether or not a process ended with return_code zero.
    pub fn is_ok(&self) -> bool {
        self.return_code == 0
    }
    /// The return code with which a process ended.
    pub fn return_code(&self) -> i64 {
        self.return_code
    }
    /// The stdout text output of a process, as a &str.
    pub fn stdout_str(&self) -> &str {
        std::str::from_utf8(&self.stdout).unwrap()
    }
    /// The stderr text output of a process, as a &str.
    pub fn stderr_str(&self) -> &str {
        std::str::from_utf8(&self.stderr).unwrap()
    }
}

/// Runs a binary with some arguments asynchronously; returns a delayed exit
/// code and two sockets with stdout and stderr. For a simpler API, use
/// run_process().
pub async fn run_process_async<'a>(
    binary_path: &'a str,
    args: impl IntoIterator<Item = &'a str>,
    proxies: impl IntoIterator<Item = (&'a str, &fidl_fuchsia_io::DirectoryProxy)>,
) -> (fasync::Task<i64>, fasync::Socket, fasync::Socket) {
    //
    let (stdout_reader, stdout_writer) =
        zx::Socket::create(zx::SocketOpts::STREAM).expect("create stdout socket");
    let (stderr_reader, stderr_writer) =
        zx::Socket::create(zx::SocketOpts::STREAM).expect("create stderr socket");
    // The reader-ends should not write.
    let () = stdout_writer.half_close().expect("stdout_reader.half_close");
    let () = stderr_writer.half_close().expect("stderr_reader.half_close");

    let args: Vec<CString> = std::iter::once(binary_path)
        .chain(args)
        .map(|a| {
            CString::new(a).unwrap_or_else(|e| panic!("failed to parse {} to CString: {}", a, e))
        })
        .collect();
    let args: Vec<&CStr> = args.iter().map(|s| s.as_c_str()).collect();

    let mut spawn_actions = vec![];

    let proxy_by_path_cstring: Vec<(CString, &fidl_fuchsia_io::DirectoryProxy)> =
        proxies.into_iter().map(|(path, dir)| (CString::new(path).unwrap(), dir)).collect();
    let proxy_by_path_cstr: Vec<(&CStr, &fidl_fuchsia_io::DirectoryProxy)> =
        proxy_by_path_cstring.iter().map(|(path, dir)| (path.as_c_str(), *dir)).collect();

    for (path, proxy) in proxy_by_path_cstr {
        let (proxy_client_end, proxy_server_end) = fidl::endpoints::create_endpoints().unwrap();
        fuchsia_fs::directory::clone_onto_no_describe(proxy, None, proxy_server_end).unwrap();
        let proxy_client_channel = proxy_client_end.into_channel();
        spawn_actions
            .push(SpawnAction::add_namespace_entry(path, proxy_client_channel.into_handle()));
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
        /*job=*/ &job_default(),
        /*options=*/ SpawnOptions::DEFAULT_LOADER,
        /*path=*/ CString::new(binary_path).expect("cstring path").as_c_str(),
        /*argv=*/ &args[..],
        /*environ=*/ None,
        /*actions=*/ &mut spawn_actions,
    )
    .expect("spawn process");

    (
        fasync::Task::spawn(async move {
            assert_eq!(
                fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
                    .await
                    .expect("wait for process termination"),
                zx::Signals::PROCESS_TERMINATED
            );
            let ProcessInfo { return_code, start_time: _, flags: _ } =
                process.info().expect("process info");
            return_code
        }),
        fasync::Socket::from_socket(stdout_reader).unwrap(),
        fasync::Socket::from_socket(stderr_reader).unwrap(),
    )
}

/// Runs a binary with some arguments synchronously; returns a struct with exit
/// code, stdout, and stderr.
pub async fn run_process<'a>(
    binary_path: &'a str,
    args: impl IntoIterator<Item = &'a str>,
    proxies: impl IntoIterator<Item = (&'a str, &fidl_fuchsia_io::DirectoryProxy)>,
) -> ProcessOutput {
    let (update, stdout_reader, stderr_reader) =
        run_process_async(binary_path, args, proxies).await;

    let drain = |pipe: fasync::Socket| pipe.into_datagram_stream().try_concat().err_into();

    future::try_join3(
        update.map(Result::<_, anyhow::Error>::Ok),
        drain(stdout_reader),
        drain(stderr_reader),
    )
    .map_ok(|(return_code, stdout, stderr)| ProcessOutput { return_code, stdout, stderr })
    .await
    .unwrap()
}
