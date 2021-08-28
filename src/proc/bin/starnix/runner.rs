// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, format_err, Context, Error};
use fidl::endpoints::ServerEnd;
use fidl::HandleBased;
use fidl_fuchsia_component as fcomponent;
use fidl_fuchsia_component_runner::{
    self as fcrunner, ComponentControllerMarker, ComponentStartInfo,
};
use fidl_fuchsia_io as fio;
use fidl_fuchsia_process as fprocess;
use fidl_fuchsia_starnix_developer as fstardev;
use fidl_fuchsia_sys2 as fsys;
use fuchsia_async as fasync;
use fuchsia_component::client as fclient;
use fuchsia_runtime::{HandleInfo, HandleType};
use fuchsia_zircon::{
    self as zx, sys::zx_exception_info_t, sys::zx_thread_state_general_regs_t,
    sys::ZX_EXCEPTION_STATE_HANDLED, sys::ZX_EXCEPTION_STATE_THREAD_EXIT,
    sys::ZX_EXCEPTION_STATE_TRY_NEXT, sys::ZX_EXCP_POLICY_CODE_BAD_SYSCALL,
    sys::ZX_EXCP_POLICY_ERROR, AsHandleRef, Task as zxTask,
};
use futures::TryStreamExt;
use log::{error, info};
use rand::Rng;
use std::convert::TryFrom;
use std::ffi::CString;
use std::mem;
use std::sync::Arc;

use crate::auth::Credentials;
use crate::errno;
use crate::fs::ext4::ExtFilesystem;
use crate::fs::fuchsia::{create_file_from_handle, RemoteFs};
use crate::fs::tmpfs::TmpFs;
use crate::fs::*;
use crate::signals::signal_handling::*;
use crate::strace;
use crate::syscalls::decls::SyscallDecl;
use crate::syscalls::table::dispatch_syscall;
use crate::syscalls::*;
use crate::task::*;
use crate::types::*;

// TODO: Should we move this code into fuchsia_zircon? It seems like part of a better abstraction
// for exception channels.
fn as_exception_info(buffer: &zx::MessageBuf) -> zx_exception_info_t {
    let mut tmp = [0; mem::size_of::<zx_exception_info_t>()];
    tmp.clone_from_slice(buffer.bytes());
    unsafe { mem::transmute(tmp) }
}

fn read_channel_sync(chan: &zx::Channel, buf: &mut zx::MessageBuf) -> Result<(), zx::Status> {
    let res = chan.read(buf);
    if let Err(zx::Status::SHOULD_WAIT) = res {
        chan.wait_handle(
            zx::Signals::CHANNEL_READABLE | zx::Signals::CHANNEL_PEER_CLOSED,
            zx::Time::INFINITE,
        )?;
        chan.read(buf)
    } else {
        res
    }
}

/// Runs the given task.
///
/// The task is expected to already have been started. This function listens to
/// the exception channel for the process (`exceptions`) and handles each
///  exception by:
///
///   - verifying that the exception represents a `ZX_EXCP_POLICY_CODE_BAD_SYSCALL`
///   - reading the thread's registers
///   - executing the appropriate syscall
///   - setting the thread's registers to their post-syscall values
///   - setting the exception state to `ZX_EXCEPTION_STATE_HANDLED`
///
/// Once this function has completed, the process' exit code (if one is available) can be read from
/// `process_context.exit_code`.
fn run_task(task_owner: TaskOwner, exceptions: zx::Channel) -> Result<i32, Error> {
    let task = &task_owner.task;
    let mut buffer = zx::MessageBuf::new();
    loop {
        read_channel_sync(&exceptions, &mut buffer)?;

        let info = as_exception_info(&buffer);
        assert!(buffer.n_handles() == 1);
        let exception = zx::Exception::from(buffer.take_handle(0).unwrap());

        if info.type_ != ZX_EXCP_POLICY_ERROR {
            info!("exception type: 0x{:x}", info.type_);
            exception.set_exception_state(&ZX_EXCEPTION_STATE_TRY_NEXT)?;
            continue;
        }

        let thread = exception.get_thread()?;
        assert!(
            thread.get_koid() == task.thread.get_koid(),
            "Exception thread did not match task thread."
        );

        let report = thread.get_exception_report()?;
        if report.context.synth_code != ZX_EXCP_POLICY_CODE_BAD_SYSCALL {
            info!("exception synth_code: {}", report.context.synth_code);
            exception.set_exception_state(&ZX_EXCEPTION_STATE_TRY_NEXT)?;
            continue;
        }

        let syscall_number = report.context.synth_data as u64;
        let mut ctx = SyscallContext { task, registers: thread.read_state_general_regs()? };

        let regs = &ctx.registers;
        let args = (regs.rdi, regs.rsi, regs.rdx, regs.r10, regs.r8, regs.r9);
        strace!(
            task,
            "{}({:#x}, {:#x}, {:#x}, {:#x}, {:#x}, {:#x})",
            SyscallDecl::from_number(syscall_number).name,
            args.0,
            args.1,
            args.2,
            args.3,
            args.4,
            args.5
        );
        match dispatch_syscall(&mut ctx, syscall_number, args) {
            Ok(SyscallResult::Exit(error_code)) => {
                strace!(task, "-> exit {:#x}", error_code);
                exception.set_exception_state(&ZX_EXCEPTION_STATE_THREAD_EXIT)?;
                return Ok(error_code);
            }
            Ok(SyscallResult::Success(return_value)) => {
                strace!(task, "-> {:#x}", return_value);
                ctx.registers.rax = return_value;
            }
            Ok(SyscallResult::SigReturn) => {
                // Do not modify the register state of the thread. The sigreturn syscall has
                // restored the proper register state for the thread to continue with.
                strace!(task, "-> sigreturn");
            }
            Err(errno) => {
                strace!(task, "!-> {}", errno);
                ctx.registers.rax = (-errno.value()) as u64;
            }
        }

        dequeue_signal(&mut ctx);
        thread.write_state_general_regs(ctx.registers)?;
        exception.set_exception_state(&ZX_EXCEPTION_STATE_HANDLED)?;
    }
}

fn start_task(
    task: &Task,
    registers: zx_thread_state_general_regs_t,
) -> Result<zx::Channel, zx::Status> {
    let exceptions = task.thread.create_exception_channel()?;
    let suspend_token = task.thread.suspend()?;
    if task.id == task.thread_group.leader {
        task.thread_group.process.start(&task.thread, 0, 0, zx::Handle::invalid(), 0)?;
    } else {
        task.thread.start(0, 0, 0, 0)?;
    }
    task.thread.wait_handle(zx::Signals::THREAD_SUSPENDED, zx::Time::INFINITE)?;
    task.thread.write_state_general_regs(registers)?;
    mem::drop(suspend_token);
    Ok(exceptions)
}

pub fn spawn_task<F>(
    task_owner: TaskOwner,
    registers: zx_thread_state_general_regs_t,
    task_complete: F,
) where
    F: FnOnce(Result<i32, Error>) + Send + Sync + 'static,
{
    std::thread::spawn(move || {
        task_complete(|| -> Result<i32, Error> {
            let exceptions = start_task(&task_owner.task, registers)?;
            run_task(task_owner, exceptions)
        }());
    });
}

struct StartupHandles {
    files: Arc<FdTable>,
    shell_controller: Option<ServerEnd<fstardev::ShellControllerMarker>>,
}

/// Creates a `StartupHandles` from the provided handles.
///
/// The `numbered_handles` of type `HandleType::FileDescriptor` are used to
/// create files, and the handles are required to be of type `zx::Socket`.
///
/// If there is a `numbered_handles` of type `HandleType::User0`, that is
/// interpreted as the server end of the ShellController protocol.
fn parse_numbered_handles(
    numbered_handles: Option<Vec<fprocess::HandleInfo>>,
    kernel: &Kernel,
) -> Result<StartupHandles, Error> {
    let files = FdTable::new();
    let mut shell_controller = None;
    if let Some(numbered_handles) = numbered_handles {
        for numbered_handle in numbered_handles {
            let info = HandleInfo::try_from(numbered_handle.id)?;
            if info.handle_type() == HandleType::FileDescriptor {
                files.insert(
                    FdNumber::from_raw(info.arg().into()),
                    create_file_from_handle(kernel, numbered_handle.handle)?,
                );
            } else if info.handle_type() == HandleType::User0 {
                shell_controller = Some(ServerEnd::<fstardev::ShellControllerMarker>::from(
                    numbered_handle.handle,
                ));
            }
        }
    }
    Ok(StartupHandles { files, shell_controller })
}

fn create_filesystem_from_spec<'a>(
    kernel: &Arc<Kernel>,
    task: Option<&Task>,
    pkg: &fio::DirectorySynchronousProxy,
    spec: &'a str,
) -> Result<(&'a [u8], WhatToMount), Error> {
    use WhatToMount::*;
    let mut iter = spec.splitn(3, ':');
    let mount_point =
        iter.next().ok_or_else(|| anyhow!("mount point is missing from {:?}", spec))?;
    let fs_type = iter.next().ok_or_else(|| anyhow!("fs type is missing from {:?}", spec))?;
    let fs_src = iter.next().unwrap_or("");

    // The filesystem types handled in this match are the ones that can only be specified in a
    // manifest file, for whatever reason. Anything else is passed to create_filesystem, which is
    // common code that also handles the mount() system call.
    let fs = match fs_type {
        "bind" => {
            let task = task.ok_or(errno!(ENOENT))?;
            Dir(task.lookup_path_from_root(fs_src.as_bytes())?.entry)
        }
        "remotefs" => {
            let rights = fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE;
            let root = syncio::directory_open_directory_async(&pkg, &fs_src, rights)
                .map_err(|e| anyhow!("Failed to open root: {}", e))?;
            Fs(RemoteFs::new(root.into_channel(), rights))
        }
        "ext4" => {
            let vmo =
                syncio::directory_open_vmo(&pkg, &fs_src, fio::VMO_FLAG_READ, zx::Time::INFINITE)?;
            Fs(ExtFilesystem::new(vmo)?)
        }
        _ => create_filesystem(&kernel, fs_src.as_bytes(), fs_type.as_bytes(), b"")?,
    };
    Ok((mount_point.as_bytes(), fs))
}

fn start_component(
    kernel: Arc<Kernel>,
    start_info: ComponentStartInfo,
    controller: ServerEnd<ComponentControllerMarker>,
) -> Result<(), Error> {
    info!(
        "start_component: {}\narguments: {:?}\nmanifest: {:?}",
        start_info.resolved_url.clone().unwrap_or("<unknown>".to_string()),
        start_info.numbered_handles,
        start_info.program,
    );

    let mounts =
        runner::get_program_strvec(&start_info, "mounts").map(|a| a.clone()).unwrap_or(vec![]);
    let binary_path = CString::new(
        runner::get_program_string(&start_info, "binary")
            .ok_or_else(|| anyhow!("Missing \"binary\" in manifest"))?,
    )?;
    let args = runner::get_program_strvec(&start_info, "args")
        .map(|args| {
            args.iter().map(|arg| CString::new(arg.clone())).collect::<Result<Vec<CString>, _>>()
        })
        .unwrap_or(Ok(vec![]))?;
    let environ = runner::get_program_strvec(&start_info, "environ")
        .map(|args| {
            args.iter().map(|arg| CString::new(arg.clone())).collect::<Result<Vec<CString>, _>>()
        })
        .unwrap_or(Ok(vec![]))?;
    let user_passwd = runner::get_program_string(&start_info, "user").unwrap_or("fuchsia:x:42:42");
    let credentials = Credentials::from_passwd(user_passwd)?;
    let apex_hack = runner::get_program_strvec(&start_info, "apex_hack").map(|v| v.clone());

    info!("start_component environment: {:?}", environ);

    let ns = start_info.ns.ok_or_else(|| anyhow!("Missing namespace"))?;

    let pkg = fio::DirectorySynchronousProxy::new(
        ns.into_iter()
            .find(|entry| entry.path == Some("/pkg".to_string()))
            .ok_or_else(|| anyhow!("Missing /pkg entry in namespace"))?
            .directory
            .ok_or_else(|| anyhow!("Missing directory handlee in pkg namespace entry"))?
            .into_channel(),
    );

    // The mounts are appplied in the order listed. Mounting will fail if the designated mount
    // point doesn't exist in a previous mount. The root must be first so other mounts can be
    // applied on top of it.
    let mut mounts_iter = mounts.iter();
    let (root_point, root_fs) = create_filesystem_from_spec(
        &kernel,
        None,
        &pkg,
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

    let fs = FsContext::new(root_fs);
    let startup_handles = parse_numbered_handles(start_info.numbered_handles, &kernel)?;
    let files = startup_handles.files;
    let shell_controller = startup_handles.shell_controller;

    let task_owner =
        Task::create_process(&kernel, &binary_path, 0, files, fs.clone(), credentials, None)?;

    for mount_spec in mounts_iter {
        let (mount_point, child_fs) =
            create_filesystem_from_spec(&kernel, Some(&task_owner.task), &pkg, mount_spec)?;
        let mount_point = task_owner.task.lookup_path_from_root(mount_point)?;
        mount_point.mount(child_fs)?;
    }

    // Hack to allow mounting apexes before apexd is working.
    // TODO(tbodt): Remove once apexd works.
    if let Some(apexes) = apex_hack {
        let task = &task_owner.task;
        task.lookup_path_from_root(b"apex")?.mount(WhatToMount::Fs(TmpFs::new()))?;
        let apex_dir = task.lookup_path_from_root(b"apex")?;
        for apex in apexes {
            let apex = apex.as_bytes();
            let apex_subdir = apex_dir.create_node(
                apex,
                FileMode::IFDIR | FileMode::from_bits(0o700),
                DeviceType::NONE,
            )?;
            let apex_source = task.lookup_path_from_root(&[b"system/apex/", apex].concat())?;
            apex_subdir.mount(WhatToMount::Dir(apex_source.entry))?;
        }
    }

    let mut argv = vec![binary_path];
    argv.extend(args.into_iter());

    let start_info = task_owner.task.exec(&argv[0], &argv, &environ)?;

    spawn_task(task_owner, start_info.to_registers(), |result| {
        // TODO(fxb/74803): Using the component controller's epitaph may not be the best way to
        // communicate the exit code. The component manager could interpret certain epitaphs as starnix
        // being unstable, and chose to terminate starnix as a result.
        // Errors when closing the controller with an epitaph are disregarded, since there are
        // legitimate reasons for this to fail (like the client having closed the channel).
        if let Some(shell_controller) = shell_controller {
            let _ = shell_controller.close_with_epitaph(zx::Status::OK);
        }
        let _ = match result {
            Ok(0) => controller.close_with_epitaph(zx::Status::OK),
            _ => controller.close_with_epitaph(zx::Status::from_raw(
                fcomponent::Error::InstanceDied.into_primitive() as i32,
            )),
        };
    });

    Ok(())
}

pub async fn start_runner(
    kernel: Arc<Kernel>,
    mut request_stream: fcrunner::ComponentRunnerRequestStream,
) -> Result<(), Error> {
    while let Some(event) = request_stream.try_next().await? {
        match event {
            fcrunner::ComponentRunnerRequest::Start { start_info, controller, .. } => {
                let kernel = kernel.clone();
                fasync::Task::local(async move {
                    if let Err(e) = start_component(kernel, start_info, controller) {
                        error!("failed to start component: {:?}", e);
                    }
                })
                .detach();
            }
        }
    }
    Ok(())
}

async fn start(url: String, args: fsys::CreateChildArgs) -> Result<(), Error> {
    // TODO(fxbug.dev/74511): The amount of setup required here is a bit lengthy. Ideally,
    // fuchsia-component would provide native bindings for the Realm API that could reduce this
    // logic to a few lines.

    const COLLECTION: &str = "playground";
    let realm = fclient::realm().context("failed to connect to Realm service")?;
    let mut collection_ref = fsys::CollectionRef { name: COLLECTION.into() };
    let id: u64 = rand::thread_rng().gen();
    let child_name = format!("starnix-{}", id);
    let child_decl = fsys::ChildDecl {
        name: Some(child_name.clone()),
        url: Some(url),
        startup: Some(fsys::StartupMode::Lazy),
        environment: None,
        ..fsys::ChildDecl::EMPTY
    };
    let () = realm
        .create_child(&mut collection_ref, child_decl, args)
        .await?
        .map_err(|e| format_err!("failed to create child: {:?}", e))?;
    // The component is run in a `SingleRun` collection instance, and will be automatically
    // deleted when it exits.
    Ok(())
}

/// Creates a `HandleInfo` from the provided socket and file descriptor.
///
/// The file descriptor is encoded as a `PA_HND(PA_FD, <file_descriptor>)` before being stored in
/// the `HandleInfo`.
///
/// Returns an error if `socket` is `None`.
fn handle_info_from_socket(
    socket: Option<fidl::Socket>,
    file_descriptor: u16,
) -> Result<fprocess::HandleInfo, Error> {
    if let Some(socket) = socket {
        let info = HandleInfo::new(HandleType::FileDescriptor, file_descriptor);
        Ok(fprocess::HandleInfo { handle: socket.into_handle(), id: info.as_raw() })
    } else {
        Err(anyhow!("Failed to create HandleInfo for {}", file_descriptor))
    }
}

pub async fn start_manager(
    mut request_stream: fstardev::ManagerRequestStream,
) -> Result<(), Error> {
    while let Some(event) = request_stream.try_next().await? {
        match event {
            fstardev::ManagerRequest::Start { url, responder } => {
                let args = fsys::CreateChildArgs {
                    numbered_handles: None,
                    ..fsys::CreateChildArgs::EMPTY
                };
                if let Err(e) = start(url, args).await {
                    error!("failed to start component: {}", e);
                }
                responder.send()?;
            }
            fstardev::ManagerRequest::StartShell { params, controller, .. } => {
                let controller_handle_info = fprocess::HandleInfo {
                    handle: controller.into_channel().into_handle(),
                    id: HandleInfo::new(HandleType::User0, 0).as_raw(),
                };
                let numbered_handles = vec![
                    handle_info_from_socket(params.standard_in, 0)?,
                    handle_info_from_socket(params.standard_out, 1)?,
                    handle_info_from_socket(params.standard_err, 2)?,
                    controller_handle_info,
                ];
                let args = fsys::CreateChildArgs {
                    numbered_handles: Some(numbered_handles),
                    ..fsys::CreateChildArgs::EMPTY
                };
                if let Err(e) = start(
                    "fuchsia-pkg://fuchsia.com/test_android_distro#meta/sh.cm".to_string(),
                    args,
                )
                .await
                {
                    error!("failed to start shell: {}", e);
                }
            }
        }
    }
    Ok(())
}
