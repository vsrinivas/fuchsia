// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Definitions specific to test suites that are implemented as ELF components (e.g. gtest and Rust
//! tests).

mod elf_component;
mod server;

pub use elf_component::{start_component, BuilderArgs, Component};
pub use server::{
    EnumeratedTestCases, FidlError, KernelError, MemoizedFutureContainer, PinnedFuture,
    SuiteServer, SuiteServerError,
};
