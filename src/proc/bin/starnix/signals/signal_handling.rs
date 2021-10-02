// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::not_implemented;
use crate::signals::*;
use crate::syscalls::SyscallContext;
use crate::task::*;
use crate::types::*;
use std::convert::TryFrom;

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

/// Prepares `ctx` state to execute the signal handler stored in `action`.
///
/// This function stores the state required to restore after the signal handler on the stack.
// TODO(lindkvist): Honor the flags in `sa_flags`.
fn dispatch_signal_handler(
    ctx: &mut SyscallContext<'_>,
    signal_state: &mut SignalState,
    signal: Signal,
    action: sigaction_t,
) {
    let signal_stack_frame = SignalStackFrame::new(
        ucontext {
            uc_mcontext: sigcontext {
                r8: ctx.registers.r8,
                r9: ctx.registers.r9,
                r10: ctx.registers.r10,
                r11: ctx.registers.r11,
                r12: ctx.registers.r12,
                r13: ctx.registers.r13,
                r14: ctx.registers.r14,
                r15: ctx.registers.r15,
                rdi: ctx.registers.rdi,
                rsi: ctx.registers.rsi,
                rbp: ctx.registers.rbp,
                rbx: ctx.registers.rbx,
                rdx: ctx.registers.rdx,
                rax: ctx.registers.rax,
                rcx: ctx.registers.rcx,
                rsp: ctx.registers.rsp,
                rip: ctx.registers.rip,
                eflags: ctx.registers.rflags,
                oldmask: signal_state.mask,
                ..Default::default()
            },
            uc_stack: signal_state
                .alt_stack
                .map(|stack| sigaltstack {
                    ss_sp: stack.ss_sp.ptr() as *mut c_void,
                    ss_flags: stack.ss_flags as i32,
                    ss_size: stack.ss_size,
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
            None => ctx.registers.rsp - RED_ZONE_SIZE,
        }
    } else {
        ctx.registers.rsp - RED_ZONE_SIZE
    };
    stack_pointer -= SIG_STACK_SIZE as u64;
    stack_pointer = misalign_stack_pointer(stack_pointer);

    // Write the signal stack frame at the updated stack pointer.
    ctx.task
        .mm
        .write_memory(UserAddress::from(stack_pointer), &signal_stack_frame.as_bytes())
        .unwrap();

    signal_state.mask = action.sa_mask;

    ctx.registers.rsp = stack_pointer;
    ctx.registers.rdi = signal.number() as u64;
    ctx.registers.rip = action.sa_handler.ptr() as u64;
}

pub fn restore_from_signal_handler(ctx: &mut SyscallContext<'_>) {
    // The stack pointer was intentionally misaligned, so this must be done
    // again to get the correct address for the stack frame.
    let signal_frame_address = misalign_stack_pointer(ctx.registers.rsp);
    let mut signal_stack_bytes = [0; SIG_STACK_SIZE];
    ctx.task
        .mm
        .read_memory(UserAddress::from(signal_frame_address), &mut signal_stack_bytes)
        .unwrap();

    let signal_stack_frame = SignalStackFrame::from_bytes(signal_stack_bytes);
    let uctx = &signal_stack_frame.context.uc_mcontext;
    // Restore the register state from before executing the signal handler.
    ctx.registers = zx::sys::zx_thread_state_general_regs_t {
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
        fs_base: ctx.registers.fs_base,
        gs_base: ctx.registers.gs_base,
    };
    ctx.task.signals.write().mask = signal_stack_frame.context.uc_sigmask;
}

pub fn send_signal(task: &Task, unchecked_signal: &UncheckedSignal) -> Result<(), Errno> {
    // 0 is a sentinel value used to do permission checks.
    let sentinel_signal = UncheckedSignal::from(0);
    if *unchecked_signal == sentinel_signal {
        return Ok(());
    }

    send_checked_signal(task, Signal::try_from(unchecked_signal)?);
    Ok(())
}

pub fn send_checked_signal(task: &Task, signal: Signal) {
    let mut signal_state = task.signals.write();
    let pending_count = signal_state.pending.entry(signal.clone()).or_insert(0);
    if signal.is_real_time() {
        *pending_count += 1;
    } else {
        *pending_count = 1;
    }

    if signal.passes_mask(signal_state.mask)
        && action_for_signal(signal, task.signal_actions.get(signal)) != DeliveryAction::Ignore
    {
        // Wake the task. Note that any potential signal handler will be executed before
        // the task returns from the suspend (from the perspective of user space).
        if let Some(waiter) = &signal_state.waiter {
            waiter.interrupt();
        }
    }
}

fn next_pending_signal(state: &SignalState) -> Option<Signal> {
    state
        .pending
        .iter()
        // Filter out signals that are blocked.
        .filter(|&(signal, num_signals)| signal.passes_mask(state.mask) && *num_signals > 0)
        .flat_map(
            // Filter out signals that are present in the map but have a 0 count.
            |(signal, num_signals)| if *num_signals > 0 { Some(*signal) } else { None },
        )
        .next()
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

fn action_for_signal(signal: Signal, sigaction: sigaction_t) -> DeliveryAction {
    match sigaction.sa_handler {
        SIG_DFL => match signal.number() {
            SIGCHLD | SIGURG | SIGWINCH | SIGRTMIN..=Signal::NUM_SIGNALS => DeliveryAction::Ignore,
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

/// Dequeues and handles a pending signal for `ctx.task`.
pub fn dequeue_signal(ctx: &mut SyscallContext<'_>) {
    let task = ctx.task;
    let mut signal_state = task.signals.write();

    if let Some(signal) = next_pending_signal(&signal_state) {
        let sigaction = task.signal_actions.get(signal);
        let action = action_for_signal(signal, sigaction);
        match action {
            DeliveryAction::CallHandler => {
                dispatch_signal_handler(ctx, &mut signal_state, signal, sigaction);
            }

            DeliveryAction::Ignore => {}

            _ => not_implemented!("Unimplemented signal delivery action {:?}", action),
        };
        // This unwrap is safe since we checked the signal comes from the signals collection.
        *signal_state.pending.get_mut(&signal).unwrap() -= 1;
    }
}

pub fn are_signals_pending(state: &SignalState) -> bool {
    next_pending_signal(state).is_some()
}
