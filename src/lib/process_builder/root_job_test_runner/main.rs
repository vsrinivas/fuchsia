// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Run a test binary as a new process under the root job.
//!
//! This is needed since most or all jobs besides the root job have restricted job policy that
//! disallows use of the zx_process_create syscall, which process_builder uses. Processes that use
//! process_builder normally run in the root job, so we need a similar environment for the test.
//!
//! This approach is a temporary hack. It relies on the fact that the root job is available on
//! certain builds and to v1 test components through the fuchsia.kernel.RootJob service.
//!
//! TODO(fxbug.dev/71135): Figure out a better way to run these tests.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_kernel as fkernel,
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, },
    std::env,
    std::ffi::{CStr, CString},
    std::fs,
    std::path::{Path},
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        return Err(format_err!("Usage: root_job_test_runner <test binary> [extra args]"));
    }

    let path = &args[1];
    if !Path::new(path).exists() {
        return Err(format_err!("Test binary '{}' does not exist in namespace", path));
    }

    // We clone stdout and stderr, but leave stdin as null. This is to test that "cloning"
    // a null stdio works when spawning processes (see bug 35902).
    let stdout = std::io::stdout();
    let stderr = std::io::stderr();
    let mut actions = vec![
        fdio::SpawnAction::clone_fd(&stdout, 1),
        fdio::SpawnAction::clone_fd(&stderr, 2),
    ];

    // Also pass through /tmp if it exists, as it's needed for some component manager tests
    let tmp_str = CString::new("/tmp")?;
    if Path::new("/tmp").exists() {
        let tmp_dir = fs::File::open("/tmp")?;
        actions.push(fdio::SpawnAction::add_namespace_entry(&tmp_str, fdio::transfer_fd(tmp_dir)?));
    }

    let root_job = get_root_job().await?;
    let argv: Vec<CString> =
        args.into_iter().skip(1).map(CString::new).collect::<Result<_, _>>()?;
    let argv_ref: Vec<&CStr> = argv.iter().map(|a| &**a).collect();
    let options = fdio::SpawnOptions::CLONE_ALL
        & !fdio::SpawnOptions::CLONE_STDIO;
    let process =
        fdio::spawn_etc(&root_job, options, argv_ref[0], argv_ref.as_slice(), None, &mut actions)
            .map_err(|(status, errmsg)| {
            format_err!("fdio::spawn_etc failed: {}, {}", status, errmsg)
        })?;

    // Wait for the process to terminate. We're hosting it's /svc directory, which will go away if
    // we exit before it.
    fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED).await?;

    // Return the same code as the test process, in case it failed.
    let info = process.info()?;
    zx::Process::exit(info.return_code);
}

async fn get_root_job() -> Result<zx::Job, Error> {
    let (proxy, server_end) = fidl::endpoints::create_proxy::<fkernel::RootJobMarker>()?;
    fdio::service_connect("/svc/fuchsia.kernel.RootJob", server_end.into_channel())
        .context("Failed to connect to fuchsia.kernel.RootJob service")?;

    let job = proxy.get().await?;
    Ok(job)
}
