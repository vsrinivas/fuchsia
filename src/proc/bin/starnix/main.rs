// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, format_err, Context, Error},
    fidl::endpoints::{self, ClientEnd, ServerEnd},
    fidl_fuchsia_component_runner::{
        self as fcrunner, ComponentControllerMarker, ComponentStartInfo,
    },
    fidl_fuchsia_io as fio, fidl_fuchsia_starnix_developer as fstardev, fidl_fuchsia_sys2 as fsys,
    fuchsia_async as fasync,
    fuchsia_component::client as fclient,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::{
        self as zx, sys::zx_exception_info_t, sys::zx_thread_state_general_regs_t,
        sys::ZX_EXCEPTION_STATE_HANDLED, sys::ZX_EXCEPTION_STATE_TRY_NEXT,
        sys::ZX_EXCP_POLICY_CODE_BAD_SYSCALL,
    },
    futures::{StreamExt, TryStreamExt},
    io_util::directory,
    log::{error, info},
    rand::Rng,
    std::ffi::CString,
    std::mem,
    std::sync::Arc,
};

mod executive;
mod loader;
mod syscall_table;
mod syscalls;
mod uapi;

use executive::*;
use loader::*;
use syscall_table::dispatch_syscall;

// TODO: Should we move this code into fuchsia_zircon? It seems like part of a better abstraction
// for exception channels.
fn as_exception_info(buffer: &zx::MessageBuf) -> zx_exception_info_t {
    let mut tmp = [0; mem::size_of::<zx_exception_info_t>()];
    tmp.clone_from_slice(buffer.bytes());
    unsafe { mem::transmute(tmp) }
}

async fn run_process(process: Arc<ProcessContext>) -> Result<(), Error> {
    let exceptions = &process.exceptions;
    let mut buffer = zx::MessageBuf::new();
    while exceptions.recv_msg(&mut buffer).await.is_ok() {
        let info = as_exception_info(&buffer);
        assert!(buffer.n_handles() == 1);
        let exception = zx::Exception::from(buffer.take_handle(0).unwrap());

        if info.type_ != 0x8208 {
            // ZX_EXCP_POLICY_ERROR
            info!("exception type: 0x{:x}", info.type_);
            exception.set_exception_state(&ZX_EXCEPTION_STATE_TRY_NEXT)?;
            continue;
        }

        let mut ctx = ThreadContext {
            handle: exception.get_thread()?,
            process: Arc::clone(&process),
            registers: zx_thread_state_general_regs_t::default(),
        };

        let report = ctx.handle.get_exception_report()?;

        if report.context.synth_code != ZX_EXCP_POLICY_CODE_BAD_SYSCALL {
            info!("exception synth_code: {}", report.context.synth_code);
            exception.set_exception_state(&ZX_EXCEPTION_STATE_TRY_NEXT)?;
            continue;
        }

        let syscall_number = report.context.synth_data as u64;
        ctx.registers = ctx.handle.read_state_general_regs()?;
        let regs = &ctx.registers;
        let args = (regs.rdi, regs.rsi, regs.rdx, regs.r10, regs.r8, regs.r9);
        // TODO(tbodt): Print the name of the syscall instead of its number (using a proc macro or
        // something)
        info!(target: "strace", "{}({:#x}, {:#x}, {:#x}, {:#x}, {:#x}, {:#x})", syscall_number, args.0, args.1, args.2, args.3, args.4, args.5);
        match dispatch_syscall(&mut ctx, syscall_number, args) {
            Ok(rv) => {
                info!(target: "strace", "-> {:#x}", rv.value());
                ctx.registers.rax = rv.value();
            }
            Err(errno) => {
                info!(target: "strace", "!-> {}", errno);
                ctx.registers.rax = (-errno.value()) as u64;
            }
        }
        ctx.handle.write_state_general_regs(ctx.registers)?;

        exception.set_exception_state(&ZX_EXCEPTION_STATE_HANDLED)?;
    }

    Ok(())
}

async fn start_component(
    start_info: ComponentStartInfo,
    _controller: ServerEnd<ComponentControllerMarker>,
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
    let (ldsvc_client, ldsvc_server) = zx::Channel::create()?;
    library_loader::start(
        directory::clone_no_describe(
            &root_proxy,
            Some(fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE),
        )?,
        ldsvc_server,
    );
    let ldsvc_client = ClientEnd::new(ldsvc_client);

    let parent_job = fuchsia_runtime::job_default();
    let job = parent_job.create_child_job()?;

    let params = ProcessParameters {
        name: CString::new(binary_path.clone())?,
        argv: vec![CString::new(binary_path.clone())?],
        environ: vec![],
    };

    run_process(Arc::new(load_executable(&job, executable_vmo, ldsvc_client, &params).await?))
        .await?;
    Ok(())
}

async fn start_runner(
    mut request_stream: fcrunner::ComponentRunnerRequestStream,
) -> Result<(), Error> {
    while let Some(event) = request_stream.try_next().await? {
        match event {
            fcrunner::ComponentRunnerRequest::Start { start_info, controller, .. } => {
                fasync::Task::local(async move {
                    start_component(start_info, controller)
                        .await
                        .expect("failed to start component")
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

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(
            async move { start_runner(stream).await.expect("failed to start runner.") },
        )
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
