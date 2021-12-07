// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains utility functions for process control.

use anyhow::{bail, Result};
use nix;
use shared_child::SharedChild;
use std::{
    convert::TryInto,
    sync::atomic::{AtomicBool, Ordering},
    sync::Arc,
    thread, time,
};

/// Monitors a shared process for the interrupt signal.
///
/// If user runs with --monitor or --console, the  Emulator will be running in the foreground,
/// this function listens for the interrupt signal (ctrl+c), once detected, wait for the emulator
/// process to finish then return.
pub fn monitored_child_process(child_arc: &Arc<SharedChild>) -> Result<()> {
    let child_arc_clone = child_arc.clone();
    let term = Arc::new(AtomicBool::new(false));
    signal_hook::flag::register(signal_hook::consts::SIGINT, Arc::clone(&term))?;
    let thread = std::thread::spawn(move || {
        while !term.load(Ordering::Relaxed) && child_arc_clone.try_wait().unwrap().is_none() {
            thread::sleep(time::Duration::from_secs(1));
        }
        child_arc_clone.wait().expect("Error waiting for emulator process");
    });
    thread.join().expect("cannot join monitor thread");
    Ok(())
}

/// Returns true if the process identified by the pid is running.
pub fn is_running(pid: u32) -> bool {
    if pid != 0 {
        // Check to see if it is running by sending signal 0. If there is no error,
        // the process is running.
        return nix::sys::signal::kill(nix::unistd::Pid::from_raw(pid.try_into().unwrap()), None)
            .is_ok();
    }
    return false;
}

/// Terminates the process.
pub fn terminate(pid: u32) -> Result<()> {
    if pid != 0 && is_running(pid) {
        match nix::sys::signal::kill(
            nix::unistd::Pid::from_raw(pid.try_into().unwrap()),
            Some(nix::sys::signal::Signal::SIGTERM),
        ) {
            Ok(_) => return Ok(()),
            Err(e) => bail!("Terminate error: {}", e),
        };
    }
    return Ok(());
}
