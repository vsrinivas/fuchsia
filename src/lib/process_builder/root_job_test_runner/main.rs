// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Run a test binary as a new process under the root job.
//!
//! This is needed since most or all jobs besides the root job have restricted job policy that
//! disallows use of the zx_process_create syscall, which process_builder uses. Processes that use
//! process_builder normally run in the root job, so we need a similar environment for the test.
//!
//! This approach is a temporary hack. It relies on the fact that the sysinfo driver freely hands
//! out handles to the root job through /dev/misc/sysinfo, which is a security hole that will be
//! closed soon.
//!
//! TODO: Figure out a better way to run these tests.

#![feature(async_await, await_macro)]

use {
    failure::{err_msg, format_err, Error, ResultExt},
    fdio, fidl_fuchsia_sysinfo as fsysinfo, fuchsia_async as fasync, fuchsia_zircon as zx,
    std::env,
    std::ffi::{CStr, CString},
    std::path::Path,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        return Err(err_msg("Usage: root_job_test_runner <test binary> [extra args]"));
    }

    let path = &args[1];
    if !Path::new(path).exists() {
        return Err(format_err!("Test binary '{}' does not exist in namespace", path));
    }

    let root_job = await!(get_root_job())?;
    let argv: Vec<CString> =
        args.into_iter().skip(1).map(CString::new).collect::<Result<_, _>>()?;
    let argv_ref: Vec<&CStr> = argv.iter().map(|a| &**a).collect();
    let process =
        fdio::spawn(&root_job, fdio::SpawnOptions::CLONE_ALL, argv_ref[0], argv_ref.as_slice())?;

    // Wait for the process to terminate, since it may want to use things out of this component's
    // namespace (like /svc), which will go away if we exit before it.
    await!(fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED))?;

    // Return the same code as the test process, in case it failed.
    let info = process.info()?;
    zx::Process::exit(info.return_code);
}

async fn get_root_job() -> Result<zx::Job, Error> {
    let (proxy, server_end) = fidl::endpoints::create_proxy::<fsysinfo::DeviceMarker>()?;
    fdio::service_connect("/dev/misc/sysinfo", server_end.into_channel())
        .context("Failed to connect to sysinfo servie")?;

    let (status, job) = await!(proxy.get_root_job())?;
    zx::Status::ok(status)?;
    Ok(job.ok_or(err_msg("sysinfo returned OK status but no root job handle!"))?)
}
