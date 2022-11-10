// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod decls;
mod syscall_result;
pub mod table;

pub use syscall_result::*;

// Here we reexport common items needed for syscall implementations. This way you can quickly get
// most things you need by importing crate::syscalls::*.
pub(crate) use crate::logging::{log_trace, not_implemented};
pub use crate::task::CurrentTask;
pub use crate::types::*;

mod misc;
mod time;
