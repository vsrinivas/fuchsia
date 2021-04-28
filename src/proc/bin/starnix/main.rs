// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, format_err, Context, Error},
    fidl::endpoints::{self, ServerEnd},
    fidl_fuchsia_component as fcomponent,
    fidl_fuchsia_component_runner::{
        self as fcrunner, ComponentControllerMarker, ComponentStartInfo,
    },
    fidl_fuchsia_io as fio, fidl_fuchsia_starnix_developer as fstardev, fidl_fuchsia_sys2 as fsys,
    fuchsia_async as fasync,
    fuchsia_component::client as fclient,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::{
        self as zx, sys::zx_exception_info_t, sys::ZX_EXCEPTION_STATE_HANDLED,
        sys::ZX_EXCEPTION_STATE_TRY_NEXT, sys::ZX_EXCP_POLICY_CODE_BAD_SYSCALL,
        sys::ZX_EXCP_POLICY_ERROR, AsHandleRef, Task as zxTask,
    },
    futures::{StreamExt, TryStreamExt},
    io_util::directory,
    log::{error, info},
    rand::Rng,
    std::ffi::CString,
    std::mem,
    std::sync::Arc,
};

mod auth;
mod fs;
mod loader;
mod mm;
mod syscall_table;
mod syscalls;
mod task;
mod uapi;

use loader::*;
use syscall_table::dispatch_syscall;
use syscalls::SyscallContext;
use task::*;
use uapi::SyscallDecl;
use uapi::SyscallResult;

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

/// Runs the process associated with `process_context`.
///
/// The process in `process_context.handle` is expected to already have been started. This function
/// listens to the exception channel for the process (`exceptions`) and handles each exception
/// by:
///   - verifying that the exception represents a `ZX_EXCP_POLICY_CODE_BAD_SYSCALL`
///   - reading the thread's registers
///   - executing the appropriate syscall
///   - setting the thread's registers to their post-syscall values
///   - setting the exception state to `ZX_EXCEPTION_STATE_HANDLED`
///
/// Once this function has completed, the process' exit code (if one is available) can be read from
/// `process_context.exit_code`.
fn run_task(task: Arc<Task>, exceptions: zx::Channel) -> Result<i32, Error> {
    let mut buffer = zx::MessageBuf::new();
    while read_channel_sync(&exceptions, &mut buffer).is_ok() {
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
        let mut ctx = SyscallContext { task: &task, registers: thread.read_state_general_regs()? };

        let regs = &ctx.registers;
        let args = (regs.rdi, regs.rsi, regs.rdx, regs.r10, regs.r8, regs.r9);
        info!(target: "strace", "{}({:#x}, {:#x}, {:#x}, {:#x}, {:#x}, {:#x})", SyscallDecl::from_number(syscall_number).name, args.0, args.1, args.2, args.3, args.4, args.5);
        match dispatch_syscall(&mut ctx, syscall_number, args) {
            Ok(SyscallResult::Exit(return_value)) => {
                info!(target: "strace", "-> exit {:#x}", return_value);
                // The process has been killed at this point, so execution is stopped. If, for
                // example, this function continued to attempt to write to th thread's registers
                // there would be a race between the process teardown and the register write.
                return Ok(return_value);
            }
            Ok(SyscallResult::Success(return_value)) => {
                info!(target: "strace", "-> {:#x}", return_value);
                ctx.registers.rax = return_value;
            }
            Err(errno) => {
                info!(target: "strace", "!-> {}", errno);
                ctx.registers.rax = (-errno.value()) as u64;
            }
        }

        thread.write_state_general_regs(ctx.registers)?;
        exception.set_exception_state(&ZX_EXCEPTION_STATE_HANDLED)?;
    }

    Ok(0)
}

async fn start_component(
    kernel: Arc<Kernel>,
    start_info: ComponentStartInfo,
    controller: ServerEnd<ComponentControllerMarker>,
) -> Result<(), Error> {
    info!(
        "start_component: {}",
        start_info.resolved_url.clone().unwrap_or("<unknown>".to_string())
    );

    let root_path = runner::get_program_string(&start_info, "root")
        .ok_or_else(|| anyhow!("No root in component manifest"))?
        .to_owned();
    let binary_path = runner::get_program_binary(&start_info)?;
    let ns = start_info.ns.ok_or_else(|| anyhow!("Missing namespace"))?;

    let pkg_proxy = ns
        .into_iter()
        .find(|entry| entry.path == Some("/pkg".to_string()))
        .ok_or_else(|| anyhow!("Missing /pkg entry in namespace"))?
        .directory
        .ok_or_else(|| anyhow!("Missing directory handlee in pkg namespace entry"))?
        .into_proxy()
        .context("failed to open /pkg")?;
    let root_proxy = directory::open_directory(
        &pkg_proxy,
        &root_path,
        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
    )
    .await
    .context("failed to open root")?;

    let executable_vmo = library_loader::load_vmo(&root_proxy, &binary_path)
        .await
        .context("failed to load executable")?;

    let params = ProcessParameters {
        name: CString::new(binary_path.clone())?,
        argv: vec![CString::new(binary_path.clone())?],
        environ: vec![],
    };

    std::thread::spawn(move || {
        let err = (|| -> Result<(), Error> {
            let task = block_on(load_executable(&kernel, executable_vmo, &params, root_proxy))?;
            let exceptions = task.thread.create_exception_channel()?;
            let exit_code = run_task(task, exceptions)?;

            // TODO(fxb/74803): Using the component controller's epitaph may not be the best way to
            // communicate the exit code. The component manager could interpret certain epitaphs as starnix
            // being unstable, and chose to terminate starnix as a result.
            // Errors when closing the controller with an epitaph are disregarded, since there are
            // legitimate reasons for this to fail (like the client having closed the channel).
            let _ = match exit_code {
                0 => controller.close_with_epitaph(zx::Status::OK),
                _ => controller.close_with_epitaph(zx::Status::from_raw(
                    fcomponent::Error::InstanceDied.into_primitive() as i32,
                )),
            };
            Ok(())
        })();
        err.unwrap();
    });
    Ok(())
}

async fn start_runner(
    kernel: Arc<Kernel>,
    mut request_stream: fcrunner::ComponentRunnerRequestStream,
) -> Result<(), Error> {
    while let Some(event) = request_stream.try_next().await? {
        match event {
            fcrunner::ComponentRunnerRequest::Start { start_info, controller, .. } => {
                let kernel = kernel.clone();
                fasync::Task::local(async move {
                    if let Err(e) = start_component(kernel, start_info, controller).await {
                        error!("failed to start component: {}", e);
                    }
                })
                .detach();
            }
        }
    }
    Ok(())
}

async fn start(url: String) -> Result<(), Error> {
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
        .create_child(&mut collection_ref, child_decl)
        .await?
        .map_err(|e| format_err!("failed to create child: {:?}", e))?;
    let mut child_ref =
        fsys::ChildRef { name: child_name.clone(), collection: Some(COLLECTION.into()) };
    let (_, server) = endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>()?;
    let () = realm
        .bind_child(&mut child_ref, server)
        .await?
        .map_err(|e| format_err!("failed to bind to child: {:?}", e))?;
    // We currently don't track the instance so we will never terminate it. Eventually, we'll keep
    // track of all the running instances and be able to stop them.
    Ok(())
}

async fn start_manager(mut request_stream: fstardev::ManagerRequestStream) -> Result<(), Error> {
    while let Some(event) = request_stream.try_next().await? {
        match event {
            fstardev::ManagerRequest::Start { url, responder } => {
                if let Err(e) = start(url).await {
                    error!("failed to start component: {}", e);
                }
                responder.send()?;
            }
            fstardev::ManagerRequest::StartShell { .. } => {
                info!("StartShell not yet implemented.")
            }
        }
    }
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["starnix"]).expect("failed to initialize logger");
    info!("main");

    // The root kernel object for this instance of Starnix.
    let kernel = Kernel::new(&CString::new("kernel")?)?;

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        let kernel = kernel.clone();
        fasync::Task::local(async move {
            start_runner(kernel, stream).await.expect("failed to start runner.")
        })
        .detach();
    });
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(async move {
            start_manager(stream).await.expect("failed to start manager.")
        })
        .detach();
    });
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}

pub fn block_on<F: std::future::Future>(fut: F) -> F::Output {
    fuchsia_async::Executor::new().unwrap().run_singlethreaded(fut)
}
