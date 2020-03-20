// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `scoped_task` provides wrappers that kill a child process when the
//! wrapper goes out of scope, or the current process exits.
//!
//! Note that this doesn't guarantee that the child process will be killed in
//! all cases: if the current process is terminated, or exits with
//! [`std::process::abort`], the child process will not be killed. Panics and
//! [`std::process::exit`] are explicitly supported, however.
//!
//! These wrappers are especially useful for tests that spawn child processes.
//! If an assert fails, the child process can live forever. This can cause the
//! test runner to hang if the child process cloned the test's stdout or stderr
//! handle, which is often the case.
//!
//! For this reason, it is recommended that all Rust tests which spawn processes
//! use `scoped_task` to do so.

mod zircon;

pub use zircon::{create_child_job, install_hooks, job_default, spawn, spawn_etc, Scoped};
