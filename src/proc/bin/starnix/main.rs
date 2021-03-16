// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_component_runner::{
        self as fcrunner, ComponentControllerMarker, ComponentStartInfo,
    },
    fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::{
        self as zx, sys::zx_exception_info_t, sys::zx_thread_state_general_regs_t,
        sys::ZX_EXCEPTION_STATE_HANDLED, sys::ZX_EXCEPTION_STATE_TRY_NEXT,
        sys::ZX_EXCP_POLICY_CODE_BAD_SYSCALL,
    },
    futures::{StreamExt, TryStreamExt},
    io_util::directory,
    log::info,
    std::ffi::CString,
    std::mem,
    std::sync::Arc,
};

mod executive;
mod loader;
mod syscalls;
mod types;

use executive::*;
use loader::*;
use syscalls::*;
use types::*;

pub fn decode_syscall(
    ctx: &mut ThreadContext,
    syscall_number: syscall_number_t,
) -> Result<SyscallResult, Errno> {
    let regs = ctx.registers;
    match syscall_number {
        SYS_WRITE => {
            sys_write(ctx, regs.rdi as i32, UserAddress::from(regs.rsi), regs.rdx as usize)
        }
        SYS_FSTAT => sys_fstat(ctx, regs.rdi as i32, UserAddress::from(regs.rsi)),
        SYS_MPROTECT => {
            sys_mprotect(ctx, UserAddress::from(regs.rdi), regs.rsi as usize, regs.rdx as i32)
        }
        SYS_BRK => sys_brk(ctx, UserAddress::from(regs.rdi)),
        SYS_WRITEV => {
            sys_writev(ctx, regs.rdi as i32, UserAddress::from(regs.rsi), regs.rdx as i32)
        }
        SYS_ACCESS => sys_access(ctx, UserAddress::from(regs.rdi), regs.rsi as i32),
        SYS_EXIT => sys_exit(ctx, regs.rdi as i32),
        SYS_UNAME => sys_uname(ctx, UserAddress::from(regs.rdi)),
        SYS_READLINK => sys_readlink(
            ctx,
            UserAddress::from(regs.rdi),
            UserAddress::from(regs.rsi),
            regs.rdx as usize,
        ),
        SYS_GETUID => sys_getuid(ctx),
        SYS_GETGID => sys_getgid(ctx),
        SYS_GETEUID => sys_geteuid(ctx),
        SYS_GETEGID => sys_getegid(ctx),
        SYS_ARCH_PRCTL => sys_arch_prctl(ctx, regs.rdi as i32, UserAddress::from(regs.rsi)),
        SYS_EXIT_GROUP => sys_exit_group(ctx, regs.rdi as i32),
        _ => sys_unknown(ctx, syscall_number),
    }
}

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

        let syscall_number = report.context.synth_data as syscall_number_t;
        ctx.registers = ctx.handle.read_state_general_regs()?;
        match decode_syscall(&mut ctx, syscall_number) {
            Ok(rv) => {
                ctx.registers.rax = rv.value();
            }
            Err(errno) => {
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
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}
