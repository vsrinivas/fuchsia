// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::syscalls::*;
use crate::types::*;

pub fn sys_bpf(
    current_task: &CurrentTask,
    cmd: bpf_cmd,
    _attr_addr: UserRef<bpf_attr>,
    _size: u32,
) -> Result<SyscallResult, Errno> {
    match cmd {
        _ => {
            not_implemented!(current_task, "bpf command {}", cmd);
            error!(EINVAL)
        }
    }
}
