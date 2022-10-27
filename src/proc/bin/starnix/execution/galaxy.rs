// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Error};
use fidl_fuchsia_io as fio;
use fuchsia_async as fasync;
use fuchsia_async::DurationExt;
use fuchsia_inspect as inspect;
use fuchsia_zircon as zx;
use futures::FutureExt;
use starnix_runner_config::Config;
use std::collections::BTreeMap;
use std::ffi::CString;
use std::sync::Arc;

use crate::auth::Credentials;
use crate::device::run_features;
use crate::execution::*;
use crate::fs::layeredfs::LayeredFs;
use crate::fs::tmpfs::TmpFs;
use crate::fs::*;
use crate::task::*;
use crate::types::*;

lazy_static::lazy_static! {
    /// The configuration for the starnix runner. This is static because reading the configuration
    /// consumes a startup handle, and thus can only be done once per component-run.
    static ref CONFIG: Config = Config::take_from_startup_handle();

    static ref COMMAND: inspect::StringReference<'static> = "command".into();
    static ref PPID: inspect::StringReference<'static> = "ppid".into();
    static ref TASKS: inspect::StringReference<'static> = "tasks".into();
    static ref STOPPED: inspect::StringReference<'static> = "stopped".into();
}

// Creates a CString from a String. Calling this with an invalid CString will panic.
fn to_cstr(str: &str) -> CString {
    CString::new(str.to_string()).unwrap()
}

pub struct Galaxy {
    /// The `Kernel` object that is associated with the galaxy.
    pub kernel: Arc<Kernel>,

    /// The root filesystem context for the galaxy.
    pub root_fs: Arc<FsContext>,

    /// The system task to execute action as the system.
    pub system_task: CurrentTask,

    /// Inspect node holding information about the state of the galaxy.
    _node: inspect::Node,
}

impl Galaxy {
    pub fn create_process(&self, binary_path: &CString) -> Result<CurrentTask, Errno> {
        let task = Task::create_process_without_parent(
            &self.kernel,
            binary_path.clone(),
            Some(self.root_fs.clone()),
        )?;
        let init_task = self.kernel.pids.read().get_task(1);
        if let Some(init_task) = init_task {
            let mut init_writer = init_task.thread_group.write();
            let mut new_process_writer = task.thread_group.write();
            new_process_writer.parent = Some(init_task.thread_group.clone());
            init_writer.children.insert(task.id, Arc::downgrade(&task.thread_group));
        }
        Ok(task)
    }
}

/// Creates a new galaxy.
///
/// If the CONFIG specifies an init task, it is run before
/// returning from create_galaxy and optionally waits for
/// a startup file to be created.
pub async fn create_galaxy() -> Result<Galaxy, Error> {
    const COMPONENT_PKG_PATH: &str = "/pkg";
    const DEFAULT_INIT: &str = "/galaxy/init";

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

    let node = inspect::component::inspector().root().create_child("galaxy");
    create_galaxy_inspect(kernel.clone(), &node);

    let mut init_task = create_init_task(&kernel)?;
    let fs_context = create_fs_context(&init_task, &CONFIG.features, &pkg_dir_proxy)?;
    init_task.set_fs(fs_context.clone());
    let system_task = create_task(&kernel, Some(fs_context), "kthread", Credentials::root())?;

    mount_filesystems(&system_task, &pkg_dir_proxy)?;

    // Hack to allow mounting apexes before apexd is working.
    // TODO(tbodt): Remove once apexd works.
    mount_apexes(&system_task)?;

    // Run all common features that were specified in the .cml.
    run_features(&CONFIG.features, &system_task)
        .map_err(|e| anyhow!("Failed to initialize features: {:?}", e))?;
    // TODO: This should probably be part of the "feature" CONFIGuration.
    let kernel = init_task.kernel().clone();
    let root_fs = init_task.fs().clone();

    let startup_file_path = if CONFIG.startup_file_path.is_empty() {
        None
    } else {
        Some(CONFIG.startup_file_path.clone())
    };

    // If there is an init binary path, run it, optionally waiting for the
    // startup_file_path to be created. The task struct is still used
    // to initialize the system up until this point, regardless of whether
    // or not there is an actual init to be run.
    let argv =
        if CONFIG.init.is_empty() { vec![DEFAULT_INIT.to_string()] } else { CONFIG.init.clone() }
            .iter()
            .map(|s| to_cstr(s))
            .collect::<Vec<_>>();
    init_task.exec(argv[0].clone(), argv.clone(), vec![])?;
    execute_task(init_task, |result| {
        tracing::info!("Finished running init process: {:?}", result);
    });
    if let Some(startup_file_path) = startup_file_path {
        wait_for_init_file(&startup_file_path, &system_task).await?;
    };

    Ok(Galaxy { kernel, root_fs, system_task, _node: node })
}

fn create_fs_context(
    task: &CurrentTask,
    features: &[String],
    pkg_dir_proxy: &fio::DirectorySynchronousProxy,
) -> Result<Arc<FsContext>, Error> {
    // The mounts are appplied in the order listed. Mounting will fail if the designated mount
    // point doesn't exist in a previous mount. The root must be first so other mounts can be
    // applied on top of it.
    let mut mounts_iter = CONFIG.mounts.iter();
    let (root_point, root_fs) = create_filesystem_from_spec(
        task,
        pkg_dir_proxy,
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

    // Create a layered fs to handle /galaxy and /galaxy/pkg
    // /galaxy will mount the galaxy pkg
    // /galaxy/pkg will be a tmpfs where component using the starnix runner will have their package
    // mounted.
    let kernel = task.kernel();
    let rights = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE;
    let galaxy_fs = LayeredFs::new_fs(
        kernel,
        create_remotefs_filesystem(kernel, pkg_dir_proxy, rights, "data")?,
        BTreeMap::from([(b"pkg".to_vec(), TmpFs::new_fs(kernel))]),
    );
    let mut mappings =
        vec![(b"galaxy".to_vec(), galaxy_fs), (b"data".to_vec(), TmpFs::new_fs(kernel))];
    if features.contains(&"custom_artifacts".to_string()) {
        mappings.push((b"custom_artifacts".to_vec(), TmpFs::new_fs(kernel)));
    }
    let root_fs = LayeredFs::new_fs(kernel, root_fs, mappings.into_iter().collect());

    Ok(FsContext::new(root_fs))
}

fn mount_apexes(init_task: &CurrentTask) -> Result<(), Error> {
    if !CONFIG.apex_hack.is_empty() {
        init_task
            .lookup_path_from_root(b"apex")?
            .mount(WhatToMount::Fs(TmpFs::new_fs(init_task.kernel())), MountFlags::empty())?;
        let apex_dir = init_task.lookup_path_from_root(b"apex")?;
        for apex in &CONFIG.apex_hack {
            let apex = apex.as_bytes();
            let apex_subdir =
                apex_dir.create_node(init_task, apex, mode!(IFDIR, 0o700), DeviceType::NONE)?;
            let apex_source = init_task.lookup_path_from_root(&[b"system/apex/", apex].concat())?;
            apex_subdir.mount(WhatToMount::Bind(apex_source), MountFlags::empty())?;
        }
    }
    Ok(())
}

fn create_task(
    kernel: &Arc<Kernel>,
    fs: Option<Arc<FsContext>>,
    name: &str,
    credentials: Credentials,
) -> Result<CurrentTask, Error> {
    let task = Task::create_process_without_parent(kernel, to_cstr(name), fs)?;
    task.set_creds(credentials);
    Ok(task)
}

fn create_init_task(kernel: &Arc<Kernel>) -> Result<CurrentTask, Error> {
    let credentials = Credentials::from_passwd(&CONFIG.init_user)?;
    let name = if CONFIG.init.is_empty() { "" } else { &CONFIG.init[0] };
    create_task(kernel, None, name, credentials)
}

fn mount_filesystems(
    system_task: &CurrentTask,
    pkg_dir_proxy: &fio::DirectorySynchronousProxy,
) -> Result<(), Error> {
    let mut mounts_iter = CONFIG.mounts.iter();
    // Skip the first mount, that was used to create the root filesystem.
    let _ = mounts_iter.next();
    for mount_spec in mounts_iter {
        let (mount_point, child_fs) =
            create_filesystem_from_spec(system_task, pkg_dir_proxy, mount_spec)?;
        let mount_point = system_task.lookup_path_from_root(mount_point)?;
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
        let root = current_task.fs().root();
        let mut context = LookupContext::default();
        match current_task.lookup_path(&mut context, root, startup_file_path.as_bytes()) {
            Ok(_) => break,
            Err(error) if error == ENOENT => continue,
            Err(error) => return Err(anyhow::Error::from(error)),
        }
    }
    Ok(())
}

/// Creates a lazy node that will contain the Kernel thread groups state.
fn create_galaxy_inspect(kernel: Arc<Kernel>, parent: &inspect::Node) {
    parent.record_lazy_child("kernel", move || {
        let inspector = inspect::Inspector::new();
        let thread_groups = inspector.root().create_child("thread_groups");
        for thread_group in kernel.pids.read().get_thread_groups() {
            let tg = thread_group.read();

            let tg_node = thread_groups.create_child(&format!("{}", thread_group.leader));
            tg_node.record_int(&*PPID, tg.get_ppid() as i64);
            tg_node.record_bool(&*STOPPED, tg.stopped);

            let tasks_node = tg_node.create_child(&*TASKS);
            for task in tg.tasks() {
                if task.id == thread_group.leader {
                    record_task_command_to_node(&task, &*COMMAND, &tg_node);
                    continue;
                }
                record_task_command_to_node(&task, &format!("{}", task.id), &tasks_node);
            }
            tg_node.record(tasks_node);
            thread_groups.record(tg_node);
        }
        inspector.root().record(thread_groups);

        async move { Ok(inspector) }.boxed()
    });
}

fn record_task_command_to_node<'a>(
    task: &Arc<Task>,
    name: impl Into<inspect::StringReference<'a>>,
    node: &inspect::Node,
) {
    match task.command().to_str() {
        Ok(command) => node.record_string(name, command),
        Err(err) => node.record_string(name, &format!("{}", err)),
    }
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
                path.as_bytes(),
                OpenFlags::CREAT,
                FileMode::default(),
            )
            .expect("Failed to create file");

        fasync::Task::local(async move {
            wait_for_init_file(path, &current_task).await.expect("failed to wait for file");
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
            wait_for_init_file(path, &init_task).await.expect("failed to wait for file");
            sender.send(()).await.expect("failed to send message");
        })
        .detach();

        // Wait for message that file check has started.
        assert!(receiver.next().await.is_some());

        // Create the file that is being waited on.
        current_task
            .open_file_at(
                FdNumber::AT_FDCWD,
                path.as_bytes(),
                OpenFlags::CREAT,
                FileMode::default(),
            )
            .expect("Failed to create file");

        // Wait for the file creation to be detected.
        assert!(receiver.next().await.is_some());
    }
}
