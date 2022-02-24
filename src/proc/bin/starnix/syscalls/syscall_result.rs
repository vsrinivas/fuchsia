// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::{FdFlags, FdNumber};
use crate::task::CurrentTask;
use crate::types::{FileMode, OpenFlags, UserAddress};

#[derive(PartialEq, Debug)]
pub struct SyscallResult(u64);
pub const SUCCESS: SyscallResult = SyscallResult(0);

impl SyscallResult {
    /// TODO document
    pub fn keep_regs(current_task: &CurrentTask) -> Self {
        SyscallResult(current_task.registers.rax)
    }

    pub fn value(&self) -> u64 {
        self.0
    }
}

impl From<UserAddress> for SyscallResult {
    fn from(value: UserAddress) -> Self {
        SyscallResult(value.ptr() as u64)
    }
}

impl From<FileMode> for SyscallResult {
    fn from(value: FileMode) -> Self {
        SyscallResult(value.bits() as u64)
    }
}

impl From<FdFlags> for SyscallResult {
    fn from(value: FdFlags) -> Self {
        SyscallResult(value.bits() as u64)
    }
}

impl From<OpenFlags> for SyscallResult {
    fn from(value: OpenFlags) -> Self {
        SyscallResult(value.bits() as u64)
    }
}

impl From<FdNumber> for SyscallResult {
    fn from(value: FdNumber) -> Self {
        SyscallResult(value.raw() as u64)
    }
}

impl From<bool> for SyscallResult {
    fn from(value: bool) -> Self {
        SyscallResult(if value { 1 } else { 0 })
    }
}

impl From<i32> for SyscallResult {
    fn from(value: i32) -> Self {
        SyscallResult(value as u64)
    }
}

impl From<u32> for SyscallResult {
    fn from(value: u32) -> Self {
        SyscallResult(value as u64)
    }
}

impl From<i64> for SyscallResult {
    fn from(value: i64) -> Self {
        SyscallResult(value as u64)
    }
}

impl From<u64> for SyscallResult {
    fn from(value: u64) -> Self {
        SyscallResult(value)
    }
}

impl From<usize> for SyscallResult {
    fn from(value: usize) -> Self {
        SyscallResult(value as u64)
    }
}
