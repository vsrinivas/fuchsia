// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use crate::commands::{list::*, list_accessors::*, logs::*, selectors::*, show::*, types::*};

#[cfg(target_os = "fuchsia")]
pub use crate::commands::{list_files::*, show_file::*, target::*};

mod list;
mod list_accessors;
#[cfg(target_os = "fuchsia")]
mod list_files;
mod logs;
mod selectors;
mod show;
#[cfg(target_os = "fuchsia")]
mod show_file;
#[cfg(target_os = "fuchsia")]
mod target;
mod types;
mod utils;
