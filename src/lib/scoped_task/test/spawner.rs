// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Spawns a subprocess in a configurable way. Used by the scoped_task test.

use anyhow::{format_err, Context};
use argh::FromArgs;
use cstr::cstr;
use fdio::SpawnOptions;
use fuchsia_runtime as runtime;
use fuchsia_zircon as zx;
use scoped_task;
use std::any::Any;
use std::env;
use std::ffi::{CStr, CString};
use std::fs::File;
use std::io::Write;
use std::os::unix::io::FromRawFd;
use std::panic;
use std::sync::mpsc;
use std::thread;

/// Spawns a subprocess in a configurable way. Used by the scoped_task test.
#[derive(FromArgs, Clone, Copy, Debug)]
struct Options {
    /// sleep forever
    #[argh(switch)]
    sleep: bool,
    /// spawn a subprocess (or job if --job is specified)
    #[argh(switch)]
    spawn: bool,
    /// spawn process on a new thread
    #[argh(switch)]
    spawn_on_thread: bool,
    /// wait for thread to finish spawning before exiting or panicking
    #[argh(switch)]
    wait: bool,
    /// spawn a job (with a scoped subprocess) instead of a scoped process
    #[argh(switch)]
    job: bool,
    /// explicitly kill immediately after spawning
    #[argh(switch)]
    kill: bool,
    /// spawn using fdio directly instead of scoped_task
    #[argh(switch)]
    unscoped: bool,
    /// panic on the main thread after spawning
    #[argh(switch)]
    panic: bool,
}

fn main() {
    let opts: Options = argh::from_env();
    println!("spawner opts={:?}", opts);
    if opts.sleep {
        zx::Time::INFINITE.sleep();
    }

    // This test deliberately races threads where one is shutting down the process.
    // This can lead to panics when trying to use the libstd buffered I/O, so we
    // print logs and non-fatal errors directly to stdout instead.
    let mut stdout = unsafe { File::from_raw_fd(1) };

    let mut _process = None;
    let (sender, receiver) = mpsc::channel();
    if opts.spawn {
        if opts.spawn_on_thread {
            // Install scoped_task hook second so it runs before the above hook.
            scoped_task::install_hooks();

            thread::spawn(move || {
                let proc = spawn_sleeper(opts);
                if let Err(e) = &proc {
                    let _ = writeln!(stdout, "{:#}", e);
                    assert!(!opts.wait, "spawning should always succeed with --wait");
                }

                let _ = writeln!(stdout, "process spawned");
                if let Err(e) = sender.send(()) {
                    let _ = writeln!(stdout, "{:#}", e);
                    assert!(!opts.wait, "sending should always succeed with --wait");
                }

                zx::Time::INFINITE.sleep();
            });
        } else {
            _process = Some(
                spawn_sleeper(opts)
                    .expect("spawning should always succeed in the singlethreaded case"),
            );
        }
    }

    if opts.wait {
        assert!(opts.spawn_on_thread);
        receiver.recv().unwrap();
    }
    // If !opts.wait, we let the creation of the child process creation race the
    // termination of the parent process. Both cases should work.

    if opts.panic {
        panic!("panic requested");
    }
}

fn spawn_sleeper(opts: Options) -> anyhow::Result<Box<dyn Any>> {
    let bin = env::args().next().ok_or(format_err!("couldn't get binary name"))?;
    let bin = CString::new(bin).unwrap();
    let args: [&CStr; 2] = [&bin, cstr!("--sleep")];

    if opts.unscoped {
        if opts.job || opts.kill {
            panic!("not supported");
        }
        return Ok(Box::new(fdio::spawn(
            &runtime::job_default(),
            SpawnOptions::CLONE_ALL,
            &bin,
            &args,
        )));
    }

    // N.B. Creating the child job and/or process can fail due to the default
    // scoped job being killed by another thread. We return an error instead of
    // panicking, so we don't get unexpected panics in a test that's supposed to
    // exit gracefully.

    let job = match opts.job {
        true => Some(scoped_task::create_child_job().context("could not create job")?),
        false => None,
    };
    let job_ref = job.as_ref().unwrap_or_else(|| scoped_task::job_default());

    let proc = scoped_task::spawn(job_ref, SpawnOptions::CLONE_ALL, &bin, &args)
        .context("could not spawn")?;
    let proc: Box<dyn Any> = match opts.kill {
        true => Box::new(proc.kill()),
        false => Box::new(proc),
    };
    Ok(Box::new((job, proc)))
}
