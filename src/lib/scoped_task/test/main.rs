// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use cstr::cstr;
use fdio::{self, SpawnOptions};
use fuchsia_runtime as runtime;
use fuchsia_zircon::{self as zx, AsHandleRef, Task};
use std::ffi::{CStr, CString};
use std::mem::size_of_val;

#[test]
fn zx_singlethreaded() {
    run_test_with_args(&["--spawn"], true);
    run_test_with_args(&["--spawn", "--job"], true);
    run_test_with_args(&["--spawn", "--kill"], true);
    run_test_with_args(&["--spawn", "--job", "--kill"], true);
}

#[test]
fn zx_singlethreaded_with_panic() {
    run_test_with_args(&["--spawn", "--panic"], false);
    run_test_with_args(&["--spawn", "--panic", "--job"], false);
    run_test_with_args(&["--spawn", "--panic", "--kill"], false);
    run_test_with_args(&["--spawn", "--panic", "--job", "--kill"], false);
}

#[test]
fn zx_multithreaded() {
    run_test_with_args(&["--spawn", "--spawn-on-thread"], true);
    run_test_with_args(&["--spawn", "--spawn-on-thread", "--job"], true);
    run_test_with_args(&["--spawn", "--spawn-on-thread", "--kill"], true);
    run_test_with_args(&["--spawn", "--spawn-on-thread", "--job", "--kill"], true);
}

#[test]
fn zx_multithreaded_with_panic() {
    run_test_with_args(&["--spawn", "--spawn-on-thread", "--panic"], false);
    run_test_with_args(&["--spawn", "--spawn-on-thread", "--panic", "--job"], false);
    run_test_with_args(&["--spawn", "--spawn-on-thread", "--panic", "--kill"], false);
    run_test_with_args(&["--spawn", "--spawn-on-thread", "--panic", "--job", "--kill"], false);
}

#[test]
#[should_panic(expected = "did not exit")]
fn zx_singlethreaded_unscoped() {
    run_test_with_args(&["--spawn", "--unscoped"], true);
}

#[test]
#[should_panic(expected = "did not exit")]
fn zx_singlethreaded_unscoped_with_panic() {
    run_test_with_args(&["--spawn", "--unscoped", "--panic"], false);
}

fn run_test_with_args(args: &[&str], expect_success: bool) {
    let args = args.iter().map(|s| CString::new(*s).unwrap()).collect::<Vec<_>>();
    let args = args.iter().map(|s| s.as_c_str()).collect::<Vec<_>>();
    run_test_with_c_args(&args, expect_success);
}

fn run_test_with_c_args(args: &[&CStr], expect_success: bool) {
    println!("running spawner with args: {:?}", args);

    let child_job = runtime::job_default().create_child_job().unwrap();
    let bin = cstr!("/pkg/bin/scoped_task_test_spawner");
    let args: Vec<&CStr> = Some(bin).iter().chain(args.into_iter()).copied().collect();

    let process = fdio::spawn(
        &child_job,
        // Avoid cloning stdio to prevent hangs in the case of a test failure.
        SpawnOptions::CLONE_ALL, // - SpawnOptions::CLONE_STDIO,
        bin,
        &args,
    )
    .expect("could not spawn");
    process
        .wait_handle(zx::Signals::PROCESS_TERMINATED, zx::Time::INFINITE)
        .expect("could not wait");
    let info = process.info().unwrap();
    if expect_success {
        assert_eq!(0, info.return_code);
    } else {
        assert_ne!(0, info.return_code);
    }
    check_all_processes_terminated(&child_job);
}

fn check_all_processes_terminated(job: &zx::Job) {
    use fuchsia_zircon::sys::*;

    let mut koids: [zx_koid_t; 32] = Default::default();
    let mut actual: usize = 0;
    let mut avail: usize = 0;
    let status = unsafe {
        zx_object_get_info(
            job.raw_handle(),
            ZX_INFO_JOB_PROCESSES,
            koids.as_mut_ptr() as *mut u8,
            size_of_val(&koids),
            &mut actual as *mut usize,
            &mut avail as *mut usize,
        )
    };
    assert_eq!(zx::Status::OK, zx::Status::from_raw(status));
    assert_eq!(actual, avail, "too many child processes");

    println!("process_koids={:?}", &koids[0..actual]);
    for koid in &koids[0..actual] {
        let process: zx::Process = unsafe {
            let mut handle: zx_handle_t = Default::default();
            let status = zx_object_get_child(
                job.raw_handle(),
                *koid,
                ZX_RIGHT_SAME_RIGHTS,
                &mut handle as *mut zx_handle_t,
            );
            assert_eq!(zx::Status::OK, zx::Status::from_raw(status));
            zx::Handle::from_raw(handle)
        }
        .into();
        let info = process.info().unwrap();
        if !info.exited {
            process.kill().unwrap();
        }
        assert!(info.exited, "process koid {} did not exit", koid);
    }

    // Recurse into sub-jobs.
    let status = unsafe {
        zx_object_get_info(
            job.raw_handle(),
            ZX_INFO_JOB_CHILDREN,
            koids.as_mut_ptr() as *mut u8,
            size_of_val(&koids),
            &mut actual as *mut usize,
            &mut avail as *mut usize,
        )
    };
    assert_eq!(zx::Status::OK, zx::Status::from_raw(status));
    assert_eq!(actual, avail, "too many child jobs");

    println!("job_koids={:?}", &koids[0..actual]);
    for koid in &koids[0..actual] {
        let job: zx::Job = unsafe {
            let mut handle: zx_handle_t = Default::default();
            let status = zx_object_get_child(
                job.raw_handle(),
                *koid,
                ZX_RIGHT_SAME_RIGHTS,
                &mut handle as *mut zx_handle_t,
            );
            assert_eq!(zx::Status::OK, zx::Status::from_raw(status));
            zx::Handle::from_raw(handle)
        }
        .into();
        check_all_processes_terminated(&job);
    }
}
