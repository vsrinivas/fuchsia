// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fdio;
use fuchsia_runtime as runtime;
use fuchsia_syslog as syslog;
use fuchsia_zircon as zx;
use lazy_static::lazy_static;
use libc;
use std::borrow::Borrow;
use std::ffi::CStr;
use std::ops::Deref;
use std::panic;

lazy_static! {
    static ref SCOPED_JOB: Scoped<zx::Job> = initialize();
}

fn initialize() -> Scoped<zx::Job> {
    let job = runtime::job_default().create_child_job().expect("couldn't create job");

    unsafe {
        libc::atexit(exit_hook);
    }
    install_panic_hook();
    // We cannot panic past this point.

    Scoped::new(job)
}

#[cfg(rust_panic = "abort")]
fn install_panic_hook() {
    println!("installing panic hook");
    let old_hook = panic::take_hook();
    let new_hook = Box::new(move |panic_info: &panic::PanicInfo<'_>| {
        exit_hook();
        old_hook(panic_info);
    });
    panic::set_hook(new_hook);
}

#[cfg(rust_panic = "unwind")]
fn install_panic_hook() {
    // When panic=unwind we can rely on the destructor of the individual
    // processes.
}

extern "C" fn exit_hook() {
    kill(SCOPED_JOB.0.as_ref().unwrap(), "scoped job root");
}

/// Installs the hooks that ensure scoped tasks are cleaned up on panic and
/// process exit.
///
/// Programs and tests that are multithreaded should call this before starting
/// any threads. Single-threaded programs do not have to call this, and can rely
/// on hooks being installed lazily.
///
/// This function can be called multiple times. It will not have any effect
/// after the first call.
pub fn install_hooks() {
    let _ = &*SCOPED_JOB;
}

/// Returns the default job for spawning scoped processes.
///
/// This job is a child of the job returned by [`fuchsia_runtime::job_default`].
pub fn job_default() -> &'static Scoped<zx::Job> {
    &*SCOPED_JOB
}

/// Creates and returns a child job of the job returned by [`job_default`].
pub fn create_child_job() -> Result<Scoped<zx::Job>, zx::Status> {
    Ok(Scoped::new(SCOPED_JOB.create_child_job()?))
}

/// A convenience wrapper around [`fdio::spawn_etc`] that returns a
/// [`Scoped`].
///
/// Note that you must assign the return value to a local. You can use a leading
/// underscore in the name to avoid the unused variable lint, but don't use just
/// `_` or the process will be killed immediately. For example,
///
/// ```
/// let _process = scoped_task::spawn_etc(
///     &scoped_task::job_default(),
///     SpawnOptions::CLONE_ALL,
///     cstr!("/pkg/bin/echo"),
///     &[cstr!("hello world")],
///     None,
///     &mut [],
/// ).expect("could not spawn process");
/// ```
pub fn spawn_etc<'a>(
    job: impl Borrow<&'a Scoped<zx::Job>>,
    options: fdio::SpawnOptions,
    path: &CStr,
    argv: &[&CStr],
    environ: Option<&[&CStr]>,
    actions: &mut [fdio::SpawnAction<'_>],
) -> Result<Scoped<zx::Process>, (zx::Status, String)> {
    fdio::spawn_etc(job.borrow().0.as_ref().unwrap(), options, path, argv, environ, actions)
        .map(Scoped::new)
}

/// A convenience wrapper around [`fdio::spawn`] that returns a
/// [`Scoped`].
///
/// Note that you must assign the return value to a local. You can use a leading
/// underscore in the name to avoid the unused variable lint, but don't use just
/// `_` or the process will be killed immediately. For example,
///
/// ```
/// let _process = scoped_task::spawn(
///     &scoped_task::job_default(),
///     SpawnOptions::CLONE_ALL,
///     cstr!("/pkg/bin/echo"),
///     &[cstr!("hello world")],
/// ).expect("could not spawn process");
/// ```
pub fn spawn<'a>(
    job: impl Borrow<&'a Scoped<zx::Job>>,
    options: fdio::SpawnOptions,
    path: &CStr,
    argv: &[&CStr],
) -> Result<Scoped<zx::Process>, zx::Status> {
    fdio::spawn(job.borrow().0.as_ref().unwrap(), options, path, argv).map(Scoped::new)
}

/// Scoped wrapper for a process or job backed by a Zircon handle.
///
/// The process or job is killed when the wrapper goes out of scope or the
/// current process panics. See the module-level documentation for more details.
#[must_use]
pub struct Scoped<T: zx::Task = zx::Process>(Option<T>);

impl<T: zx::Task> Scoped<T> {
    fn new(process: T) -> Self {
        Scoped(Some(process))
    }

    /// Kills the process, consuming the Scoped wrapper and returning
    /// the inner task object.
    pub fn kill(mut self) -> Result<T, zx::Status> {
        let inner = self.0.take().unwrap();
        let result = inner.kill();
        result.map(|()| inner)
    }

    /// Consumes the Scoped wrapper and returns the inner task object. The task
    /// will no longer be killed on Drop, but may still be killed on exit.
    #[must_use]
    pub fn into_inner(mut self) -> T {
        self.0.take().unwrap()
    }
}

impl<T: zx::Task> Drop for Scoped<T> {
    fn drop(&mut self) {
        if let Some(process) = self.0.take() {
            kill(&process, "scoped task");
        }
    }
}

impl<T: zx::Task> Deref for Scoped<T> {
    type Target = T;
    fn deref(&self) -> &T {
        self.0.as_ref().unwrap()
    }
}

fn kill<T: zx::Task>(process: &T, what: &'static str) {
    let result: Result<(), zx::Status> = (|| {
        process.kill()?;
        process.wait_handle(zx::Signals::TASK_TERMINATED, zx::Time::INFINITE)?;
        Ok(())
    })();
    match result {
        Ok(()) => {}
        Err(e) => {
            eprintln!("error: could not kill {}: {}", what, e);
            syslog::fx_log_err!("error: could not kill {}: {}", what, e);
        }
    }
}
