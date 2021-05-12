// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryFrom;

use crate::signals::*;
use crate::syscalls::*;
use crate::types::*;

pub fn sys_rt_sigaction(
    ctx: &SyscallContext<'_>,
    signum: UncheckedSignal,
    user_action: UserRef<sigaction_t>,
    user_old_action: UserRef<sigaction_t>,
) -> Result<SyscallResult, Errno> {
    let signal = Signal::try_from(signum)?;
    if signal == Signal::SIGKILL || signal == Signal::SIGSTOP {
        return Err(EINVAL);
    }

    let new_signal_action = if !user_action.is_null() {
        let mut signal_action = sigaction_t::default();
        ctx.task.mm.read_object(user_action, &mut signal_action)?;
        Some(signal_action)
        // TODO: Sometimes the new action will trigger pending signals to be discarded.
    } else {
        None
    };

    let mut signal_actions = ctx.task.thread_group.signal_actions.write();
    if !user_old_action.is_null() {
        let existing_signal_action = signal_actions[&signal];
        ctx.task.mm.write_object(user_old_action, &existing_signal_action)?;
    }

    if let Some(new_signal_action) = new_signal_action {
        signal_actions[&signal] = new_signal_action;
    }

    Ok(SUCCESS)
}

pub fn sys_rt_sigprocmask(
    ctx: &SyscallContext<'_>,
    how: u32,
    user_set: UserRef<sigset_t>,
    user_old_set: UserRef<sigset_t>,
    sigset_size: usize,
) -> Result<SyscallResult, Errno> {
    if sigset_size != std::mem::size_of::<sigset_t>() {
        return Err(EINVAL);
    }
    match how {
        SIG_BLOCK | SIG_UNBLOCK | SIG_SETMASK => (),
        _ => return Err(EINVAL),
    };

    let mut signal_mask = ctx.task.signal_mask.lock();
    // If old_set is not null, store the previous value in old_set.
    if !user_old_set.is_null() {
        ctx.task.mm.write_object(user_old_set, &mut signal_mask)?;
    }

    // If set is null, how is ignored and the mask is not updated.
    if user_set.is_null() {
        return Ok(SUCCESS);
    }

    let mut new_mask = sigset_t::default();
    ctx.task.mm.read_object(user_set, &mut new_mask)?;

    let mut updated_signal_mask = match how {
        SIG_BLOCK => (*signal_mask | new_mask),
        SIG_UNBLOCK => *signal_mask & !new_mask,
        SIG_SETMASK => new_mask,
        // Arguments have already been verified, this should never match.
        _ => *signal_mask,
    };

    // Can't block SIGKILL, or SIGSTOP.
    updated_signal_mask = updated_signal_mask & !(Signal::SIGSTOP.mask() | Signal::SIGKILL.mask());
    *signal_mask = updated_signal_mask;

    Ok(SUCCESS)
}

pub fn sys_sigaltstack(
    ctx: &SyscallContext<'_>,
    user_ss: UserRef<sigaltstack_t>,
    user_old_ss: UserRef<sigaltstack_t>,
) -> Result<SyscallResult, Errno> {
    let mut ss = sigaltstack_t::default();
    if !user_ss.is_null() {
        ctx.task.mm.read_object(user_ss, &mut ss)?;
        if (ss.ss_flags & !(SS_AUTODISARM | SS_DISABLE)) != 0 {
            return Err(EINVAL);
        }
    }

    let mut signal_stack = ctx.task.signal_stack.lock();

    if !user_old_ss.is_null() {
        // TODO: Implement SS_ONSTACK when we actually call the signal handler.
        ctx.task.mm.write_object(
            user_old_ss,
            &match *signal_stack {
                Some(old_ss) => old_ss,
                None => sigaltstack_t { ss_flags: SS_DISABLE, ..sigaltstack_t::default() },
            },
        )?;
    }

    if !user_ss.is_null() {
        if ss.ss_flags & SS_DISABLE != 0 {
            *signal_stack = None;
        } else {
            *signal_stack = Some(ss);
        }
    }

    Ok(SUCCESS)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::mm::PAGE_SIZE;
    use fuchsia_async as fasync;

    use crate::testing::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_sigaltstack() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);

        let user_ss = UserRef::<sigaltstack_t>::new(addr);
        let nullptr = UserRef::<sigaltstack_t>::default();

        // Check that the initial state is disabled.
        sys_sigaltstack(&ctx, nullptr, user_ss).expect("failed to call sigaltstack");
        let mut ss = sigaltstack_t::default();
        ctx.task.mm.read_object(user_ss, &mut ss).expect("failed to read struct");
        assert!(ss.ss_flags & SS_DISABLE != 0);

        // Install a sigaltstack and read it back out.
        ss.ss_sp = UserAddress::from(0x7FFFF);
        ss.ss_size = 0x1000;
        ss.ss_flags = SS_AUTODISARM;
        ctx.task.mm.write_object(user_ss, &ss).expect("failed to write struct");
        sys_sigaltstack(&ctx, user_ss, nullptr).expect("failed to call sigaltstack");
        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigaltstack_t>()])
            .expect("failed to clear struct");
        sys_sigaltstack(&ctx, nullptr, user_ss).expect("failed to call sigaltstack");
        let mut another_ss = sigaltstack_t::default();
        ctx.task.mm.read_object(user_ss, &mut another_ss).expect("failed to read struct");
        assert_eq!(ss, another_ss);

        // Disable the sigaltstack and read it back out.
        ss.ss_flags = SS_DISABLE;
        ctx.task.mm.write_object(user_ss, &ss).expect("failed to write struct");
        sys_sigaltstack(&ctx, user_ss, nullptr).expect("failed to call sigaltstack");
        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigaltstack_t>()])
            .expect("failed to clear struct");
        sys_sigaltstack(&ctx, nullptr, user_ss).expect("failed to call sigaltstack");
        ctx.task.mm.read_object(user_ss, &mut ss).expect("failed to read struct");
        assert!(ss.ss_flags & SS_DISABLE != 0);
    }

    /// It is invalid to call rt_sigprocmask with a sigsetsize that does not match the size of
    /// sigset_t.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_invalid_size() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let set = UserRef::<sigset_t>::default();
        let old_set = UserRef::<sigset_t>::default();
        let how = 0;

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>() * 2),
            Err(EINVAL)
        );
        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>() / 2),
            Err(EINVAL)
        );
    }

    /// It is invalid to call rt_sigprocmask with a bad `how`.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_invalid_how() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);

        let set = UserRef::<sigset_t>::new(addr);
        let old_set = UserRef::<sigset_t>::default();
        let how = SIG_SETMASK | SIG_UNBLOCK | SIG_BLOCK;

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Err(EINVAL)
        );
    }

    /// It is valid to call rt_sigprocmask with a null value for set. In that case, old_set should
    /// contain the current signal mask.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_null_set() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        let original_mask = Signal::SIGTRAP.mask();
        {
            *ctx.task.signal_mask.lock() = original_mask;
        }

        let set = UserRef::<sigset_t>::default();
        let old_set = UserRef::<sigset_t>::new(addr);
        let how = SIG_SETMASK;

        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>()])
            .expect("failed to clear struct");

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(SUCCESS)
        );

        let mut old_mask = sigset_t::default();
        ctx.task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
    }

    /// It is valid to call rt_sigprocmask with null values for both set and old_set.
    /// In this case, how should be ignored and the set remains the same.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_null_set_and_old_set() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let original_mask = Signal::SIGTRAP.mask();
        {
            *ctx.task.signal_mask.lock() = original_mask;
        }

        let set = UserRef::<sigset_t>::default();
        let old_set = UserRef::<sigset_t>::default();
        let how = SIG_SETMASK;

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(SUCCESS)
        );
        assert_eq!(*ctx.task.signal_mask.lock(), original_mask);
    }

    /// Calling rt_sigprocmask with SIG_SETMASK should set the mask to the provided set.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_setmask() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>() * 2])
            .expect("failed to clear struct");

        let original_mask = Signal::SIGTRAP.mask();
        {
            *ctx.task.signal_mask.lock() = original_mask;
        }

        let new_mask: sigset_t = Signal::SIGPOLL.mask();
        let set = UserRef::<sigset_t>::new(addr);
        ctx.task.mm.write_object(set, &new_mask).expect("failed to set mask");

        let old_set = UserRef::<sigset_t>::new(addr + std::mem::size_of::<sigset_t>());
        let how = SIG_SETMASK;

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(SUCCESS)
        );

        let mut old_mask = sigset_t::default();
        ctx.task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
        assert_eq!(*ctx.task.signal_mask.lock(), new_mask);
    }

    /// Calling st_sigprocmask with a how of SIG_BLOCK should add to the existing set.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_block() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>() * 2])
            .expect("failed to clear struct");

        let original_mask = Signal::SIGTRAP.mask();
        {
            *ctx.task.signal_mask.lock() = original_mask;
        }

        let new_mask: sigset_t = Signal::SIGPOLL.mask();
        let set = UserRef::<sigset_t>::new(addr);
        ctx.task.mm.write_object(set, &new_mask).expect("failed to set mask");

        let old_set = UserRef::<sigset_t>::new(addr + std::mem::size_of::<sigset_t>());
        let how = SIG_BLOCK;

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(SUCCESS)
        );

        let mut old_mask = sigset_t::default();
        ctx.task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
        assert_eq!(*ctx.task.signal_mask.lock(), new_mask | original_mask);
    }

    /// Calling st_sigprocmask with a how of SIG_UNBLOCK should remove from the existing set.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_unblock() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>() * 2])
            .expect("failed to clear struct");

        let original_mask = Signal::SIGTRAP.mask() | Signal::SIGPOLL.mask();
        {
            *ctx.task.signal_mask.lock() = original_mask;
        }

        let new_mask: sigset_t = Signal::SIGTRAP.mask();
        let set = UserRef::<sigset_t>::new(addr);
        ctx.task.mm.write_object(set, &new_mask).expect("failed to set mask");

        let old_set = UserRef::<sigset_t>::new(addr + std::mem::size_of::<sigset_t>());
        let how = SIG_UNBLOCK;

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(SUCCESS)
        );

        let mut old_mask = sigset_t::default();
        ctx.task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
        assert_eq!(*ctx.task.signal_mask.lock(), Signal::SIGPOLL.mask());
    }

    /// It's ok to call sigprocmask to unblock a signal that is not set.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_unblock_not_set() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>() * 2])
            .expect("failed to clear struct");

        let original_mask = Signal::SIGPOLL.mask();
        {
            *ctx.task.signal_mask.lock() = original_mask;
        }

        let new_mask: sigset_t = Signal::SIGTRAP.mask();
        let set = UserRef::<sigset_t>::new(addr);
        ctx.task.mm.write_object(set, &new_mask).expect("failed to set mask");

        let old_set = UserRef::<sigset_t>::new(addr + std::mem::size_of::<sigset_t>());
        let how = SIG_UNBLOCK;

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(SUCCESS)
        );

        let mut old_mask = sigset_t::default();
        ctx.task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
        assert_eq!(*ctx.task.signal_mask.lock(), original_mask);
    }

    /// It's not possible to block SIGKILL or SIGSTOP.
    #[fasync::run_singlethreaded(test)]
    async fn test_sigprocmask_kill_stop() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigset_t>() * 2])
            .expect("failed to clear struct");

        let original_mask = Signal::SIGPOLL.mask();
        {
            *ctx.task.signal_mask.lock() = original_mask;
        }

        let new_mask: sigset_t = Signal::SIGSTOP.mask() | Signal::SIGKILL.mask();
        let set = UserRef::<sigset_t>::new(addr);
        ctx.task.mm.write_object(set, &new_mask).expect("failed to set mask");

        let old_set = UserRef::<sigset_t>::new(addr + std::mem::size_of::<sigset_t>());
        let how = SIG_BLOCK;

        assert_eq!(
            sys_rt_sigprocmask(&ctx, how, set, old_set, std::mem::size_of::<sigset_t>()),
            Ok(SUCCESS)
        );

        let mut old_mask = sigset_t::default();
        ctx.task.mm.read_object(old_set, &mut old_mask).expect("failed to read mask");
        assert_eq!(old_mask, original_mask);
        assert_eq!(*ctx.task.signal_mask.lock(), original_mask);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_sigaction_invalid_signal() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        assert_eq!(
            sys_rt_sigaction(
                &ctx,
                UncheckedSignal::from(SIGKILL),
                UserRef::<sigaction_t>::default(),
                UserRef::<sigaction_t>::default()
            ),
            Err(EINVAL)
        );
        assert_eq!(
            sys_rt_sigaction(
                &ctx,
                UncheckedSignal::from(SIGSTOP),
                UserRef::<sigaction_t>::default(),
                UserRef::<sigaction_t>::default()
            ),
            Err(EINVAL)
        );
        assert_eq!(
            sys_rt_sigaction(
                &ctx,
                UncheckedSignal::from(Signal::NUM_SIGNALS + 1),
                UserRef::<sigaction_t>::default(),
                UserRef::<sigaction_t>::default()
            ),
            Err(EINVAL)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_sigaction_old_value_set() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigaction_t>()])
            .expect("failed to clear struct");

        let mut original_action = sigaction_t::default();
        original_action.sa_mask = 3;

        {
            ctx.task.thread_group.signal_actions.write()[&Signal::SIGHUP] = original_action.clone();
        }

        let old_action_ref = UserRef::<sigaction_t>::new(addr);
        assert_eq!(
            sys_rt_sigaction(
                &ctx,
                UncheckedSignal::from(SIGHUP),
                UserRef::<sigaction_t>::default(),
                old_action_ref
            ),
            Ok(SUCCESS)
        );

        let mut old_action = sigaction_t::default();
        ctx.task.mm.read_object(old_action_ref, &mut old_action).expect("failed to read action");
        assert_eq!(old_action, original_action);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_sigaction_new_value_set() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        ctx.task
            .mm
            .write_memory(addr, &[0u8; std::mem::size_of::<sigaction_t>()])
            .expect("failed to clear struct");

        let mut original_action = sigaction_t::default();
        original_action.sa_mask = 3;
        let set_action_ref = UserRef::<sigaction_t>::new(addr);
        ctx.task.mm.write_object(set_action_ref, &original_action).expect("failed to set action");

        assert_eq!(
            sys_rt_sigaction(
                &ctx,
                UncheckedSignal::from(SIGINT),
                set_action_ref,
                UserRef::<sigaction_t>::default()
            ),
            Ok(SUCCESS)
        );

        assert_eq!(ctx.task.thread_group.signal_actions.read()[&Signal::SIGINT], original_action,);
    }
}
