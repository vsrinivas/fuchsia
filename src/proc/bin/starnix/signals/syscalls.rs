// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryFrom;
use std::sync::Arc;

use super::signalfd::*;
use crate::fs::*;
use crate::logging::not_implemented;
use crate::signals::*;
use crate::syscalls::*;
use crate::task::*;
use crate::types::*;
use zerocopy::FromBytes;

pub fn sys_rt_sigaction(
    current_task: &CurrentTask,
    signum: UncheckedSignal,
    user_action: UserRef<sigaction_t>,
    user_old_action: UserRef<sigaction_t>,
    sigset_size: usize,
) -> Result<(), Errno> {
    if sigset_size != std::mem::size_of::<sigset_t>() {
        return error!(EINVAL);
    }

    let signal = Signal::try_from(signum)?;

    let new_signal_action = if !user_action.is_null() {
        // Actions can't be set for SIGKILL and SIGSTOP, but the actions for these signals can
        // still be returned in `user_old_action`, so only return early if the intention is to
        // set an action (i.e., the user_action is non-null).
        if signal.is_unblockable() {
            return error!(EINVAL);
        }

        let mut signal_action = sigaction_t::default();
        current_task.mm.read_object(user_action, &mut signal_action)?;
        Some(signal_action)
    } else {
        None
    };

    let signal_actions = &current_task.thread_group.signal_actions;
    let old_action = if let Some(new_signal_action) = new_signal_action {
        signal_actions.set(signal, new_signal_action)
    } else {
        signal_actions.get(signal)
    };

    if !user_old_action.is_null() {
        current_task.mm.write_object(user_old_action, &old_action)?;
    }

    Ok(())
}

pub fn sys_rt_sigprocmask(
    current_task: &CurrentTask,
    how: u32,
    user_set: UserRef<sigset_t>,
    user_old_set: UserRef<sigset_t>,
    sigset_size: usize,
) -> Result<(), Errno> {
    if sigset_size != std::mem::size_of::<sigset_t>() {
        return error!(EINVAL);
    }
    match how {
        SIG_BLOCK | SIG_UNBLOCK | SIG_SETMASK => (),
        _ => return error!(EINVAL),
    };

    // Read the new mask. This must be done before the old maks is written to `user_old_set`
    // since it might point to the same location as `user_set`.
    let mut new_mask = sigset_t::default();
    if !user_set.is_null() {
        current_task.mm.read_object(user_set, &mut new_mask)?;
    }

    let mut signal_state = current_task.signals.write();
    let signal_mask = signal_state.mask;
    // If old_set is not null, store the previous value in old_set.
    if !user_old_set.is_null() {
        current_task.mm.write_object(user_old_set, &signal_mask)?;
    }

    // If set is null, how is ignored and the mask is not updated.
    if user_set.is_null() {
        return Ok(());
    }

    let signal_mask = match how {
        SIG_BLOCK => (signal_mask | new_mask),
        SIG_UNBLOCK => signal_mask & !new_mask,
        SIG_SETMASK => new_mask,
        // Arguments have already been verified, this should never match.
        _ => signal_mask,
    };
    signal_state.set_signal_mask(signal_mask);

    Ok(())
}

pub fn sys_sigaltstack(
    current_task: &CurrentTask,
    user_ss: UserRef<sigaltstack_t>,
    user_old_ss: UserRef<sigaltstack_t>,
) -> Result<(), Errno> {
    let mut signal_state = current_task.signals.write();
    let on_signal_stack = signal_state
        .alt_stack
        .map(|signal_stack| signal_stack.contains_pointer(current_task.registers.rsp))
        .unwrap_or(false);

    let mut ss = sigaltstack_t::default();
    if !user_ss.is_null() {
        if on_signal_stack {
            return error!(EPERM);
        }
        current_task.mm.read_object(user_ss, &mut ss)?;
        if (ss.ss_flags & !(SS_AUTODISARM | SS_DISABLE)) != 0 {
            return error!(EINVAL);
        }
    }

    if !user_old_ss.is_null() {
        let mut old_ss = match signal_state.alt_stack {
            Some(old_ss) => old_ss,
            None => sigaltstack_t { ss_flags: SS_DISABLE, ..sigaltstack_t::default() },
        };
        if on_signal_stack {
            old_ss.ss_flags = SS_ONSTACK;
        }
        current_task.mm.write_object(user_old_ss, &old_ss)?;
    }

    if !user_ss.is_null() {
        if ss.ss_flags & SS_DISABLE != 0 {
            signal_state.alt_stack = None;
        } else {
            signal_state.alt_stack = Some(ss);
        }
    }

    Ok(())
}

pub fn sys_rt_sigsuspend(
    current_task: &mut CurrentTask,
    user_mask: UserRef<sigset_t>,
    sigset_size: usize,
) -> Result<(), Errno> {
    if sigset_size != std::mem::size_of::<sigset_t>() {
        return error!(EINVAL);
    }

    let mut mask = sigset_t::default();
    current_task.mm.read_object(user_mask, &mut mask)?;

    let waiter = Waiter::new();
    current_task.wait_with_temporary_mask(mask, |current_task| waiter.wait(current_task))?;

    Ok(())
}

fn send_unchecked_signal(task: &Task, unchecked_signal: &UncheckedSignal) -> Result<(), Errno> {
    // 0 is a sentinel value used to do permission checks.
    let sentinel_signal = UncheckedSignal::from(0);
    if *unchecked_signal == sentinel_signal {
        return Ok(());
    }

    send_signal(
        task,
        SignalInfo {
            code: SI_USER as i32,
            ..SignalInfo::default(Signal::try_from(unchecked_signal)?)
        },
    );
    Ok(())
}

fn send_unchecked_signal_info(
    task: &Task,
    unchecked_signal: &UncheckedSignal,
    info: SignalInfo,
) -> Result<(), Errno> {
    // 0 is a sentinel value used to do permission checks.
    let sentinel_signal = UncheckedSignal::from(0);
    if *unchecked_signal == sentinel_signal {
        return Ok(());
    }

    send_signal(task, info);
    Ok(())
}

pub fn sys_kill(
    current_task: &CurrentTask,
    pid: pid_t,
    unchecked_signal: UncheckedSignal,
) -> Result<(), Errno> {
    match pid {
        pid if pid > 0 => {
            // "If pid is positive, then signal sig is sent to the process with
            // the ID specified by pid."
            let target_thread_group =
                &current_task.get_task(pid).ok_or(errno!(ESRCH))?.thread_group;
            let target =
                get_signal_target(target_thread_group, &unchecked_signal).ok_or(errno!(ESRCH))?;
            if !current_task.can_signal(&target, &unchecked_signal) {
                return error!(EPERM);
            }
            send_unchecked_signal(&target, &unchecked_signal)?;
        }
        pid if pid == -1 => {
            // "If pid equals -1, then sig is sent to every process for which
            // the calling process has permission to send signals, except for
            // process 1 (init), but ... POSIX.1-2001 requires that kill(-1,sig)
            // send sig to all processes that the calling process may send
            // signals to, except possibly for some implementation-defined
            // system processes. Linux allows a process to signal itself, but on
            // Linux the call kill(-1,sig) does not signal the calling process."

            let thread_groups = current_task.thread_group.kernel.pids.read().get_thread_groups();
            signal_thread_groups(
                &current_task,
                &unchecked_signal,
                thread_groups.into_iter().filter(|thread_group| {
                    if current_task.thread_group == *thread_group {
                        return false;
                    }
                    if thread_group.leader == 1 {
                        return false;
                    }
                    true
                }),
            )?;
        }
        _ => {
            // "If pid equals 0, then sig is sent to every process in the
            // process group of the calling process."
            //
            // "If pid is less than -1, then sig is sent to every process in the
            // process group whose ID is -pid."
            let process_group_id = match pid {
                0 => current_task.thread_group.process_group.read().leader,
                _ => -pid,
            };

            let thread_groups = {
                let pids = current_task.thread_group.kernel.pids.read();
                let process_group = pids.get_process_group(process_group_id);
                process_group
                    .iter()
                    .flat_map(|pg| {
                        pg.thread_groups
                            .read()
                            .iter()
                            .flat_map(|p| pids.get_thread_group(*p))
                            .collect::<Vec<_>>()
                    })
                    .collect::<Vec<_>>()
            };
            signal_thread_groups(&current_task, &unchecked_signal, thread_groups.into_iter())?;
        }
    };

    Ok(())
}

pub fn sys_tgkill(
    current_task: &CurrentTask,
    tgid: pid_t,
    tid: pid_t,
    unchecked_signal: UncheckedSignal,
) -> Result<(), Errno> {
    // Linux returns EINVAL when the tgid or tid <= 0.
    if tgid <= 0 || tid <= 0 {
        return error!(EINVAL);
    }

    let target = current_task.get_task(tid).ok_or(errno!(ESRCH))?;
    if target.get_pid() != tgid {
        return error!(EINVAL);
    }

    if !current_task.can_signal(&target, &unchecked_signal) {
        return error!(EPERM);
    }

    send_unchecked_signal(&target, &unchecked_signal)?;
    Ok(())
}

pub fn sys_rt_sigreturn(current_task: &mut CurrentTask) -> Result<SyscallResult, Errno> {
    restore_from_signal_handler(current_task)?;
    Ok(SyscallResult::keep_regs(current_task))
}

pub fn sys_rt_tgsigqueueinfo(
    current_task: &CurrentTask,
    tgid: pid_t,
    tid: pid_t,
    unchecked_signal: UncheckedSignal,
    siginfo_ref: UserAddress,
) -> Result<(), Errno> {
    let mut siginfo_mem = [0u8; SI_MAX_SIZE as usize];
    current_task.mm.read_memory(siginfo_ref, &mut siginfo_mem)?;

    let header = SignalInfoHeader::read_from(&siginfo_mem[..SI_HEADER_SIZE]).unwrap();

    let signal = Signal::try_from(unchecked_signal)?;

    let this_pid = current_task.get_pid();
    if this_pid == tgid {
        if header.code >= 0 || header.code == SI_TKILL {
            return error!(EINVAL);
        }
    }

    let mut bytes = [0u8; SI_MAX_SIZE as usize - SI_HEADER_SIZE];
    bytes.copy_from_slice(&siginfo_mem[SI_HEADER_SIZE..SI_MAX_SIZE as usize]);
    let details = SignalDetail::Raw { data: bytes };
    let signal_info = SignalInfo {
        signal,
        errno: header.errno,
        code: header.code,
        detail: details,
        force: false,
    };

    let target = current_task.get_task(tid).ok_or(errno!(ESRCH))?;
    if target.get_pid() != tgid {
        return error!(EINVAL);
    }
    if !current_task.can_signal(&target, &unchecked_signal) {
        return error!(EPERM);
    }

    send_unchecked_signal_info(&target, &unchecked_signal, signal_info)?;
    Ok(())
}

/// Sends a signal to all thread groups in `thread_groups`.
///
/// # Parameters
/// - `task`: The task that is sending the signal.
/// - `unchecked_signal`: The signal that is to be sent. Unchecked, since `0` is a sentinel value
/// where rights are to be checked but no signal is actually sent.
/// - `thread_groups`: The thread groups to signal.
///
/// # Returns
/// Returns Ok(()) if at least one signal was sent, otherwise the last error that was encountered.
fn signal_thread_groups<F>(
    task: &Task,
    unchecked_signal: &UncheckedSignal,
    thread_groups: F,
) -> Result<(), Errno>
where
    F: Iterator<Item = Arc<ThreadGroup>>,
{
    let mut last_error = errno!(ESRCH);
    let mut sent_signal = false;

    // This loop keeps track of whether a signal was sent, so that "on
    // success (at least one signal was sent), zero is returned."
    for thread_group in thread_groups {
        let target = get_signal_target(&thread_group, unchecked_signal).ok_or(errno!(ESRCH))?;
        if !task.can_signal(&target, &unchecked_signal) {
            last_error = errno!(EPERM);
            continue;
        }

        match send_unchecked_signal(&target, unchecked_signal) {
            Ok(_) => sent_signal = true,
            Err(errno) => last_error = errno,
        }
    }

    if sent_signal {
        return Ok(());
    } else {
        return Err(last_error);
    }
}

/// Waits on the task with `pid` to exit.
///
/// WNOHANG and WNOWAIT are implemented flags for `options`. WUNTRACED, WEXITED and WCONTINUED are
/// not supported, but don't result in an error, only a log. Any other unsupported flags will
/// result in EINVAL.
///
/// - `current_task`: The current task.
/// - `pid`: The id of the task to wait on.
/// - `options`: The options passed to the wait syscall.
fn wait_on_pid(
    current_task: &CurrentTask,
    selector: ProcessSelector,
    options: u32,
) -> Result<Option<ZombieProcess>, Errno> {
    if options & !(WNOHANG | WNOWAIT) != 0 {
        not_implemented!("unsupported wait options: {:#x}", options);
        if options & !(WUNTRACED | WEXITED | WCONTINUED | WNOHANG | WNOWAIT) != 0 {
            return error!(EINVAL);
        }
    }

    let waiter = Waiter::new();
    let mut wait_result = Ok(());
    loop {
        let mut wait_queue = current_task.thread_group.child_exit_waiters.lock();
        if let Some(zombie) =
            current_task.thread_group.get_zombie_child(selector, options & WNOWAIT == 0)
        {
            return Ok(Some(zombie));
        }

        if !current_task.thread_group.has_child(selector) {
            return error!(ECHILD);
        }

        if options & WNOHANG != 0 {
            return Ok(None);
        }
        // Return any error encountered during previous iteration's wait. This is done after the
        // zombie process has been dequeued to make sure that the zombie process is returned even
        // if the wait was interrupted.
        wait_result?;
        wait_queue.wait_async(&waiter);
        std::mem::drop(wait_queue);
        wait_result = waiter.wait(current_task);
    }
}

pub fn sys_waitid(
    current_task: &CurrentTask,
    id_type: u32,
    id: i32,
    user_info: UserAddress,
    options: u32,
) -> Result<(), Errno> {
    // waitid requires at least one of these options.
    if options & (WEXITED | WSTOPPED | WCONTINUED) == 0 {
        return error!(EINVAL);
    }
    // WUNTRACED is not supported (only allowed for waitpid).
    if options & WUNTRACED != 0 {
        return error!(EINVAL);
    }
    // TODO(tbodt): don't allow WEXITED or WSTOPPED in any other wait syscalls, according to the
    // manpage they should be valid only in waitid.

    let task_selector = match id_type {
        P_PID => ProcessSelector::Pid(id),
        P_ALL => ProcessSelector::Any,
        P_PIDFD | P_PGID => {
            not_implemented!("unsupported waitpid id_type {:?}", id_type);
            return error!(ENOSYS);
        }
        _ => return error!(EINVAL),
    };

    // wait_on_pid returns None if the task was not waited on. In that case, we don't write out a
    // siginfo. This seems weird but is the correct behavior according to the waitid(2) man page.
    if let Some(zombie_process) = wait_on_pid(current_task, task_selector, options)? {
        let siginfo = zombie_process.as_signal_info();
        current_task.mm.write_memory(user_info, &siginfo.as_siginfo_bytes())?;
    }

    Ok(())
}

pub fn sys_wait4(
    current_task: &CurrentTask,
    pid: pid_t,
    user_wstatus: UserRef<i32>,
    options: u32,
    user_rusage: UserRef<rusage>,
) -> Result<pid_t, Errno> {
    let selector = if pid == -1 {
        ProcessSelector::Any
    } else if pid > 0 {
        ProcessSelector::Pid(pid)
    } else {
        not_implemented!("unimplemented wait4 pid selector {}", pid);
        return error!(ENOSYS);
    };

    if let Some(zombie_process) = wait_on_pid(current_task, selector, options)? {
        let status = zombie_process.wait_status;

        if !user_rusage.is_null() {
            let usage = rusage::default();
            // TODO(fxb/76976): Return proper usage information.
            current_task.mm.write_object(user_rusage, &usage)?;
            not_implemented!("wait4 does not set rusage info");
        }

        if !user_wstatus.is_null() {
            current_task.mm.write_object(user_wstatus, &status)?;
        }

        Ok(zombie_process.pid)
    } else {
        Ok(0)
    }
}

pub fn sys_signalfd4(
    current_task: &CurrentTask,
    fd: FdNumber,
    mask_addr: UserRef<sigset_t>,
    mask_size: usize,
    flags: u32,
) -> Result<FdNumber, Errno> {
    if fd.raw() != -1 {
        not_implemented!("changing mask of a signalfd");
        return error!(EINVAL);
    }
    if flags & !(SFD_CLOEXEC | SFD_NONBLOCK) != 0 {
        return error!(EINVAL);
    }
    if mask_size != std::mem::size_of::<sigset_t>() {
        return error!(EINVAL);
    }

    let mut mask: sigset_t = 0;
    current_task.mm.read_object(mask_addr, &mut mask)?;
    let signalfd = SignalFd::new(current_task.kernel(), mask, flags);
    let flags = if flags & SFD_CLOEXEC != 0 { FdFlags::CLOEXEC } else { FdFlags::empty() };
    let fd = current_task.files.add_with_flags(signalfd, flags)?;
    Ok(fd)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::auth::Credentials;
    use crate::mm::PAGE_SIZE;
    use crate::testing::*;
    use std::convert::TryInto;
    use zerocopy::AsBytes;

    #[::fuchsia::test]
    fn test_sigaltstack() {
        let (_kernel, current_task) = create_kernel_and_task();
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);

        let user_ss = UserRef::<sigaltstack_t>::new(addr);
        let nullptr = UserRef::<sigaltstack_t>::default();

        // Check that the initial state is disabled.
        sys_sigaltstack(&current_task, nullptr, user_ss).expect("failed to call sigaltstack");
        let mut ss = sigaltstack_t::default();
        current_task.mm.read_object(user_ss, &mut ss).expect("failed to read struct");
        assert!(ss.ss_flags & SS_DISABLE != 0);

        // Install a sigaltstack and read it back out.
        ss.ss_sp = UserAddress::from(0x7FFFF);
        ss.ss_size = 0x1000;
        ss.ss_flags = SS_AUTODISARM;
        current_task.mm.write_object(user_ss, &ss).expect("failed to write struct");
        sys_sigaltstack(&current_task, user_ss, nullptr).expect("failed to call sigaltstack");
        current_task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigaltstack_t>()])
            .expect("failed to clear struct");
        sys_sigaltstack(&current_task, nullptr, user_ss).expect("failed to call sigaltstack");
        let mut another_ss = sigaltstack_t::default();
        current_task.mm.read_object(user_ss, &mut another_ss).expect("failed to read struct");
        assert_eq!(ss, another_ss);

        // Disable the sigaltstack and read it back out.
        ss.ss_flags = SS_DISABLE;
        current_task.mm.write_object(user_ss, &ss).expect("failed to write struct");
        sys_sigaltstack(&current_task, user_ss, nullptr).expect("failed to call sigaltstack");
        current_task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigaltstack_t>()])
            .expect("failed to clear struct");
        sys_sigaltstack(&current_task, nullptr, user_ss).expect("failed to call sigaltstack");
        current_task.mm.read_object(user_ss, &mut ss).expect("failed to read struct");
        assert!(ss.ss_flags & SS_DISABLE != 0);
    }

    /// It is invalid to call rt_sigprocmask with a sigsetsize that does not match the size of
    /// sigset_t.
    #[::fuchsia::test]
    fn test_sigprocmask_invalid_size() {
        let (_kernel, current_task) = create_kernel_and_task();

        let set = UserRef::<sigset_t>::default();
        let old_set = UserRef::<sigset_t>::default();
        let how = 0;

        assert_eq!(
            sys_rt_sigprocmask(
                &current_task,
                how,
                set,
                old_set,
                std::mem::size_of::<sigset_t>() * 2
            ),
            error!(EINVAL)
        );
        assert_eq!(
            sys_rt_sigprocmask(
                &current_task,
                how,
                set,
                old_set,
                std::mem::size_of::<sigset_t>() / 2
            ),
            error!(EINVAL)
        );
    }

    /// It is invalid to call rt_sigprocmask with a bad `how`.
    #[::fuchsia::test]
    fn test_sigprocmask_invalid_how() {
        let (_kernel, current_task) = create_kernel_and_task();
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);

        let set = UserRef::<sigset_t>::new(addr);
        let old_set = UserRef::<sigset_t>::default();
        let how = SIG_SETMASK | SIG_UNBLOCK | SIG_BLOCK;

        assert_eq!(
            sys_rt_sigprocmask(&current_task, how, set, old_set, std::mem::size_of::<sigset_t>()),
            error!(EINVAL)
        );
    }

    /// It is valid to call rt_sigprocmask with a null value for set. In that case, old_set should
    /// contain the current signal mask.
    #[::fuchsia::test]
    fn test_sigprocmask_null_set() {
        let (_kernel, current_task) = create_kernel_and_task();
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        let original_mask = SIGTRAP.mask();
        {
            current_task.signals.write().mask = original_mask;
        }

        let set = UserRef::<sigset_t>::default();
        let old_set = UserRef::<sigset_t>::new(addr);
        let how = SIG_SETMASK;

        current_task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>()])
            .expect("failed to clear struct");

        assert_eq!(
            sys_rt_sigprocmask(&current_task, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(())
        );

        let mut old_mask = sigset_t::default();
        current_task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
    }

    /// It is valid to call rt_sigprocmask with null values for both set and old_set.
    /// In this case, how should be ignored and the set remains the same.
    #[::fuchsia::test]
    fn test_sigprocmask_null_set_and_old_set() {
        let (_kernel, current_task) = create_kernel_and_task();
        let original_mask = SIGTRAP.mask();
        {
            current_task.signals.write().mask = original_mask;
        }

        let set = UserRef::<sigset_t>::default();
        let old_set = UserRef::<sigset_t>::default();
        let how = SIG_SETMASK;

        assert_eq!(
            sys_rt_sigprocmask(&current_task, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(())
        );
        assert_eq!(current_task.signals.read().mask, original_mask);
    }

    /// Calling rt_sigprocmask with SIG_SETMASK should set the mask to the provided set.
    #[::fuchsia::test]
    fn test_sigprocmask_setmask() {
        let (_kernel, current_task) = create_kernel_and_task();
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        current_task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>() * 2])
            .expect("failed to clear struct");

        let original_mask = SIGTRAP.mask();
        {
            current_task.signals.write().mask = original_mask;
        }

        let new_mask: sigset_t = SIGIO.mask();
        let set = UserRef::<sigset_t>::new(addr);
        current_task.mm.write_object(set, &new_mask).expect("failed to set mask");

        let old_set = UserRef::<sigset_t>::new(addr + std::mem::size_of::<sigset_t>());
        let how = SIG_SETMASK;

        assert_eq!(
            sys_rt_sigprocmask(&current_task, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(())
        );

        let mut old_mask = sigset_t::default();
        current_task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
        assert_eq!(current_task.signals.read().mask, new_mask);
    }

    /// Calling st_sigprocmask with a how of SIG_BLOCK should add to the existing set.
    #[::fuchsia::test]
    fn test_sigprocmask_block() {
        let (_kernel, current_task) = create_kernel_and_task();
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        current_task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>() * 2])
            .expect("failed to clear struct");

        let original_mask = SIGTRAP.mask();
        {
            current_task.signals.write().mask = original_mask;
        }

        let new_mask: sigset_t = SIGIO.mask();
        let set = UserRef::<sigset_t>::new(addr);
        current_task.mm.write_object(set, &new_mask).expect("failed to set mask");

        let old_set = UserRef::<sigset_t>::new(addr + std::mem::size_of::<sigset_t>());
        let how = SIG_BLOCK;

        assert_eq!(
            sys_rt_sigprocmask(&current_task, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(())
        );

        let mut old_mask = sigset_t::default();
        current_task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
        assert_eq!(current_task.signals.read().mask, new_mask | original_mask);
    }

    /// Calling st_sigprocmask with a how of SIG_UNBLOCK should remove from the existing set.
    #[::fuchsia::test]
    fn test_sigprocmask_unblock() {
        let (_kernel, current_task) = create_kernel_and_task();
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        current_task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>() * 2])
            .expect("failed to clear struct");

        let original_mask = SIGTRAP.mask() | SIGIO.mask();
        {
            current_task.signals.write().mask = original_mask;
        }

        let new_mask: sigset_t = SIGTRAP.mask();
        let set = UserRef::<sigset_t>::new(addr);
        current_task.mm.write_object(set, &new_mask).expect("failed to set mask");

        let old_set = UserRef::<sigset_t>::new(addr + std::mem::size_of::<sigset_t>());
        let how = SIG_UNBLOCK;

        assert_eq!(
            sys_rt_sigprocmask(&current_task, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(())
        );

        let mut old_mask = sigset_t::default();
        current_task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
        assert_eq!(current_task.signals.read().mask, SIGIO.mask());
    }

    /// It's ok to call sigprocmask to unblock a signal that is not set.
    #[::fuchsia::test]
    fn test_sigprocmask_unblock_not_set() {
        let (_kernel, current_task) = create_kernel_and_task();
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        current_task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>() * 2])
            .expect("failed to clear struct");

        let original_mask = SIGIO.mask();
        {
            current_task.signals.write().mask = original_mask;
        }

        let new_mask: sigset_t = SIGTRAP.mask();
        let set = UserRef::<sigset_t>::new(addr);
        current_task.mm.write_object(set, &new_mask).expect("failed to set mask");

        let old_set = UserRef::<sigset_t>::new(addr + std::mem::size_of::<sigset_t>());
        let how = SIG_UNBLOCK;

        assert_eq!(
            sys_rt_sigprocmask(&current_task, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(())
        );

        let mut old_mask = sigset_t::default();
        current_task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
        assert_eq!(current_task.signals.read().mask, original_mask);
    }

    /// It's not possible to block SIGKILL or SIGSTOP.
    #[::fuchsia::test]
    fn test_sigprocmask_kill_stop() {
        let (_kernel, current_task) = create_kernel_and_task();
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        current_task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>() * 2])
            .expect("failed to clear struct");

        let original_mask = SIGIO.mask();
        {
            current_task.signals.write().mask = original_mask;
        }

        let new_mask: sigset_t = SIGSTOP.mask() | SIGKILL.mask();
        let set = UserRef::<sigset_t>::new(addr);
        current_task.mm.write_object(set, &new_mask).expect("failed to set mask");

        let old_set = UserRef::<sigset_t>::new(addr + std::mem::size_of::<sigset_t>());
        let how = SIG_BLOCK;

        assert_eq!(
            sys_rt_sigprocmask(&current_task, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(())
        );

        let mut old_mask = sigset_t::default();
        current_task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
        assert_eq!(current_task.signals.read().mask, original_mask);
    }

    #[::fuchsia::test]
    fn test_sigaction_invalid_signal() {
        let (_kernel, current_task) = create_kernel_and_task();
        assert_eq!(
            sys_rt_sigaction(
                &current_task,
                UncheckedSignal::from(SIGKILL),
                // The signal is only checked when the action is set (i.e., action is non-null).
                UserRef::<sigaction_t>::new(UserAddress::from(10)),
                UserRef::<sigaction_t>::default(),
                std::mem::size_of::<sigset_t>(),
            ),
            error!(EINVAL)
        );
        assert_eq!(
            sys_rt_sigaction(
                &current_task,
                UncheckedSignal::from(SIGSTOP),
                // The signal is only checked when the action is set (i.e., action is non-null).
                UserRef::<sigaction_t>::new(UserAddress::from(10)),
                UserRef::<sigaction_t>::default(),
                std::mem::size_of::<sigset_t>(),
            ),
            error!(EINVAL)
        );
        assert_eq!(
            sys_rt_sigaction(
                &current_task,
                UncheckedSignal::from(Signal::NUM_SIGNALS + 1),
                // The signal is only checked when the action is set (i.e., action is non-null).
                UserRef::<sigaction_t>::new(UserAddress::from(10)),
                UserRef::<sigaction_t>::default(),
                std::mem::size_of::<sigset_t>(),
            ),
            error!(EINVAL)
        );
    }

    #[::fuchsia::test]
    fn test_sigaction_old_value_set() {
        let (_kernel, current_task) = create_kernel_and_task();
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        current_task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigaction_t>()])
            .expect("failed to clear struct");

        let mut original_action = sigaction_t::default();
        original_action.sa_mask = 3;

        {
            current_task.thread_group.signal_actions.set(SIGHUP, original_action.clone());
        }

        let old_action_ref = UserRef::<sigaction_t>::new(addr);
        assert_eq!(
            sys_rt_sigaction(
                &current_task,
                UncheckedSignal::from(SIGHUP),
                UserRef::<sigaction_t>::default(),
                old_action_ref,
                std::mem::size_of::<sigset_t>()
            ),
            Ok(())
        );

        let mut old_action = sigaction_t::default();
        current_task
            .mm
            .read_object(old_action_ref, &mut old_action)
            .expect("failed to read action");
        assert_eq!(old_action, original_action);
    }

    #[::fuchsia::test]
    fn test_sigaction_new_value_set() {
        let (_kernel, current_task) = create_kernel_and_task();
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        current_task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigaction_t>()])
            .expect("failed to clear struct");

        let mut original_action = sigaction_t::default();
        original_action.sa_mask = 3;
        let set_action_ref = UserRef::<sigaction_t>::new(addr);
        current_task
            .mm
            .write_object(set_action_ref, &original_action)
            .expect("failed to set action");

        assert_eq!(
            sys_rt_sigaction(
                &current_task,
                UncheckedSignal::from(SIGINT),
                set_action_ref,
                UserRef::<sigaction_t>::default(),
                std::mem::size_of::<sigset_t>(),
            ),
            Ok(())
        );

        assert_eq!(current_task.thread_group.signal_actions.get(SIGINT), original_action,);
    }

    /// A task should be able to signal itself.
    #[::fuchsia::test]
    fn test_kill_same_task() {
        let (_kernel, current_task) = create_kernel_and_task();

        assert_eq!(sys_kill(&current_task, current_task.id, SIGINT.into()), Ok(()));
    }

    /// A task should be able to signal its own thread group.
    #[::fuchsia::test]
    fn test_kill_own_thread_group() {
        let (_kernel, init_task) = create_kernel_and_task();
        let task1 = init_task
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");
        task1.thread_group.setsid().expect("setsid");
        let task2 = task1
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");

        assert_eq!(sys_kill(&task1, 0, SIGINT.into()), Ok(()));
        assert_eq!(task1.signals.read().queued_count(SIGINT), 1);
        assert_eq!(task2.signals.read().queued_count(SIGINT), 1);
        assert_eq!(init_task.signals.read().queued_count(SIGINT), 0);
    }

    /// A task should be able to signal a thread group.
    #[::fuchsia::test]
    fn test_kill_thread_group() {
        let (_kernel, init_task) = create_kernel_and_task();
        let task1 = init_task
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");
        task1.thread_group.setsid().expect("setsid");
        let task2 = task1
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");

        assert_eq!(sys_kill(&task1, -task1.id, SIGINT.into()), Ok(()));
        assert_eq!(task1.signals.read().queued_count(SIGINT), 1);
        assert_eq!(task2.signals.read().queued_count(SIGINT), 1);
        assert_eq!(init_task.signals.read().queued_count(SIGINT), 0);
    }

    /// A task should be able to signal everything but init and itself.
    #[::fuchsia::test]
    fn test_kill_all() {
        let (_kernel, init_task) = create_kernel_and_task();
        let task1 = init_task
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");
        task1.thread_group.setsid().expect("setsid");
        let task2 = task1
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");

        assert_eq!(sys_kill(&task1, -1, SIGINT.into()), Ok(()));
        assert_eq!(task1.signals.read().queued_count(SIGINT), 0);
        assert_eq!(task2.signals.read().queued_count(SIGINT), 1);
        assert_eq!(init_task.signals.read().queued_count(SIGINT), 0);
    }

    /// A task should not be able to signal a nonexistent task.
    #[::fuchsia::test]
    fn test_kill_inexistant_task() {
        let (_kernel, current_task) = create_kernel_and_task();

        assert_eq!(sys_kill(&current_task, 9, SIGINT.into()), error!(ESRCH));
    }

    /// A task should not be able to signal a task owned by another uid.
    #[::fuchsia::test]
    fn test_kill_invalid_task() {
        let (_kernel, task1) = create_kernel_and_task();
        // Task must not have the kill capability.
        *task1.creds.write() =
            Credentials::from_passwd("foo:x:1:1").expect("Credentials::from_passwd");
        let task2 = task1
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");
        *task2.creds.write() = Credentials::from_passwd("bin:x:2:2:bin:/bin:/usr/sbin/nologin")
            .expect("build credentials");

        assert_eq!(task1.can_signal(&task2, &SIGINT.into()), false);
        assert_eq!(sys_kill(&task2, task1.id, SIGINT.into()), error!(EPERM));
        assert_eq!(task1.signals.read().queued_count(SIGINT), 0);
    }

    /// A task should not be able to signal a task owned by another uid in a thead group.
    #[::fuchsia::test]
    fn test_kill_invalid_task_in_thread_group() {
        let (_kernel, init_task) = create_kernel_and_task();
        let task1 = init_task
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");
        task1.thread_group.setsid().expect("setsid");
        let task2 = task1
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");
        task2.thread_group.setsid().expect("setsid");
        *task2.creds.write() = Credentials::from_passwd("bin:x:2:2:bin:/bin:/usr/sbin/nologin")
            .expect("build credentials");

        assert_eq!(task2.can_signal(&task1, &SIGINT.into()), false);
        assert_eq!(sys_kill(&task2, -task1.id, SIGINT.into()), error!(EPERM));
        assert_eq!(task1.signals.read().queued_count(SIGINT), 0);
    }

    /// A task should not be able to send an invalid signal.
    #[::fuchsia::test]
    fn test_kill_invalid_signal() {
        let (_kernel, current_task) = create_kernel_and_task();

        assert_eq!(
            sys_kill(&current_task, current_task.id, UncheckedSignal::from(75)),
            error!(EINVAL)
        );
    }

    /// Sending a blocked signal should result in a pending signal.
    #[::fuchsia::test]
    fn test_blocked_signal_pending() {
        let (_kernel, current_task) = create_kernel_and_task();
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        current_task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>() * 2])
            .expect("failed to clear struct");

        let new_mask: sigset_t = SIGIO.mask();
        let set = UserRef::<sigset_t>::new(addr);
        current_task.mm.write_object(set, &new_mask).expect("failed to set mask");

        assert_eq!(
            sys_rt_sigprocmask(
                &current_task,
                SIG_BLOCK,
                set,
                UserRef::default(),
                std::mem::size_of::<sigset_t>()
            ),
            Ok(())
        );
        assert_eq!(sys_kill(&current_task, current_task.id, SIGIO.into()), Ok(()));
        assert_eq!(current_task.signals.read().queued_count(SIGIO), 1);

        // A second signal should not increment the number of pending signals.
        assert_eq!(sys_kill(&current_task, current_task.id, SIGIO.into()), Ok(()));
        assert_eq!(current_task.signals.read().queued_count(SIGIO), 1);
    }

    /// More than one instance of a real-time signal can be blocked.
    #[::fuchsia::test]
    fn test_blocked_real_time_signal_pending() {
        let (_kernel, current_task) = create_kernel_and_task();
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        current_task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>() * 2])
            .expect("failed to clear struct");

        let new_mask: sigset_t = SIGRTMIN.mask();
        let set = UserRef::<sigset_t>::new(addr);
        current_task.mm.write_object(set, &new_mask).expect("failed to set mask");

        assert_eq!(
            sys_rt_sigprocmask(
                &current_task,
                SIG_BLOCK,
                set,
                UserRef::default(),
                std::mem::size_of::<sigset_t>()
            ),
            Ok(())
        );
        assert_eq!(sys_kill(&current_task, current_task.id, SIGRTMIN.into()), Ok(()));
        assert_eq!(current_task.signals.read().queued_count(SIGRTMIN), 1);

        // A second signal should increment the number of pending signals.
        assert_eq!(sys_kill(&current_task, current_task.id, SIGRTMIN.into()), Ok(()));
        assert_eq!(current_task.signals.read().queued_count(SIGRTMIN), 2);
    }

    #[::fuchsia::test]
    fn test_suspend() {
        let (kernel, first_current) = create_kernel_and_task();
        let first_task_clone = first_current.task_arc_clone();
        let first_task_id = first_task_clone.id;

        let thread = std::thread::spawn(move || {
            let mut current_task = first_current;
            let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
            let user_ref = UserRef::<sigset_t>::new(addr);

            let sigset: sigset_t = !SIGCONT.mask();
            current_task.mm.write_object(user_ref, &sigset).expect("failed to set action");

            assert_eq!(
                sys_rt_sigsuspend(&mut current_task, user_ref, std::mem::size_of::<sigset_t>()),
                error!(EINTR)
            );
        });

        let current_task = create_task(&kernel, "test-task-2");

        // Wait for the first task to be suspended.
        let mut suspended = false;
        while !suspended {
            suspended = first_task_clone.signals.read().waiter.is_some();
            std::thread::sleep(std::time::Duration::from_millis(10));
        }

        // Signal the suspended task with a signal that is not blocked (only SIGIO in this test).
        let _ = sys_kill(&current_task, first_task_id, UncheckedSignal::from(SIGCONT));

        // Wait for the sigsuspend to complete.
        let _ = thread.join();

        assert!(first_task_clone.signals.read().waiter.is_none());
    }

    /// Waitid does not support all options.
    #[::fuchsia::test]
    fn test_waitid_options() {
        let (_kernel, current_task) = create_kernel_and_task();
        let id = 1;
        assert_eq!(sys_waitid(&current_task, P_PID, id, UserAddress::default(), 0), Err(EINVAL));
        assert_eq!(
            sys_waitid(&current_task, P_PID, id, UserAddress::default(), WEXITED | WSTOPPED),
            Err(EINVAL)
        );
    }

    #[::fuchsia::test]
    fn test_echild_when_no_zombie() {
        let (_kernel, current_task) = create_kernel_and_task();
        // Send the signal to the task.
        assert!(
            sys_kill(&current_task, current_task.get_pid(), UncheckedSignal::from(SIGCHLD)).is_ok()
        );
        // Verify that ECHILD is returned because there is no zombie process and no children to
        // block waiting for.
        assert_eq!(wait_on_pid(&current_task, ProcessSelector::Any, 0), Err(ECHILD));
    }

    #[::fuchsia::test]
    fn test_no_error_when_zombie() {
        let (_kernel, current_task) = create_kernel_and_task();
        // Send the signal to the task.
        assert!(
            sys_kill(&current_task, current_task.get_pid(), UncheckedSignal::from(SIGCHLD)).is_ok()
        );
        let zombie = ZombieProcess { pid: 0, uid: 0, parent: 3, wait_status: 1 << 8 };
        current_task.thread_group.zombie_children.lock().push(zombie.clone());
        assert_eq!(wait_on_pid(&current_task, ProcessSelector::Any, 0), Ok(Some(zombie)));
    }

    #[::fuchsia::test]
    fn test_exit_status() {
        let (_kernel, current_task) = create_kernel_and_task();
        let mut child = current_task
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");

        // Send SigKill to the child.
        send_signal(&child, SignalInfo::default(SIGKILL));
        dequeue_signal(&mut child);
        std::mem::drop(child);

        // Retrieve the exit status.
        let address =
            map_memory(&current_task, UserAddress::default(), std::mem::size_of::<i32>() as u64);
        let address_ref = UserRef::<i32>::new(address);
        sys_wait4(&current_task, -1, address_ref, 0, UserRef::default()).expect("wait4");
        let mut wstatus: i32 = 0;
        current_task.mm.read_object(address_ref, &mut wstatus).expect("read memory");
        assert_eq!(wstatus, 128 + SIGKILL.number() as i32);
    }

    #[::fuchsia::test]
    fn test_sigqueue() {
        let (kernel, current_task) = create_kernel_and_task();
        let current_uid = current_task.creds.read().uid;
        let current_pid = current_task.get_pid();

        const TEST_VALUE: u64 = 101;

        // Taken from gVisor of SignalInfo in  //pkg/abi/linux/signal.go
        const PID_DATA_OFFSET: usize = SI_HEADER_SIZE;
        const UID_DATA_OFFSET: usize = SI_HEADER_SIZE + 4;
        const VALUE_DATA_OFFSET: usize = SI_HEADER_SIZE + 8;

        let mut data = vec![0u8; SI_MAX_SIZE as usize];
        let mut header = SignalInfoHeader::default();
        header.code = SI_QUEUE;
        header.write_to(&mut data[..SI_HEADER_SIZE]);
        data[PID_DATA_OFFSET..PID_DATA_OFFSET + 4].copy_from_slice(&current_pid.to_ne_bytes());
        data[UID_DATA_OFFSET..UID_DATA_OFFSET + 4].copy_from_slice(&current_uid.to_ne_bytes());
        data[VALUE_DATA_OFFSET..VALUE_DATA_OFFSET + 8].copy_from_slice(&TEST_VALUE.to_ne_bytes());

        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        current_task.mm.write_memory(addr, &data).unwrap();
        let second_current = create_task(&kernel, "second task");
        let second_pid = second_current.get_pid();
        let second_tid = second_current.get_tid();
        assert_eq!(second_current.signals.read().queued_count(SIGIO), 0);

        assert_eq!(
            sys_rt_tgsigqueueinfo(
                &current_task,
                second_pid,
                second_tid,
                UncheckedSignal::from(SIGIO),
                addr
            ),
            Ok(())
        );
        assert_eq!(second_current.signals.read().queued_count(SIGIO), 1);

        let queued_signal = second_current
            .signals
            .write()
            .take_next_where(|sig| sig.signal.is_in_set(SIGIO.mask()));
        if let Some(sig) = queued_signal {
            assert_eq!(sig.signal, SIGIO);
            assert_eq!(sig.errno, 0);
            assert_eq!(sig.code, SI_QUEUE);
            if let SignalDetail::Raw { data } = sig.detail {
                // offsets into the raw portion of the signal info
                let offset_pid = PID_DATA_OFFSET - SI_HEADER_SIZE;
                let offset_uid = UID_DATA_OFFSET - SI_HEADER_SIZE;
                let offset_value = VALUE_DATA_OFFSET - SI_HEADER_SIZE;
                let pid =
                    pid_t::from_ne_bytes(data[offset_pid..offset_pid + 4].try_into().unwrap());
                let uid =
                    uid_t::from_ne_bytes(data[offset_uid..offset_uid + 4].try_into().unwrap());
                let value =
                    u64::from_ne_bytes(data[offset_value..offset_value + 8].try_into().unwrap());
                assert_eq!(pid, current_pid);
                assert_eq!(uid, current_uid);
                assert_eq!(value, TEST_VALUE);
            } else {
                panic!("incorrect signal detail");
            }
        } else {
            panic!("expected a queued signal");
        }
    }
}
