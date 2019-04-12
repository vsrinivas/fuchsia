// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fdio::fdio_sys,
    fidl_fuchsia_sys::FileDescriptor,
    fuchsia_async as fasync,
    fuchsia_component::client::{self, LaunchOptions},
    fuchsia_zircon as zx,
    std::{fs::File, io::Read, os::unix::io::FromRawFd, str, time::{Duration, SystemTime}},
};

fn main() {
    run_test();
}

fn run_test() {
    let mut _executor = fasync::Executor::new().expect("error creating executor");
    let launcher = client::launcher().expect("failed to connect to launcher");
    let (pipe_fd, pipe_handle) = make_pipe();
    let mut options = LaunchOptions::new();
    options.set_out(pipe_handle);
    let _app = client::launch_with_options(
        &launcher,
        "fuchsia-pkg://fuchsia.com/routing_integration_test#meta/component_manager.cmx".to_string(),
        Some(vec![
            "fuchsia-pkg://fuchsia.com/routing_integration_test#meta/echo_realm.cm".to_string()
        ]),
        options,
    )
    .expect("component manager failed to launch");
    let mut pipe = unsafe { File::from_raw_fd(pipe_fd) };
    read_from_pipe(&mut pipe, "Hippos rule!\n");
}

fn make_pipe() -> (i32, FileDescriptor) {
    const PA_FDIO_REMOTE: u32 = 0x30;
    unsafe {
        let mut pipe_handle = zx::sys::ZX_HANDLE_INVALID;
        let mut pipe_fd = -1;
        let status = fdio_sys::fdio_pipe_half2(
            &mut pipe_fd as *mut i32,
            &mut pipe_handle as *mut zx::sys::zx_handle_t,
        );
        if status != zx::sys::ZX_OK {
            panic!("failed to create pipe");
        }
        let pipe_handle = FileDescriptor {
            type0: PA_FDIO_REMOTE as i32,
            type1: 0,
            type2: 0,
            handle0: Some(zx::Handle::from_raw(pipe_handle)),
            handle1: None,
            handle2: None,
        };
        (pipe_fd, pipe_handle)
    }
}

fn read_from_pipe(f: &mut File, msg: &str) {
    let mut buf = [0; 1024];
    let mut out = String::new();
    let start = SystemTime::now();
    while out != msg {
        let n = f.read(&mut buf).expect("failed to read pipe");
        out.push_str(str::from_utf8(&buf[0..n]).expect("string is not utf-8"));
        if start.elapsed().unwrap() > Duration::from_secs(60) {
            panic!("reading from pipe timed out");
        }
    }
}
