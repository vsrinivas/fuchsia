// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::logging::strace;
use crate::signals::*;
use crate::syscalls::SyscallResult;
use crate::task::*;
use crate::types::*;

use fuchsia_zircon as zx;

/// The size of the red zone.
///
/// From the AMD64 ABI:
///   > The 128-byte area beyond the location pointed to
///   > by %rsp is considered to be reserved and shall not be modified by signal or
///   > interrupt handlers. Therefore, functions may use this area for temporary
///   > data that is not needed across function calls. In particular, leaf functions
///   > may use this area for their entire stack frame, rather than adjusting the
///   > stack pointer in the prologue and epilogue. This area is known as the red
///   > zone.
#[cfg(target_arch = "x86_64")]
const RED_ZONE_SIZE: u64 = 128;

/// The size of the syscall instruction in bytes.
#[cfg(target_arch = "x86_64")]
const SYSCALL_INSTRUCTION_SIZE_BYTES: u64 = 2;

/// A `SignalStackFrame` contains all the state that is stored on the stack prior
/// to executing a signal handler.
///
/// The ordering of the fields is significant, as it is part of the syscall ABI. In particular,
/// restorer_address must be the first field, since that is where the signal handler will return
/// after it finishes executing.
#[repr(C)]
#[cfg(target_arch = "x86_64")]
struct SignalStackFrame {
    /// The address of the signal handler function.
    ///
    /// Must be the first field, to be positioned to serve as the return address.
    restorer_address: u64,

    /// Information about the signal.
    siginfo_bytes: [u8; std::mem::size_of::<siginfo_t>()],

    /// The state of the thread at the time the signal was handled.
    context: ucontext,
    /// The FPU state. Must be immediately after the ucontext field, since Bionic considers this to
    /// be part of the ucontext for some reason.
    fpstate: _fpstate_64,
}

const SIG_STACK_SIZE: usize = std::mem::size_of::<SignalStackFrame>();

impl SignalStackFrame {
    fn new(siginfo: &SignalInfo, context: ucontext, restorer_address: u64) -> SignalStackFrame {
        SignalStackFrame {
            context,
            siginfo_bytes: siginfo.as_siginfo_bytes(),
            restorer_address,
            fpstate: Default::default(),
        }
    }

    fn as_bytes(&self) -> &[u8; SIG_STACK_SIZE] {
        unsafe { std::mem::transmute(self) }
    }

    fn from_bytes(bytes: [u8; SIG_STACK_SIZE]) -> SignalStackFrame {
        unsafe { std::mem::transmute(bytes) }
    }
}

/// Aligns the stack pointer to be 16 byte aligned, and then misaligns it by 8 bytes.
///
/// This is done because x86-64 functions expect the stack to be misaligned by 8 bytes,
/// as if the stack was 16 byte aligned and then someone used a call instruction. This
/// is due to alignment-requiring SSE instructions.
fn misalign_stack_pointer(pointer: u64) -> u64 {
    pointer - (pointer % 16 + 8)
}

/// Prepares `current` state to execute the signal handler stored in `action`.
///
/// This function stores the state required to restore after the signal handler on the stack.
// TODO(lindkvist): Honor the flags in `sa_flags`.
fn dispatch_signal_handler(
    current_task: &mut CurrentTask,
    signal_state: &mut SignalState,
    siginfo: SignalInfo,
    action: sigaction_t,
) {
    let signal_stack_frame = SignalStackFrame::new(
        &siginfo,
        ucontext {
            uc_mcontext: sigcontext {
                r8: current_task.registers.r8,
                r9: current_task.registers.r9,
                r10: current_task.registers.r10,
                r11: current_task.registers.r11,
                r12: current_task.registers.r12,
                r13: current_task.registers.r13,
                r14: current_task.registers.r14,
                r15: current_task.registers.r15,
                rdi: current_task.registers.rdi,
                rsi: current_task.registers.rsi,
                rbp: current_task.registers.rbp,
                rbx: current_task.registers.rbx,
                rdx: current_task.registers.rdx,
                rax: current_task.registers.rax,
                rcx: current_task.registers.rcx,
                rsp: current_task.registers.rsp,
                rip: current_task.registers.rip,
                eflags: current_task.registers.rflags,
                oldmask: signal_state.mask(),
                ..Default::default()
            },
            uc_stack: signal_state
                .alt_stack
                .map(|stack| sigaltstack {
                    ss_sp: stack.ss_sp.ptr() as *mut c_void,
                    ss_flags: stack.ss_flags as i32,
                    ss_size: stack.ss_size as u64,
                    ..Default::default()
                })
                .unwrap_or_default(),
            uc_sigmask: signal_state.mask(),
            ..Default::default()
        },
        action.sa_restorer.ptr() as u64,
    );

    // Determine which stack pointer to use for the signal handler.
    // If the signal handler is executed on the main stack, adjust the stack pointer to account for
    // the red zone.
    // https://en.wikipedia.org/wiki/Red_zone_%28computing%29
    let mut stack_pointer = if (action.sa_flags & SA_ONSTACK as u64) != 0 {
        match signal_state.alt_stack {
            Some(sigaltstack) => {
                // Since the stack grows down, the size is added to the ss_sp when calculating the
                // "bottom" of the stack.
                (sigaltstack.ss_sp.ptr() + sigaltstack.ss_size) as u64
            }
            None => current_task.registers.rsp - RED_ZONE_SIZE,
        }
    } else {
        current_task.registers.rsp - RED_ZONE_SIZE
    };
    stack_pointer -= SIG_STACK_SIZE as u64;
    stack_pointer = misalign_stack_pointer(stack_pointer);

    // Write the signal stack frame at the updated stack pointer.
    current_task
        .mm
        .write_memory(UserAddress::from(stack_pointer), signal_stack_frame.as_bytes())
        .unwrap();

    signal_state.set_mask(action.sa_mask);

    current_task.registers.rsp = stack_pointer;
    current_task.registers.rdi = siginfo.signal.number() as u64;
    if (action.sa_flags & SA_SIGINFO as u64) != 0 {
        current_task.registers.rsi =
            stack_pointer + memoffset::offset_of!(SignalStackFrame, siginfo_bytes) as u64;
        current_task.registers.rdx =
            stack_pointer + memoffset::offset_of!(SignalStackFrame, context) as u64;
    }
    current_task.registers.rip = action.sa_handler.ptr() as u64;
}

pub fn restore_from_signal_handler(current_task: &mut CurrentTask) -> Result<(), Errno> {
    // The stack pointer was intentionally misaligned, so this must be done
    // again to get the correct address for the stack frame.
    let signal_frame_address = misalign_stack_pointer(current_task.registers.rsp);
    let mut signal_stack_bytes = [0; SIG_STACK_SIZE];
    current_task
        .mm
        .read_memory(UserAddress::from(signal_frame_address), &mut signal_stack_bytes)?;

    let signal_stack_frame = SignalStackFrame::from_bytes(signal_stack_bytes);
    let uctx = &signal_stack_frame.context.uc_mcontext;
    // Restore the register state from before executing the signal handler.
    current_task.registers = zx::sys::zx_thread_state_general_regs_t {
        r8: uctx.r8,
        r9: uctx.r9,
        r10: uctx.r10,
        r11: uctx.r11,
        r12: uctx.r12,
        r13: uctx.r13,
        r14: uctx.r14,
        r15: uctx.r15,
        rax: uctx.rax,
        rbx: uctx.rbx,
        rcx: uctx.rcx,
        rdx: uctx.rdx,
        rsi: uctx.rsi,
        rdi: uctx.rdi,
        rbp: uctx.rbp,
        rsp: uctx.rsp,
        rip: uctx.rip,
        rflags: uctx.eflags,
        fs_base: current_task.registers.fs_base,
        gs_base: current_task.registers.gs_base,
    }
    .into();
    current_task.write().signals.set_mask(signal_stack_frame.context.uc_sigmask);
    Ok(())
}

pub fn send_signal(task: &Task, siginfo: SignalInfo) {
    let mut task_state = task.write();

    let action_is_masked = siginfo.signal.is_in_set(task_state.signals.mask());
    let sigaction = task.thread_group.signal_actions.get(siginfo.signal);
    let action = action_for_signal(&siginfo, sigaction);

    // Enqueue a masked signal, since it can be unmasked later, but don't enqueue an ignored
    // signal, since it cannot. See SigtimedwaitTest.IgnoredUnmaskedSignal gvisor test.
    if action != DeliveryAction::Ignore || action_is_masked {
        task_state.signals.enqueue(siginfo.clone());
    }

    drop(task_state);

    if !action_is_masked && action.must_interrupt(sigaction) {
        // Wake the task. Note that any potential signal handler will be executed before
        // the task returns from the suspend (from the perspective of user space).
        task.interrupt();
    }

    // Unstop the process for SIGCONT. Also unstop for SIGKILL, the only signal that can interrupt
    // a stopped process.
    if siginfo.signal == SIGCONT || siginfo.signal == SIGKILL {
        task.thread_group.set_stopped(false, siginfo);
    }
}

pub fn force_signal(current_task: &CurrentTask, mut siginfo: SignalInfo) {
    strace!(current_task, "forced signal {:?}", siginfo);
    siginfo.force = true;
    send_signal(current_task, siginfo)
}

/// Represents the action to take when signal is delivered.
///
/// See https://man7.org/linux/man-pages/man7/signal.7.html.
#[derive(Debug, PartialEq)]
enum DeliveryAction {
    Ignore,
    CallHandler,
    Terminate,
    CoreDump,
    Stop,
    Continue,
}

impl DeliveryAction {
    /// Returns whether the target task must be interrupted to execute the action.
    ///
    /// The task will not be interrupted if the signal is the action is the Continue action, or if
    /// the action is Ignore and the user specifically requested to ignore the signal.
    pub fn must_interrupt(&self, sigaction: sigaction_t) -> bool {
        match *self {
            Self::Continue => false,
            Self::Ignore => sigaction.sa_handler == SIG_IGN,
            _ => true,
        }
    }
}

fn action_for_signal(siginfo: &SignalInfo, sigaction: sigaction_t) -> DeliveryAction {
    match sigaction.sa_handler {
        SIG_DFL => match siginfo.signal {
            SIGCHLD | SIGURG | SIGWINCH => DeliveryAction::Ignore,
            sig if sig.is_real_time() => DeliveryAction::Ignore,
            SIGHUP | SIGINT | SIGKILL | SIGPIPE | SIGALRM | SIGTERM | SIGUSR1 | SIGUSR2
            | SIGPROF | SIGVTALRM | SIGSTKFLT | SIGIO | SIGPWR => DeliveryAction::Terminate,
            SIGQUIT | SIGILL | SIGABRT | SIGFPE | SIGSEGV | SIGBUS | SIGSYS | SIGTRAP | SIGXCPU
            | SIGXFSZ => DeliveryAction::CoreDump,
            SIGSTOP | SIGTSTP | SIGTTIN | SIGTTOU => DeliveryAction::Stop,
            SIGCONT => DeliveryAction::Continue,
            _ => panic!("Unknown signal"),
        },
        SIG_IGN => DeliveryAction::Ignore,
        _ => DeliveryAction::CallHandler,
    }
}

/// Maybe adjust a task's registers to restart a syscall once the task switches back to userspace,
/// based on whether the return value is one of the restartable error codes such as ERESTARTSYS.
fn prepare_to_restart_syscall(current_task: &mut CurrentTask, sigaction: Option<sigaction_t>) {
    let err = ErrnoCode::from_return_value(current_task.registers.rax);
    // If sigaction is None, the syscall must be restarted if it is restartable. The default
    // sigaction will not have a sighandler, which will guarantee a restart.
    let sigaction = sigaction.unwrap_or_default();

    let should_restart = match err {
        ERESTARTSYS => sigaction.sa_flags & SA_RESTART as u64 != 0,
        ERESTARTNOINTR => true,
        ERESTARTNOHAND | ERESTART_RESTARTBLOCK => false,

        // The syscall did not request to be restarted.
        _ => return,
    };
    // Always restart if the signal did not call a handler (i.e. SIGSTOP).
    let should_restart = should_restart || sigaction.sa_handler.is_null();

    if !should_restart {
        current_task.registers.rax = EINTR.return_value();
        return;
    }

    // The syscall should be restarted. Reload the original `rax` (syscall number) into `rax`,
    // and backup the instruction pointer to the `syscall` instruction.
    current_task.registers.rax = match err {
        // Custom restart, invoke restart_syscall instead of the original syscall.
        ERESTART_RESTARTBLOCK => __NR_restart_syscall as u64,
        _ => current_task.registers.orig_rax,
    };
    current_task.registers.rip -= SYSCALL_INSTRUCTION_SIZE_BYTES;
}

/// Dequeues and handles a pending signal for `current_task`.
pub fn dequeue_signal(current_task: &mut CurrentTask) {
    let task = current_task.task_arc_clone();
    let mut task_state = task.write();

    let mask = task_state.signals.mask();
    let siginfo =
        task_state.signals.take_next_where(|sig| !sig.signal.is_in_set(mask) || sig.force);
    prepare_to_restart_syscall(
        current_task,
        siginfo.as_ref().map(|siginfo| task.thread_group.signal_actions.get(siginfo.signal)),
    );
    if let Some(siginfo) = siginfo {
        let sigaction = task.thread_group.signal_actions.get(siginfo.signal);
        match action_for_signal(&siginfo, sigaction) {
            DeliveryAction::Ignore => {}
            DeliveryAction::CallHandler => {
                dispatch_signal_handler(current_task, &mut task_state.signals, siginfo, sigaction);
            }
            DeliveryAction::Terminate => {
                // Release the signals lock. [`ThreadGroup::exit`] sends signals to threads which
                // will include this one and cause a deadlock re-acquiring the signals lock.
                drop(task_state);
                current_task.thread_group.exit(ExitStatus::Kill(siginfo));
            }
            DeliveryAction::CoreDump => {
                task_state.dump_on_exit = true;
                drop(task_state);
                current_task.thread_group.exit(ExitStatus::CoreDump(siginfo));
            }
            DeliveryAction::Stop => {
                drop(task_state);
                current_task.thread_group.set_stopped(true, siginfo);
            }
            DeliveryAction::Continue => {
                // Nothing to do. Effect already happened when the signal was raised.
            }
        };
    }
}

pub fn sys_restart_syscall(current_task: &mut CurrentTask) -> Result<SyscallResult, Errno> {
    match current_task.syscall_restart_func.take() {
        Some(f) => f(current_task),
        None => {
            // This may indicate a bug where a syscall returns ERESTART_RESTARTBLOCK without
            // setting a restart func. But it can also be triggered by userspace, e.g. by directly
            // calling restart_syscall or injecting an ERESTART_RESTARTBLOCK error through ptrace.
            tracing::warn!("restart_syscall called, but nothing to restart");
            error!(EINTR)
        }
    }
}

#[cfg(test)]
mod tests {
    use std::sync::Arc;

    use super::*;
    use crate::mm::{DesiredAddress, MappingOptions};
    use crate::testing::*;

    const SYSCALL_INSTRUCTION_ADDRESS: UserAddress = UserAddress::from(100);
    const SYSCALL_NUMBER: u64 = 42;
    const SYSCALL_ARGS: (u64, u64, u64, u64, u64, u64) = (20, 21, 22, 23, 24, 25);
    const SA_RESTORER_ADDRESS: UserAddress = UserAddress::from(0xDEADBEEF);
    const SA_HANDLER_ADDRESS: UserAddress = UserAddress::from(0x00BADDAD);

    const SYSCALL2_INSTRUCTION_ADDRESS: UserAddress = UserAddress::from(200);
    const SYSCALL2_NUMBER: u64 = 84;
    const SYSCALL2_ARGS: (u64, u64, u64, u64, u64, u64) = (30, 31, 32, 33, 34, 35);
    const SA_HANDLER2_ADDRESS: UserAddress = UserAddress::from(0xBADDAD00);

    #[cfg(target_arch = "x86_64")]
    #[::fuchsia::test]
    fn syscall_restart_adjusts_instruction_pointer_and_rax() {
        let (_kernel, mut current_task) = create_kernel_and_task_with_stack();

        // Register the signal action.
        current_task.thread_group.signal_actions.set(
            SIGUSR1,
            sigaction_t {
                sa_flags: (SA_RESTORER | SA_RESTART | SA_SIGINFO) as u64,
                sa_handler: SA_HANDLER_ADDRESS,
                sa_restorer: SA_RESTORER_ADDRESS,
                ..sigaction_t::default()
            },
        );

        // Simulate a syscall that should be restarted by setting up the register state to what it
        // was after the interrupted syscall. `rax` should have the return value (-ERESTARTSYS);
        // `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`, should be the syscall arguments;
        // `orig_rax` should hold the syscall number;
        // and the instruction pointer should be 2 bytes after the syscall instruction.
        current_task.registers.rax = ERESTARTSYS.return_value();
        current_task.registers.rdi = SYSCALL_ARGS.0;
        current_task.registers.rsi = SYSCALL_ARGS.1;
        current_task.registers.rdx = SYSCALL_ARGS.2;
        current_task.registers.r10 = SYSCALL_ARGS.3;
        current_task.registers.r8 = SYSCALL_ARGS.4;
        current_task.registers.r9 = SYSCALL_ARGS.5;
        current_task.registers.orig_rax = SYSCALL_NUMBER;
        current_task.registers.rip = (SYSCALL_INSTRUCTION_ADDRESS + 2u64).ptr() as u64;

        // Queue the signal that interrupted the syscall.
        current_task.write().signals.enqueue(SignalInfo::new(SIGUSR1, SI_USER, SignalDetail::None));

        // Process the signal.
        dequeue_signal(&mut current_task);

        // The instruction pointer should have changed to the signal handling address.
        assert_eq!(current_task.registers.rip, SA_HANDLER_ADDRESS.ptr() as u64);

        // The syscall arguments should be overwritten with signal handling args.
        assert_ne!(current_task.registers.rdi, SYSCALL_ARGS.0);
        assert_ne!(current_task.registers.rsi, SYSCALL_ARGS.1);
        assert_ne!(current_task.registers.rdx, SYSCALL_ARGS.2);

        // Now we assume that execution of the signal handler completed with a call to
        // `sys_rt_sigreturn`, which would set `rax` to that syscall number.
        current_task.registers.rax = __NR_rt_sigreturn as u64;
        current_task.registers.rsp += 8; // The stack was popped returning from the signal handler.

        restore_from_signal_handler(&mut current_task).expect("failed to restore state");

        // The state of the task is now such that when switching back to userspace, the instruction
        // pointer will point at the original syscall instruction, with the arguments correctly
        // restored into the registers.
        assert_eq!(current_task.registers.rax, SYSCALL_NUMBER);
        assert_eq!(current_task.registers.rdi, SYSCALL_ARGS.0);
        assert_eq!(current_task.registers.rsi, SYSCALL_ARGS.1);
        assert_eq!(current_task.registers.rdx, SYSCALL_ARGS.2);
        assert_eq!(current_task.registers.r10, SYSCALL_ARGS.3);
        assert_eq!(current_task.registers.r8, SYSCALL_ARGS.4);
        assert_eq!(current_task.registers.r9, SYSCALL_ARGS.5);
        assert_eq!(current_task.registers.rip, SYSCALL_INSTRUCTION_ADDRESS.ptr() as u64);
    }

    #[cfg(target_arch = "x86_64")]
    #[::fuchsia::test]
    fn syscall_nested_restart() {
        let (_kernel, mut current_task) = create_kernel_and_task_with_stack();

        // Register the signal actions.
        current_task.thread_group.signal_actions.set(
            SIGUSR1,
            sigaction_t {
                sa_flags: (SA_RESTORER | SA_RESTART | SA_SIGINFO) as u64,
                sa_handler: SA_HANDLER_ADDRESS,
                sa_restorer: SA_RESTORER_ADDRESS,
                ..sigaction_t::default()
            },
        );
        current_task.thread_group.signal_actions.set(
            SIGUSR2,
            sigaction_t {
                sa_flags: (SA_RESTORER | SA_RESTART | SA_SIGINFO) as u64,
                sa_handler: SA_HANDLER2_ADDRESS,
                sa_restorer: SA_RESTORER_ADDRESS,
                ..sigaction_t::default()
            },
        );

        // Simulate a syscall that should be restarted by setting up the register state to what it
        // was after the interrupted syscall. `rax` should have the return value (-ERESTARTSYS);
        // `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`, should be the syscall arguments;
        // `orig_rax` should hold the syscall number;
        // and the instruction pointer should be 2 bytes after the syscall instruction.
        current_task.registers.rax = ERESTARTSYS.return_value();
        current_task.registers.rdi = SYSCALL_ARGS.0;
        current_task.registers.rsi = SYSCALL_ARGS.1;
        current_task.registers.rdx = SYSCALL_ARGS.2;
        current_task.registers.r10 = SYSCALL_ARGS.3;
        current_task.registers.r8 = SYSCALL_ARGS.4;
        current_task.registers.r9 = SYSCALL_ARGS.5;
        current_task.registers.orig_rax = SYSCALL_NUMBER;
        current_task.registers.rip = (SYSCALL_INSTRUCTION_ADDRESS + 2u64).ptr() as u64;

        // Queue the signal that interrupted the syscall.
        current_task.write().signals.enqueue(SignalInfo::new(SIGUSR1, SI_USER, SignalDetail::None));

        // Process the signal.
        dequeue_signal(&mut current_task);

        // The instruction pointer should have changed to the signal handling address.
        assert_eq!(current_task.registers.rip, SA_HANDLER_ADDRESS.ptr() as u64);

        // The syscall arguments should be overwritten with signal handling args.
        assert_ne!(current_task.registers.rdi, SYSCALL_ARGS.0);
        assert_ne!(current_task.registers.rsi, SYSCALL_ARGS.1);
        assert_ne!(current_task.registers.rdx, SYSCALL_ARGS.2);

        // Simulate another syscall being interrupted.
        current_task.registers.rax = ERESTARTSYS.return_value();
        current_task.registers.rdi = SYSCALL2_ARGS.0;
        current_task.registers.rsi = SYSCALL2_ARGS.1;
        current_task.registers.rdx = SYSCALL2_ARGS.2;
        current_task.registers.r10 = SYSCALL2_ARGS.3;
        current_task.registers.r8 = SYSCALL2_ARGS.4;
        current_task.registers.r9 = SYSCALL2_ARGS.5;
        current_task.registers.orig_rax = SYSCALL2_NUMBER;
        current_task.registers.rip = (SYSCALL2_INSTRUCTION_ADDRESS + 2u64).ptr() as u64;

        // Queue the signal that interrupted the syscall.
        current_task.write().signals.enqueue(SignalInfo::new(SIGUSR2, SI_USER, SignalDetail::None));

        // Process the signal.
        dequeue_signal(&mut current_task);

        // The instruction pointer should have changed to the signal handling address.
        assert_eq!(current_task.registers.rip, SA_HANDLER2_ADDRESS.ptr() as u64);

        // The syscall arguments should be overwritten with signal handling args.
        assert_ne!(current_task.registers.rdi, SYSCALL2_ARGS.0);
        assert_ne!(current_task.registers.rsi, SYSCALL2_ARGS.1);
        assert_ne!(current_task.registers.rdx, SYSCALL2_ARGS.2);

        // Now we assume that execution of the second signal handler completed with a call to
        // `sys_rt_sigreturn`, which would set `rax` to that syscall number.
        current_task.registers.rax = __NR_rt_sigreturn as u64;
        current_task.registers.rsp += 8; // The stack was popped returning from the signal handler.

        restore_from_signal_handler(&mut current_task).expect("failed to restore state");

        // The state of the task is now such that when switching back to userspace, the instruction
        // pointer will point at the original syscall instruction, with the arguments correctly
        // restored into the registers.
        assert_eq!(current_task.registers.rax, SYSCALL2_NUMBER);
        assert_eq!(current_task.registers.rdi, SYSCALL2_ARGS.0);
        assert_eq!(current_task.registers.rsi, SYSCALL2_ARGS.1);
        assert_eq!(current_task.registers.rdx, SYSCALL2_ARGS.2);
        assert_eq!(current_task.registers.r10, SYSCALL2_ARGS.3);
        assert_eq!(current_task.registers.r8, SYSCALL2_ARGS.4);
        assert_eq!(current_task.registers.r9, SYSCALL2_ARGS.5);
        assert_eq!(current_task.registers.rip, SYSCALL2_INSTRUCTION_ADDRESS.ptr() as u64);

        // Now we assume that execution of the first signal handler completed with a call to
        // `sys_rt_sigreturn`, which would set `rax` to that syscall number.
        current_task.registers.rax = __NR_rt_sigreturn as u64;
        current_task.registers.rsp += 8; // The stack was popped returning from the signal handler.

        restore_from_signal_handler(&mut current_task).expect("failed to restore state");

        // The state of the task is now such that when switching back to userspace, the instruction
        // pointer will point at the original syscall instruction, with the arguments correctly
        // restored into the registers.
        assert_eq!(current_task.registers.rax, SYSCALL_NUMBER);
        assert_eq!(current_task.registers.rdi, SYSCALL_ARGS.0);
        assert_eq!(current_task.registers.rsi, SYSCALL_ARGS.1);
        assert_eq!(current_task.registers.rdx, SYSCALL_ARGS.2);
        assert_eq!(current_task.registers.r10, SYSCALL_ARGS.3);
        assert_eq!(current_task.registers.r8, SYSCALL_ARGS.4);
        assert_eq!(current_task.registers.r9, SYSCALL_ARGS.5);
        assert_eq!(current_task.registers.rip, SYSCALL_INSTRUCTION_ADDRESS.ptr() as u64);
    }

    #[cfg(target_arch = "x86_64")]
    #[::fuchsia::test]
    fn syscall_does_not_restart_if_signal_action_has_no_sa_restart_flag() {
        let (_kernel, mut current_task) = create_kernel_and_task_with_stack();

        // Register the signal action.
        current_task.thread_group.signal_actions.set(
            SIGUSR1,
            sigaction_t {
                sa_flags: (SA_RESTORER | SA_SIGINFO) as u64,
                sa_handler: SA_HANDLER_ADDRESS,
                sa_restorer: SA_RESTORER_ADDRESS,
                ..sigaction_t::default()
            },
        );

        // Simulate a syscall that should be restarted by setting up the register state to what it
        // was after the interrupted syscall. `rax` should have the return value (-ERESTARTSYS);
        // `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`, should be the syscall arguments;
        // `orig_rax` should hold the syscall number;
        // and the instruction pointer should be 2 bytes after the syscall instruction.
        current_task.registers.rax = ERESTARTSYS.return_value();
        current_task.registers.rdi = SYSCALL_ARGS.0;
        current_task.registers.rsi = SYSCALL_ARGS.1;
        current_task.registers.rdx = SYSCALL_ARGS.2;
        current_task.registers.r10 = SYSCALL_ARGS.3;
        current_task.registers.r8 = SYSCALL_ARGS.4;
        current_task.registers.r9 = SYSCALL_ARGS.5;
        current_task.registers.orig_rax = SYSCALL_NUMBER;
        current_task.registers.rip = (SYSCALL_INSTRUCTION_ADDRESS + 2u64).ptr() as u64;

        // Queue the signal that interrupted the syscall.
        current_task.write().signals.enqueue(SignalInfo::new(SIGUSR1, SI_USER, SignalDetail::None));

        // Process the signal.
        dequeue_signal(&mut current_task);

        // The instruction pointer should have changed to the signal handling address.
        assert_eq!(current_task.registers.rip, SA_HANDLER_ADDRESS.ptr() as u64);

        // The syscall arguments should be overwritten with signal handling args.
        assert_ne!(current_task.registers.rdi, SYSCALL_ARGS.0);
        assert_ne!(current_task.registers.rsi, SYSCALL_ARGS.1);
        assert_ne!(current_task.registers.rdx, SYSCALL_ARGS.2);

        // Now we assume that execution of the signal handler completed with a call to
        // `sys_rt_sigreturn`, which would set `rax` to that syscall number.
        current_task.registers.rax = __NR_rt_sigreturn as u64;
        current_task.registers.rsp += 8; // The stack was popped returning from the signal handler.

        restore_from_signal_handler(&mut current_task).expect("failed to restore state");

        // The state of the task is now such that when switching back to userspace, the instruction
        // pointer will point at the original syscall instruction, with the arguments correctly
        // restored into the registers.
        assert_eq!(current_task.registers.rax, EINTR.return_value());
        assert_eq!(current_task.registers.rip, (SYSCALL_INSTRUCTION_ADDRESS + 2u64).ptr() as u64);
    }

    /// Creates a kernel and initial task, giving the task a stack.
    fn create_kernel_and_task_with_stack() -> (Arc<Kernel>, CurrentTask) {
        let (kernel, mut current_task) = create_kernel_and_task();

        const STACK_SIZE: usize = 0x1000;

        // Give the task a stack.
        let stack_base = current_task
            .mm
            .map(
                DesiredAddress::Hint(UserAddress::default()),
                Arc::new(zx::Vmo::create(STACK_SIZE as u64).expect("failed to create stack VMO")),
                0,
                STACK_SIZE,
                zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE,
                MappingOptions::empty(),
                None,
            )
            .expect("failed to map stack VMO");
        current_task.registers.rsp = (stack_base + (STACK_SIZE - 8)).ptr() as u64;

        (kernel, current_task)
    }
}
