// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Run a test binary as a new process under the root job.
//!
//! This is needed since most or all jobs besides the root job have restricted job policy that
//! disallows use of the zx_process_create syscall, which process_builder uses. Processes that use
//! process_builder normally run in the root job, so we need a similar environment for the test.
//!
//! Note that any Rust test using this runner will usually need the `--force-run-in-process` flag
//! passed to the test binary. This ensures that each unit test is run in the custom environment
//! we create, not a generic subprocess of it. The drawback is that should_panic tests are NOT
//! supported with this flag.
//!
//! This approach is a temporary hack. It relies on the fact that the root job is available on
//! certain builds and to v1 test components through the fuchsia.boot.RootJob service.
//!
//! TODO: Figure out a better way to run these tests.

use {
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints::ServiceMarker,
    fidl_fidl_examples_echo::EchoMarker,
    fidl_fuchsia_boot as fboot, fidl_fuchsia_kernel as fkernel, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::{self as zx, HandleBased},
    futures::prelude::*,
    std::env,
    std::ffi::{CStr, CString},
    std::fs,
    std::path::{Path, PathBuf},
};

/// Create a ServiceFs that proxies through limited services from our namespace. Specifically, we
/// don't pass through fuchsia.process.Launcher, which this process needs to use fdio_spawn but
/// which we don't want in the actual test process' namespace to ensure the test process isn't
/// incorrectly using it rather than serving its own.
fn serve_proxy_svc_dir() -> Result<zx::Channel, Error> {
    let mut fs = ServiceFs::new_local();

    // This can be expanded to proxy additional services as needed, though it's not totally clear
    // how to generalize this to loop over a set over markers.
    let echo_path = PathBuf::from(format!("/svc/{}", EchoMarker::NAME));
    if echo_path.exists() {
        fs.add_proxy_service::<EchoMarker, _>();
    }

    let root_resource_path = PathBuf::from(format!("/svc/{}", fboot::RootResourceMarker::NAME));
    if root_resource_path.exists() {
        fs.add_proxy_service::<fboot::RootResourceMarker, _>();
    }

    let mmio_resource_path = PathBuf::from(format!("/svc/{}", fkernel::MmioResourceMarker::NAME));
    if mmio_resource_path.exists() {
        fs.add_proxy_service::<fkernel::MmioResourceMarker, _>();
    }

    let (client, server) = zx::Channel::create().expect("Failed to create channel");
    fs.serve_connection(server)?;
    fasync::Task::local(fs.collect()).detach();
    Ok(client)
}

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

    // Provide the test process with a namespace containing only /pkg and a more limited /svc.
    //
    // We also clone stdout and stderr, but leave stdin as null. This is to test that "cloning"
    // a null stdio works when spawning processes (see bug 35902).
    let svc_str = CString::new("/svc")?;
    let pkg_str = CString::new("/pkg")?;
    let pkg_dir = fs::File::open("/pkg")?;
    let stdout = std::io::stdout();
    let stderr = std::io::stderr();
    let mut actions = vec![
        fdio::SpawnAction::add_namespace_entry(&svc_str, serve_proxy_svc_dir()?.into_handle()),
        fdio::SpawnAction::add_namespace_entry(&pkg_str, fdio::transfer_fd(pkg_dir)?),
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
        & !fdio::SpawnOptions::CLONE_NAMESPACE
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
    let (proxy, server_end) = fidl::endpoints::create_proxy::<fboot::RootJobMarker>()?;
    fdio::service_connect("/svc/fuchsia.boot.RootJob", server_end.into_channel())
        .context("Failed to connect to fuchsia.boot.RootJob service")?;

    let job = proxy.get().await?;
    Ok(job)
}
