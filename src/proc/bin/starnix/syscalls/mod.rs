// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod decls;
mod syscall_result;
pub mod system;
pub mod table;

pub use syscall_result::*;

pub use crate::task::CurrentTask;
