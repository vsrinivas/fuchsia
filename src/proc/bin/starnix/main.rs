// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_zircon::{
        self as zx, sys::zx_exception_info_t, sys::zx_thread_state_general_regs_t,
        sys::ZX_EXCEPTION_STATE_HANDLED, sys::ZX_EXCEPTION_STATE_TRY_NEXT,
        sys::ZX_EXCP_POLICY_CODE_BAD_SYSCALL,
    },
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
            println!("starnix: exception type: 0x{:x}", info.type_);
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
            println!("starnix: exception synth_code: {}", report.context.synth_code);
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

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    const HELLO_WORLD_BIN: &'static str = "/pkg/bin/hello_world.bin";
    let file =
        fdio::open_fd(HELLO_WORLD_BIN, fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE)?;
    let executable = fdio::get_vmo_exec_from_file(&file)?;
    let parent_job = fuchsia_runtime::job_default();
    let job = parent_job.create_child_job()?;

    let params = ProcessParameters {
        name: CString::new(HELLO_WORLD_BIN.to_owned())?,
        argv: vec![CString::new(HELLO_WORLD_BIN).unwrap()],
        environ: vec![],
        aux: vec![AT_UID, 3, AT_EUID, 3, AT_GID, 3, AT_EGID, 3, AT_SECURE, 0],
    };

    run_process(Arc::new(load_executable(&job, executable, &params).await?)).await?;

    Ok(())
}
