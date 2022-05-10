// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Error};
use fidl_fuchsia_io as fio;
use fuchsia_zircon as zx;
use starnix_runner_config::Config;
use std::ffi::CString;
use std::sync::Arc;

use crate::auth::Credentials;
use crate::device::run_features;
use crate::execution::*;
use crate::fs::tmpfs::TmpFs;
use crate::fs::*;
use crate::task::*;
use crate::types::*;

lazy_static::lazy_static! {
    /// The configuration for the starnix runner. This is static because reading the configuration
    /// consumes a startup handle, and thus can only be done once per component-run.
    static ref CONFIG: Config = Config::take_from_startup_handle();
}

// Creates a CString from a String. Calling this with an invalid CString will panic.
fn to_cstr(str: &String) -> CString {
    CString::new(str.clone()).unwrap()
}

pub struct Galaxy {
    /// The initial task in the galaxy, if one was specified in the galaxy's CONFIGuration file.
    ///
    /// This task is executed prior to running any other tasks in the galaxy.
    pub init_task: Option<CurrentTask>,

    /// A path to a file in the `init_task`'s filesystem. If this file is `Some`, then the runner
    /// machinery will wait for the file to exist before executing any other tasks in the galaxy
    /// (`init_task` is started, since it is expected to create this file).
    pub startup_file_path: Option<String>,

    /// The `Kernel` object that is associated with the galaxy.
    pub kernel: Arc<Kernel>,

    /// The root filesystem context for the galaxy.
    pub root_fs: Arc<FsContext>,
}

/// Creates a new galaxy.
///
/// A galaxy contains a `Kernel`, and an optional `init_task`. The returned init task is expected to
/// execute before any other tasks are executed.
///
/// If a `startup_file_path` is also returned, the caller should wait to start any other tasks
/// until the file at `startup_file_path` exists.
///
/// # Parameters
/// - `outgoing_dir`: The outgoing directory of the component to run in the galaxy. This is used
///                   to serve protocols on behalf of the component.
pub fn create_galaxy(
    outgoing_dir: &mut Option<fidl::endpoints::ServerEnd<fidl_fuchsia_io::DirectoryMarker>>,
) -> Result<Galaxy, Error> {
    const COMPONENT_PKG_PATH: &'static str = "/pkg";

    let (server, client) = zx::Channel::create().context("failed to create channel pair")?;
    fdio::open(
        COMPONENT_PKG_PATH,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        server,
    )
    .context("failed to open /pkg")?;
    let pkg_dir_proxy = fio::DirectorySynchronousProxy::new(client);
    let mut kernel = Kernel::new(&to_cstr(&CONFIG.name))?;
    kernel.cmdline = CONFIG.kernel_cmdline.as_bytes().to_vec();
    *kernel.outgoing_dir.lock() = outgoing_dir.take().map(|server_end| server_end.into_channel());
    let kernel = Arc::new(kernel);

    let fs_context = create_fs_context(&kernel, &pkg_dir_proxy)?;
    let mut init_task = create_init_task(&kernel, &fs_context)?;

    mount_filesystems(&init_task, &pkg_dir_proxy)?;

    // Hack to allow mounting apexes before apexd is working.
    // TODO(tbodt): Remove once apexd works.
    mount_apexes(&init_task)?;

    // Run all the features (e.g., wayland) that were specified in the .cml.
    run_features(&CONFIG.features, &init_task)
        .map_err(|e| anyhow!("Failed to initialize features: {:?}", e))?;
    // TODO: This should probably be part of the "feature" CONFIGuration.
    let kernel = init_task.kernel().clone();

    let root_fs = init_task.fs.clone();
    // Only return an init task if there was an init binary path. The task struct is still used
    // to initialize the system up until this point, regardless of whether or not there is an
    // actual init to be run.
    let init_task = if CONFIG.init.is_empty() {
        // A task must have an exit status, so set it here to simulate the init task having run.
        init_task.write().exit_status = Some(ExitStatus::Exit(0));
        None
    } else {
        let argv: Vec<_> = CONFIG.init.iter().map(to_cstr).collect();
        init_task.exec(argv[0].clone(), argv.clone(), vec![])?;
        Some(init_task)
    };

    let startup_file_path = if CONFIG.startup_file_path.is_empty() {
        None
    } else {
        Some(CONFIG.startup_file_path.clone())
    };
    Ok(Galaxy { init_task, startup_file_path, kernel, root_fs })
}

fn create_fs_context(
    kernel: &Arc<Kernel>,
    pkg_dir_proxy: &fio::DirectorySynchronousProxy,
) -> Result<Arc<FsContext>, Error> {
    // The mounts are appplied in the order listed. Mounting will fail if the designated mount
    // point doesn't exist in a previous mount. The root must be first so other mounts can be
    // applied on top of it.
    let mut mounts_iter = CONFIG.mounts.iter();
    let (root_point, root_fs) = create_filesystem_from_spec(
        &kernel,
        None,
        &pkg_dir_proxy,
        mounts_iter.next().ok_or_else(|| anyhow!("Mounts list is empty"))?,
    )?;
    if root_point != b"/" {
        anyhow::bail!("First mount in mounts list is not the root");
    }
    let root_fs = if let WhatToMount::Fs(fs) = root_fs {
        fs
    } else {
        anyhow::bail!("how did a bind mount manage to get created as the root?")
    };

    Ok(FsContext::new(root_fs))
}

fn mount_apexes(init_task: &CurrentTask) -> Result<(), Error> {
    if !CONFIG.apex_hack.is_empty() {
        init_task
            .lookup_path_from_root(b"apex")?
            .mount(WhatToMount::Fs(TmpFs::new()), MountFlags::empty())?;
        let apex_dir = init_task.lookup_path_from_root(b"apex")?;
        for apex in &CONFIG.apex_hack {
            let apex = apex.as_bytes();
            let apex_subdir = apex_dir.create_node(
                apex,
                FileMode::IFDIR | FileMode::from_bits(0o700),
                DeviceType::NONE,
            )?;
            let apex_source = init_task.lookup_path_from_root(&[b"system/apex/", apex].concat())?;
            apex_subdir.mount(WhatToMount::Dir(apex_source.entry), MountFlags::empty())?;
        }
    }
    Ok(())
}

fn create_init_task(kernel: &Arc<Kernel>, fs: &Arc<FsContext>) -> Result<CurrentTask, Error> {
    let credentials = Credentials::from_passwd(&CONFIG.init_user)?;
    let name =
        if CONFIG.init.is_empty() { to_cstr(&String::new()) } else { to_cstr(&CONFIG.init[0]) };
    let init_task = Task::create_process_without_parent(kernel, name, fs.clone())?;
    init_task.write().creds = credentials;
    Ok(init_task)
}

fn mount_filesystems(
    init_task: &CurrentTask,
    pkg_dir_proxy: &fio::DirectorySynchronousProxy,
) -> Result<(), Error> {
    let mut mounts_iter = CONFIG.mounts.iter();
    // Skip the first mount, that was used to create the root filesystem.
    let _ = mounts_iter.next();
    for mount_spec in mounts_iter {
        let (mount_point, child_fs) = create_filesystem_from_spec(
            init_task.kernel(),
            Some(&init_task),
            pkg_dir_proxy,
            mount_spec,
        )?;
        let mount_point = init_task.lookup_path_from_root(mount_point)?;
        mount_point.mount(child_fs, MountFlags::empty())?;
    }
    Ok(())
}
