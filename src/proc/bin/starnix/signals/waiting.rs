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
    let waiter = Waiter::for_task(current_task);
    loop {
        let mut wait_queue = current_task.thread_group.child_exit_waiters.lock();
        if let Some(zombie) = current_task.get_zombie_child(selector) {
            return Ok(Some(zombie));
        }
        if !should_hang {
            return Ok(None);
        }
        wait_queue.wait_async(&waiter);
        std::mem::drop(wait_queue);
        waiter.wait(current_task)?;
    }
}

/// Converts the given exit code to a status code suitable for returning from wait syscalls.
pub fn exit_code_to_status(exit_code: Option<i32>) -> i32 {
    let exit_code = exit_code.expect("a process should not be exiting without an exit code");
    (exit_code & 0xff) << 8
}
