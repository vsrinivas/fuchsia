// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Error};
use fidl::endpoints::Proxy;
use fidl_fuchsia_io as fio;
use fuchsia_async as fasync;
use serde::Deserialize;
use std::ffi::CString;
use std::sync::Arc;

use crate::auth::Credentials;
use crate::device::run_features;
use crate::execution::*;
use crate::fs::tmpfs::TmpFs;
use crate::fs::*;
use crate::task::*;
use crate::types::*;

/// The arguments for the `Galaxy`.
///
/// These arguments are read from a configuration file in the Starnix runner's package. Thus one can
/// create different runtime environments (galaxies) by packaging the Starnix runner component with
/// different configuration files and system images.
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Arguments {
    /// The filesystems that get mounted when the galaxy is created.
    #[serde(default)]
    mounts: Vec<String>,

    /// Hack for specifying apexes.
    #[serde(default)]
    apexes: Vec<CString>,

    /// A path to a binary, inside of the system image, that is expected to run as the first task
    /// in the galaxy.
    #[serde(default)]
    init_binary_path: Option<CString>,

    /// The arguments for the `init` task, if such a task exists.
    #[serde(default)]
    init_args: Vec<CString>,

    /// The environment for the `init` task.
    #[serde(default)]
    init_environ: Vec<CString>,

    /// The user string to use when creating the `init` task.
    #[serde(default = "default_user")]
    init_user: String,

    /// The command line arguments for the `Kernel`.
    #[serde(default)]
    kernel_cmdline: CString,

    /// The features to run in the galaxy (e.g., wayland support).
    #[serde(default)]
    features: Vec<String>,

    /// The name of the galaxy.
    #[serde(default = "default_kernel_name")]
    name: CString,
}

fn default_user() -> String {
    "fuchsia:x:42:42".to_string()
}

fn default_kernel_name() -> CString {
    CString::new("kernel").expect("Failed to create default kernel name")
}

pub struct Galaxy {
    /// The initial task in the galaxy, if one was specified in the galaxy's configuration file.
    ///
    /// This task is executed prior to running any other tasks in the galaxy.
    pub init_task: Option<CurrentTask>,

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
/// # Parameters
/// - `outgoing_dir`: The outgoing directory of the component to run in the galaxy. This is used
///                   to serve protocols on behalf of the component.
pub fn create_galaxy(
    outgoing_dir: &mut Option<fidl::endpoints::ServerEnd<fidl_fuchsia_io::DirectoryMarker>>,
) -> Result<Galaxy, Error> {
    const CONFIGURATION_FILE_PATH: &'static str = "/pkg/data/runner.config";
    const COMPONENT_PKG_PATH: &'static str = "/pkg";
    let galaxy_args: Arguments =
        serde_json::from_str(&std::fs::read_to_string(CONFIGURATION_FILE_PATH)?)?;

    let chan: fasync::Channel =
        io_util::open_directory_in_namespace(COMPONENT_PKG_PATH, io_util::OPEN_RIGHT_READABLE)?
            .into_channel()
            .map_err(|_| anyhow!("Failed to open pkg"))?;
    let pkg_dir_proxy = fio::DirectorySynchronousProxy::new(chan.into_zx_channel());
    let mut kernel = Kernel::new(&galaxy_args.name)?;
    kernel.cmdline = galaxy_args.kernel_cmdline.as_bytes().to_vec();
    *kernel.outgoing_dir.lock() = outgoing_dir.take().map(|server_end| server_end.into_channel());
    let kernel = Arc::new(kernel);

    let fs_context = create_fs_context(&galaxy_args, &kernel, &pkg_dir_proxy)?;
    let mut init_task = create_init_task(&galaxy_args, &kernel, &fs_context)?;

    mount_filesystems(&galaxy_args, &init_task, &pkg_dir_proxy)?;

    // Hack to allow mounting apexes before apexd is working.
    // TODO(tbodt): Remove once apexd works.
    mount_apexes(&galaxy_args, &init_task)?;

    // Run all the features (e.g., wayland) that were specified in the .cml.
    run_features(&galaxy_args.features, &init_task)
        .map_err(|e| anyhow!("Failed to initialize features: {:?}", e))?;
    // TODO: This should probably be part of the "feature" configuration.
    let kernel = init_task.kernel().clone();

    let root_fs = init_task.fs.clone();
    // Only return an init task if there was an init binary path. The task struct is still used
    // to initialize the system up until this point, regardless of whether or not there is an
    // actual init to be run.
    let init_task = if let Some(binary_path) = galaxy_args.init_binary_path {
        let mut argv = vec![binary_path.clone()];
        argv.extend(galaxy_args.init_args.into_iter());
        init_task.exec(argv[0].clone(), argv.clone(), galaxy_args.init_environ)?;
        Some(init_task)
    } else {
        // A task must have an exit code, so set it here to simulate the init task having run.
        *init_task.exit_code.lock() = Some(0);
        None
    };

    Ok(Galaxy { init_task, kernel, root_fs })
}

fn create_fs_context(
    args: &Arguments,
    kernel: &Arc<Kernel>,
    pkg_dir_proxy: &fio::DirectorySynchronousProxy,
) -> Result<Arc<FsContext>, Error> {
    // The mounts are appplied in the order listed. Mounting will fail if the designated mount
    // point doesn't exist in a previous mount. The root must be first so other mounts can be
    // applied on top of it.
    let mut mounts_iter = args.mounts.iter();
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

fn mount_apexes(args: &Arguments, init_task: &CurrentTask) -> Result<(), Error> {
    if !args.apexes.is_empty() {
        init_task
            .lookup_path_from_root(b"apex")?
            .mount(WhatToMount::Fs(TmpFs::new()), MountFlags::empty())?;
        let apex_dir = init_task.lookup_path_from_root(b"apex")?;
        for apex in &args.apexes {
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

fn create_init_task(
    args: &Arguments,
    kernel: &Arc<Kernel>,
    fs: &Arc<FsContext>,
) -> Result<CurrentTask, Error> {
    let credentials = Credentials::from_passwd(&args.init_user)?;
    let name = args.init_binary_path.clone().unwrap_or_default();
    let init_task = Task::create_process_without_parent(kernel, name, fs.clone())?;
    *init_task.creds.write() = credentials;
    Ok(init_task)
}

fn mount_filesystems(
    args: &Arguments,
    init_task: &CurrentTask,
    pkg_dir_proxy: &fio::DirectorySynchronousProxy,
) -> Result<(), Error> {
    let mut mounts_iter = args.mounts.iter();
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
