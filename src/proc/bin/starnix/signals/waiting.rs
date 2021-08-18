// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::task::{Task, ZombieTask};
use crate::types::*;

/// Waits on the task with `pid` to exit.
///
/// If the task has not yet exited, `should_hang` is called to check whether or not an actual
/// wait should be performed. If `should_hang` then returns false, `wait_on_pid` returns `Ok(None)`.
///
/// - `task`: The task that is waiting on `pid`.
/// - `pid`: The id of the task to wait on.
/// - `should_hang`: Whether or not `task` should hang if the waited on task has not yet exited.
pub fn wait_on_pid(
    task: &Task,
    pid: pid_t,
    should_hang: bool,
) -> Result<Option<ZombieTask>, Errno> {
    let zombie_task = match task.get_zombie_task(pid) {
        Some(zombie_task) => zombie_task,
        None => {
            if !should_hang {
                return Ok(None);
            }
            task.thread_group.kernel.scheduler.write().add_exit_waiter(pid, task.waiter.clone());
            task.waiter.wait()?;
            // It would be an error for more than one task to remove the zombie task,
            // so `expect` that it is present.
            task.get_zombie_task(pid)
                .expect("Waited for task to exit, but it is not present in zombie tasks.")
        }
    };
    Ok(Some(zombie_task))
}

/// Converts the given exit code to a status code suitable for returning from wait syscalls.
pub fn exit_code_to_status(exit_code: Option<i32>) -> i32 {
    match exit_code {
        Some(exit_code) => (exit_code & 0xff) << 8,
        _ => 0,
    }
}
