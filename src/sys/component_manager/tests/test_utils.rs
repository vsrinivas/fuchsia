// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error},
    fidl_fuchsia_sys::FileDescriptor,
    fuchsia_component::client,
    fuchsia_runtime::HandleType,
    fuchsia_zircon as zx,
    parking_lot::{Condvar, Mutex},
    std::{fs::File, io::Read, sync::Arc, thread, time::Duration},
};

const WAIT_TIMEOUT_SEC: u64 = 10;

pub fn launch_test_component(
    component_url: String,
    args: Option<Vec<String>>,
) -> Result<client::App, Error> {
    let launcher = client::launcher().expect("failed to connect to launcher");
    client::launch(&launcher, component_url, args)
}

pub async fn launch_and_wait_for_msg(
    component_url: String,
    args: Option<Vec<String>>,
    expected_msg: String,
) -> Result<(), Error> {
    launch_and_wait_for_msg_with_extra_dirs(component_url, args, None, expected_msg).await
}

pub async fn launch_and_wait_for_msg_with_extra_dirs(
    component_url: String,
    args: Option<Vec<String>>,
    dir_handles: Option<Vec<(String, zx::Handle)>>,
    expected_msg: String,
) -> Result<(), Error> {
    let (pipe, pipe_handle) = make_pipe();
    let mut options = client::LaunchOptions::new();
    options.set_out(pipe_handle);
    if let Some(dir_handles) = dir_handles {
        for dir in dir_handles {
            options.add_handle_to_namespace(dir.0, dir.1);
        }
    }

    let launcher = client::launcher().expect("failed to connect to launcher");
    let _app = client::launch_with_options(&launcher, component_url, args, options)
        .expect("component manager failed to launch");

    read_from_pipe(pipe, expected_msg)
}

fn make_pipe() -> (std::fs::File, FileDescriptor) {
    match fdio::pipe_half() {
        Err(_) => panic!("failed to create pipe"),
        Ok((pipe, handle)) => {
            let pipe_handle = FileDescriptor {
                type0: HandleType::FileDescriptor as i32,
                type1: 0,
                type2: 0,
                handle0: Some(handle.into()),
                handle1: None,
                handle2: None,
            };
            (pipe, pipe_handle)
        }
    }
}

fn read_from_pipe(mut f: File, expected_msg: String) -> Result<(), Error> {
    let pair = Arc::new((Mutex::new(Vec::new()), Condvar::new()));

    // This uses a blocking std::file::File::read call, so we can't use async or the timeout below
    // will never trigger since the read doesn't yield. Need to spawn a thread.
    // TODO: Improve this to use async I/O and replace the thread with an async closure.
    {
        let pair = pair.clone();
        let expected_msg = expected_msg.clone();
        thread::spawn(move || {
            let expected = expected_msg.as_bytes();
            let mut buf = [0; 1024];
            loop {
                let n = f.read(&mut buf).expect("failed to read pipe");

                let (actual, cond) = &*pair;
                let mut actual = actual.lock();
                actual.extend_from_slice(&buf[0..n]);

                // If the read data equals expected message, return early; the test passed. Otherwise
                // keep gathering data until the timeout is reached. This allows tests to print info
                // about failed expectations, even though this is most often used with
                // component_manager.cmx which doesn't exit (so we can't just get output on exit).
                if &**actual == expected {
                    cond.notify_one();
                    return;
                }
            }
        });
    }

    // parking_lot::Condvar has no spurious wakeups, yay!
    let (actual, cond) = &*pair;
    let mut actual = actual.lock();
    if cond.wait_for(&mut actual, Duration::from_secs(WAIT_TIMEOUT_SEC)).timed_out() {
        let actual_msg = String::from_utf8(actual.clone())
            .map(|v| format!("'{}'", v))
            .unwrap_or(format!("{:?}", actual));

        return Err(format_err!(
            "Timed out waiting for matching output\n\
             Expected: '{}'\n\
             Actual: {}",
            expected_msg,
            actual_msg,
        ));
    }
    Ok(())
}
