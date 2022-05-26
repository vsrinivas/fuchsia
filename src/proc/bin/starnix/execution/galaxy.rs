// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Error};
use fidl_fuchsia_io as fio;
use fuchsia_async as fasync;
use fuchsia_async::DurationExt;
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
    /// The `Kernel` object that is associated with the galaxy.
    pub kernel: Arc<Kernel>,

    /// The root filesystem context for the galaxy.
    pub root_fs: Arc<FsContext>,
}

/// Creates a new galaxy.
///
/// If the CONFIG specifies an init task, it is run before
/// returning from create_galaxy and optionally waits for
/// a startup file to be created.
pub async fn create_galaxy() -> Result<Galaxy, Error> {
    const COMPONENT_PKG_PATH: &'static str = "/pkg";

    let (server, client) = zx::Channel::create().context("failed to create channel pair")?;
    fdio::open(
        COMPONENT_PKG_PATH,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        server,
    )
    .context("failed to open /pkg")?;
    let pkg_dir_proxy = fio::DirectorySynchronousProxy::new(client);
    let mut kernel = Kernel::new(&to_cstr(&CONFIG.name), &CONFIG.features)?;
    kernel.cmdline = CONFIG.kernel_cmdline.as_bytes().to_vec();
    let kernel = Arc::new(kernel);

    let fs_context = create_fs_context(&kernel, &pkg_dir_proxy)?;
    let mut init_task = create_init_task(&kernel, &fs_context)?;

    mount_filesystems(&init_task, &pkg_dir_proxy)?;

    // Hack to allow mounting apexes before apexd is working.
    // TODO(tbodt): Remove once apexd works.
    mount_apexes(&init_task)?;

    // Run all common features that were specified in the .cml.
    run_features(&CONFIG.features, &init_task)
        .map_err(|e| anyhow!("Failed to initialize features: {:?}", e))?;
    // TODO: This should probably be part of the "feature" CONFIGuration.
    let kernel = init_task.kernel().clone();

    let root_fs = init_task.fs.clone();

    let startup_file_path = if CONFIG.startup_file_path.is_empty() {
        None
    } else {
        Some(CONFIG.startup_file_path.clone())
    };

    // If there is an init binary path, run it, optionally waiting for the
    // startup_file_path to be created. The task struct is still used
    // to initialize the system up until this point, regardless of whether
    // or not there is an actual init to be run.
    if CONFIG.init.is_empty() {
        // A task must have an exit status, so set it here to simulate the init task having run.
        init_task.write().exit_status = Some(ExitStatus::Exit(0));
    } else {
        let argv: Vec<_> = CONFIG.init.iter().map(to_cstr).collect();
        init_task.exec(argv[0].clone(), argv.clone(), vec![])?;
        execute_task(init_task, |result| {
            tracing::info!("Finished running init process: {:?}", result);
        });
        if let Some(startup_file_path) = startup_file_path {
            let init_wait_task = create_init_task(&kernel, &fs_context)?;
            wait_for_init_file(&startup_file_path, &init_wait_task).await?;
            init_wait_task.write().exit_status = Some(ExitStatus::Exit(0));
        }
    };

    Ok(Galaxy { kernel, root_fs })
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
            .mount(WhatToMount::Fs(TmpFs::new(init_task.kernel())), MountFlags::empty())?;
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

async fn wait_for_init_file(
    startup_file_path: &str,
    current_task: &CurrentTask,
) -> Result<(), Error> {
    // TODO(fxb/96299): Use inotify machinery to wait for the file.
    loop {
        fasync::Timer::new(fasync::Duration::from_millis(100).after_now()).await;
        let root = current_task.fs.namespace_root();
        let mut context = LookupContext::default();
        match current_task.lookup_path(&mut context, root, startup_file_path.as_bytes()) {
            Ok(_) => break,
            Err(error) if error == ENOENT => continue,
            Err(error) => return Err(anyhow::Error::from(error)),
        }
    }
    Ok(())
}

#[cfg(test)]
mod test {
    use super::wait_for_init_file;
    use crate::fs::FdNumber;
    use crate::testing::create_kernel_and_task;
    use crate::types::*;
    use fuchsia_async as fasync;
    use futures::{SinkExt, StreamExt};

    #[fuchsia::test]
    async fn test_init_file_already_exists() {
        let (_kernel, current_task) = create_kernel_and_task();
        let (mut sender, mut receiver) = futures::channel::mpsc::unbounded();

        let path = "/path";
        current_task
            .open_file_at(
                FdNumber::AT_FDCWD,
                &path.as_bytes(),
                OpenFlags::CREAT,
                FileMode::default(),
            )
            .expect("Failed to create file");

        fasync::Task::local(async move {
            wait_for_init_file(&path, &current_task).await.expect("failed to wait for file");
            sender.send(()).await.expect("failed to send message");
        })
        .detach();

        // Wait for the file creation to have been detected.
        assert!(receiver.next().await.is_some());
    }

    #[fuchsia::test]
    async fn test_init_file_wait_required() {
        let (_kernel, current_task) = create_kernel_and_task();
        let (mut sender, mut receiver) = futures::channel::mpsc::unbounded();

        let init_task = current_task.clone_task_for_test(CLONE_FS as u64);
        let path = "/path";

        fasync::Task::local(async move {
            sender.send(()).await.expect("failed to send message");
            wait_for_init_file(&path, &init_task).await.expect("failed to wait for file");
            sender.send(()).await.expect("failed to send message");
        })
        .detach();

        // Wait for message that file check has started.
        assert!(receiver.next().await.is_some());

        // Create the file that is being waited on.
        current_task
            .open_file_at(
                FdNumber::AT_FDCWD,
                &path.as_bytes(),
                OpenFlags::CREAT,
                FileMode::default(),
            )
            .expect("Failed to create file");

        // Wait for the file creation to be detected.
        assert!(receiver.next().await.is_some());
    }
}
