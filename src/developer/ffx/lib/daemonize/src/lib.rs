// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use libc;
use std::os::unix::process::CommandExt;
use std::process::Command;

/// daemonize adds a pre_exec to call daemon(3) causing the spawned
/// process to be forked again and detached from the controlling
/// terminal.
///
/// The implementation does not violate any of the constraints documented on
/// `Command::pre_exec`, and this code is expected to be safe.
///
/// This code may however cause a process hang if not used appropriately. Reading on
/// the subtleties of CLOEXEC, CLOFORK and forking multi-threaded programs will
/// provide ample background reading. For the sake of safe use, callers should work
/// to ensure that uses of `daemonize` occur early in the program lifecycle, before
/// many threads have been spawned, libraries have been used or files have been
/// opened that may introduce CLOEXEC behaviors that could cause EXTBUSY outcomes in
/// a Linux environment.
pub fn daemonize(c: &mut Command) -> &mut Command {
    unsafe {
        c.pre_exec(|| {
            // daemonize(3) is deprecated on macOS 10.15. The replacement is not
            // yet clear, we may want to replace this with a manual double fork
            // setsid, etc.
            #[allow(deprecated)]
            // First argument: chdir(/)
            // Second argument: do not close stdio (we use stdio to write to the daemon log file)
            match libc::daemon(0, 1) {
                0 => Ok(()),
                x => Err(std::io::Error::from_raw_os_error(x)),
            }
        })
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_daemonize() {
        let started = std::time::Instant::now();
        // TODO(raggi): this technically leaks a sleep process, which is
        // not ideal, but the much better approach would be a
        // significant amount of work, as we'd really want a program
        // that will wait for a signal on some other channel (such as a
        // unix socket) and otherwise linger as a daemon. If we had
        // that, we could then check the ppid and assert that daemon(3)
        // really did the work we're expecting it to. As that would
        // involve specific subprograms, finding those, and so on, it is
        // likely beyond ROI for this test coverage, which aims to just
        // prove that the immediate spawn() succeeded was detached from
        // the program in question. There is a risk that this
        // implementation passes if sleep(1) is not found, which is also
        // not ideal.
        let mut child = daemonize(Command::new("sleep").arg("10")).spawn().expect("child spawned");
        child.wait().expect("child exited successfully");
        assert!(started.elapsed() < std::time::Duration::from_secs(10));
    }
}
