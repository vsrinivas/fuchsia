// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::not_implemented;
use crate::signals::*;
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

/// A `SignalStackFrame` contains all the state that is stored on the stack prior
/// to executing a signal handler.
///
/// Note that the ordering of the fields is significant. In particular, restorer_address
/// must be the first field, since that is where the signal handler will return after
/// it finishes executing.
#[repr(C)]
#[derive(Default)]
#[cfg(target_arch = "x86_64")]
struct SignalStackFrame {
    /// The address of the signal handler function.
    restorer_address: u64,

    /// The state of the thread at the time the signal was handled.
    context: ucontext,
}

const SIG_STACK_SIZE: usize = std::mem::size_of::<SignalStackFrame>();

impl SignalStackFrame {
    fn new(context: ucontext, restorer_address: u64) -> SignalStackFrame {
        SignalStackFrame { context, restorer_address }
    }

    fn as_bytes(self) -> [u8; SIG_STACK_SIZE] {
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
                oldmask: signal_state.mask,
                ..Default::default()
            },
            uc_stack: signal_state
                .alt_stack
                .map(|stack| sigaltstack {
                    ss_sp: stack.ss_sp.ptr() as *mut c_void,
                    ss_flags: stack.ss_flags as i32,
                    ss_size: stack.ss_size,
                    ..Default::default()
                })
                .unwrap_or(sigaltstack::default()),
            uc_sigmask: signal_state.mask,
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
        .write_memory(UserAddress::from(stack_pointer), &signal_stack_frame.as_bytes())
        .unwrap();

    signal_state.mask = action.sa_mask;

    current_task.registers.rsp = stack_pointer;
    current_task.registers.rdi = siginfo.signal.number() as u64;
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
    };
    current_task.signals.write().mask = signal_stack_frame.context.uc_sigmask;
    Ok(())
}

pub fn send_signal(task: &Task, siginfo: SignalInfo) {
    let mut signal_state = task.signals.write();
    signal_state.enqueue(siginfo.clone());

    if !siginfo.signal.is_in_set(signal_state.mask)
        && action_for_signal(&siginfo, task.signal_actions.get(siginfo.signal))
            != DeliveryAction::Ignore
    {
        // Wake the task. Note that any potential signal handler will be executed before
        // the task returns from the suspend (from the perspective of user space).
        if let Some(waiter) = &signal_state.waiter {
            waiter.interrupt();
        }
    }
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

/// Dequeues and handles a pending signal for `current_task`.
pub fn dequeue_signal(current_task: &mut CurrentTask) {
    let task = current_task.task_arc_clone();
    let mut signal_state = task.signals.write();

    let mask = signal_state.mask;
    if let Some(siginfo) = signal_state.take_next_allowed_by_mask(mask) {
        let sigaction = task.signal_actions.get(siginfo.signal);
        match action_for_signal(&siginfo, sigaction) {
            DeliveryAction::CallHandler => {
                dispatch_signal_handler(current_task, &mut signal_state, siginfo, sigaction);
            }

            DeliveryAction::Ignore => {}

            action => not_implemented!("Unimplemented signal delivery action {:?}", action),
        };
    }
}
