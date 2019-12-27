// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ok;
use crate::{AsHandleRef, Status};

use fuchsia_zircon_sys as sys;

pub trait Task: AsHandleRef {
    /// Kill the give task (job, process, or thread).
    ///
    /// Wraps the
    /// [zx_task_kill](https://fuchsia.dev/fuchsia-src/reference/syscalls/task_kill.md)
    /// syscall.
    // TODO: Not yet implemented on Thread, need to add object_get_info impl for Thread for proper
    // testability.
    fn kill(&self) -> Result<(), Status> {
        ok(unsafe { sys::zx_task_kill(self.raw_handle()) })
    }
}
