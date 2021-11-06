// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::task::*;
use crate::types::*;

/// Waits on the task with `pid` to exit.
///
/// If the task has not yet exited, `should_hang` is called to check whether or not an actual
/// wait should be performed. If `should_hang` then returns false, `wait_on_pid` returns `Ok(None)`.
///
/// - `current_task`: The current task.
/// - `pid`: The id of the task to wait on.
/// - `should_hang`: Whether or not `task` should hang if the waited on task has not yet exited.
pub fn wait_on_pid(
    current_task: &Task,
    selector: TaskSelector,
    should_hang: bool,
) -> Result<Option<ZombieTask>, Errno> {
    let waiter = Waiter::new();
    let mut wait_result = Ok(());
    loop {
        let mut wait_queue = current_task.thread_group.child_exit_waiters.lock();
        if let Some(zombie) = current_task.get_zombie_child(selector) {
            return Ok(Some(zombie));
        }
        if !should_hang {
            return Ok(None);
        }
        // Return any error encountered during previous iteration's wait. This is done after the
        // zombie task has been dequeued to make sure that the zombie task is returned even if the
        // wait was interrupted.
        wait_result?;
        wait_queue.wait_async(&waiter);
        std::mem::drop(wait_queue);
        wait_result = waiter.wait(current_task);
    }
}

/// Converts the given exit code to a status code suitable for returning from wait syscalls.
pub fn exit_code_to_status(exit_code: Option<i32>) -> i32 {
    let exit_code = exit_code.expect("a process should not be exiting without an exit code");
    (exit_code & 0xff) << 8
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::signals::syscalls::sys_kill;
    use crate::testing::*;

    #[test]
    fn test_eintr_when_no_zombie() {
        let (_kernel, current_task) = create_kernel_and_task();
        // Send the signal to the task.
        assert!(
            sys_kill(&current_task, current_task.get_pid(), UncheckedSignal::from(SIGCHLD)).is_ok()
        );
        // Verify that EINTR is returned because there is no zombie task.
        assert_eq!(wait_on_pid(&current_task, TaskSelector::Any, true), Err(EINTR));
    }

    #[test]
    fn test_no_error_when_zombie() {
        let (_kernel, current_task) = create_kernel_and_task();
        // Send the signal to the task.
        assert!(
            sys_kill(&current_task, current_task.get_pid(), UncheckedSignal::from(SIGCHLD)).is_ok()
        );
        let zombie = ZombieTask { id: 0, parent: 3, exit_code: Some(1) };
        current_task.zombie_children.lock().push(zombie.clone());
        assert_eq!(wait_on_pid(&current_task, TaskSelector::Any, true), Ok(Some(zombie)));
    }
}
