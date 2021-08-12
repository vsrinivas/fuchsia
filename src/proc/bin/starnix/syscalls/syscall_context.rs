// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::sys::zx_thread_state_general_regs_t;
use std::sync::Arc;

use crate::task::{Kernel, Task};

pub struct SyscallContext<'a> {
    pub task: &'a Arc<Task>,

    /// A copy of the registers associated with the Zircon thread. Up-to-date values can be read
    /// from `self.handle.read_state_general_regs()`. To write these values back to the thread, call
    /// `self.handle.write_state_general_regs(self.registers)`.
    pub registers: zx_thread_state_general_regs_t,
}

impl SyscallContext<'_> {
    #[cfg(test)]
    pub fn new<'a>(task: &'a Arc<Task>) -> SyscallContext<'a> {
        SyscallContext { task, registers: zx_thread_state_general_regs_t::default() }
    }

    pub fn kernel(&self) -> &Arc<Kernel> {
        &self.task.thread_group.kernel
    }
}
