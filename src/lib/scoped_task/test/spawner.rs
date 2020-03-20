// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Spawns a subprocess in a configurable way. Used by the scoped_task test.

use argh::FromArgs;
use cstr::cstr;
use fdio::SpawnOptions;
use fuchsia_runtime as runtime;
use fuchsia_zircon as zx;
use scoped_task;
use std::any::Any;
use std::env;
use std::ffi::{CStr, CString};
use std::thread;

/// Spawns a subprocess in a configurable way. Used by the scoped_task test.
#[derive(FromArgs, Clone, Copy)]
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
    if opts.sleep {
        zx::Time::INFINITE.sleep();
    }

    let mut _process = None;
    if opts.spawn {
        if opts.spawn_on_thread {
            scoped_task::install_hooks();
            thread::spawn(move || {
                let _proc = spawn_sleeper(opts);
                println!("process spawned");
                zx::Time::INFINITE.sleep();
            });
        // We let the creation of the child process creation race the
        // termination of the parent process. Both cases should work.
        } else {
            _process = Some(spawn_sleeper(opts));
        }
    }

    if opts.panic {
        panic!("panic requested");
    }
}

fn spawn_sleeper(opts: Options) -> Box<dyn Any> {
    let bin = env::args().next().unwrap();
    let bin = CString::new(bin).unwrap();
    let args: [&CStr; 2] = [&bin, cstr!("--sleep")];

    if opts.unscoped {
        if opts.job || opts.kill {
            panic!("not supported");
        }
        return Box::new(fdio::spawn(
            &runtime::job_default(),
            SpawnOptions::CLONE_ALL,
            &bin,
            &args,
        ));
    }

    let job = match opts.job {
        true => Some(scoped_task::create_child_job().expect("could not create job")),
        false => None,
    };
    let job_ref = job.as_ref().unwrap_or_else(|| scoped_task::job_default());

    let proc =
        scoped_task::spawn(job_ref, SpawnOptions::CLONE_ALL, &bin, &args).expect("could not spawn");
    let proc: Box<dyn Any> = match opts.kill {
        true => Box::new(proc.kill()),
        false => Box::new(proc),
    };
    Box::new((job, proc))
}
