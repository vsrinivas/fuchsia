//! A crate to wait on a child process with a particular timeout.
//!
//! This crate is an implementation for Unix and Windows of the ability to wait
//! on a child process with a timeout specified. On Windows the implementation
//! is fairly trivial as it's just a call to `WaitForSingleObject` with a
//! timeout argument, but on Unix the implementation is much more involved. The
//! current implementation registeres a `SIGCHLD` handler and initializes some
//! global state. If your application is otherwise handling `SIGCHLD` then bugs
//! may arise.
//!
//! # Example
//!
//! ```no_run
//! use std::process::Command;
//! use wait_timeout::ChildExt;
//! use std::time::Duration;
//!
//! let mut child = Command::new("foo").spawn().unwrap();
//!
//! let one_sec = Duration::from_secs(1);
//! let status_code = match child.wait_timeout(one_sec).unwrap() {
//!     Some(status) => status.code(),
//!     None => {
//!         // child hasn't exited yet
//!         child.kill().unwrap();
//!         child.wait().unwrap().code()
//!     }
//! };
//! ```

#![deny(missing_docs, warnings)]
#![doc(html_root_url = "https://docs.rs/wait-timeout/0.1")]

extern crate libc;

use std::fmt;
use std::io;
use std::process::Child;
use std::time::Duration;

/// Exit status from a child process.
///
/// This type mirrors that in `std::process` but currently must be distinct as
/// the one in `std::process` cannot be created.
#[derive(Eq, PartialEq, Copy, Clone, Debug)]
pub struct ExitStatus(imp::ExitStatus);

#[cfg(unix)] #[path = "unix.rs"]
mod imp;
#[cfg(windows)] #[path = "windows.rs"]
mod imp;

/// Extension methods for the standard `std::process::Child` type.
pub trait ChildExt {
    /// Deprecated, use `wait_timeout` instead.
    #[doc(hidden)]
    fn wait_timeout_ms(&mut self, ms: u32) -> io::Result<Option<ExitStatus>> {
        self.wait_timeout(Duration::from_millis(ms as u64))
    }

    /// Wait for this child to exit, timing out after `ms` milliseconds have
    /// elapsed.
    ///
    /// If `Ok(None)` is returned then the timeout period elapsed without the
    /// child exiting, and if `Ok(Some(..))` is returned then the child exited
    /// with the specified exit code.
    ///
    /// # Warning
    ///
    /// Currently this function must be called with great care. If the child
    /// has already been waited on (e.g. `wait` returned a success) then this
    /// function will either wait on another process or fail spuriously on some
    /// platforms. This function may only be reliably called if the process has
    /// not already been waited on.
    ///
    /// Additionally, once this method completes the original child cannot be
    /// waited on reliably. The `wait` method on the original child may return
    /// spurious errors or have odd behavior on some platforms. If this
    /// function returns `Ok(None)`, however, it is safe to wait on the child
    /// with the normal libstd `wait` method.
    fn wait_timeout(&mut self, dur: Duration) -> io::Result<Option<ExitStatus>>;
}

impl ChildExt for Child {
    fn wait_timeout(&mut self, dur: Duration) -> io::Result<Option<ExitStatus>> {
        imp::wait_timeout(self, dur).map(|m| m.map(ExitStatus))
    }
}

impl ExitStatus {
    /// Returns whether this exit status represents a successful execution.
    ///
    /// This typically means that the child process successfully exited with a
    /// status code of 0.
    pub fn success(&self) -> bool {
        self.0.success()
    }

    /// Returns the code associated with the child's exit event.
    ///
    /// On Unix this can return `None` if the child instead exited because of a
    /// signal. On Windows, however, this will always return `Some`.
    pub fn code(&self) -> Option<i32> {
        self.0.code()
    }

    /// Returns the Unix signal which terminated this process.
    ///
    /// Note that on Windows this will always return `None` and on Unix this
    /// will return `None` if the process successfully exited otherwise.
    pub fn unix_signal(&self) -> Option<i32> {
        self.0.unix_signal()
    }
}

impl fmt::Display for ExitStatus {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        if let Some(c) = self.code() {
            write!(f, "exit code: {}", c)
        } else if let Some(s) = self.unix_signal() {
            write!(f, "signal: {}", s)
        } else {
            write!(f, "exit status: unknown")
        }
    }
}
