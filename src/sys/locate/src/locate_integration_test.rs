// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use failure::Error;
use fidl_fuchsia_sys::FileDescriptor;
use fuchsia_async as fasync;
use fuchsia_component::client::{launch_with_options, launcher, LaunchOptions};
use fuchsia_runtime::HandleType;
use std::io::{BufRead, BufReader};

/// Integration test for the locate shell tool.
///
/// Launches locate as locate.cmx, a component. Attempts to locate and assert
/// the fuchsia-pkg URL of itself, the test component
/// locate_integration_test.cmx.
#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let (pipe, socket) = fdio::pipe_half()?;
    let mut launch_options = LaunchOptions::new();
    launch_options.set_out(FileDescriptor {
        type0: HandleType::FileDescriptor as i32,
        type1: 0,
        type2: 0,
        handle0: Some(socket.into()),
        handle1: None,
        handle2: None,
    });

    let launcher = launcher().expect("Failed to open launcher service");
    let _app = launch_with_options(
        &launcher,
        "fuchsia-pkg://fuchsia.com/locate#meta/locate.cmx".to_string(),
        Some(vec!["locate_integration_test".to_string()]),
        launch_options,
    )
    .expect("Failed to launch locate");

    let mut reader = BufReader::new(pipe);

    // Assert locate.cmx was able to locate locate_integration_test.cmx.
    let mut line = String::new();
    reader.read_line(&mut line)?;
    assert_eq!(
        line,
        "fuchsia-pkg://fuchsia.com/locate_integration_test#meta/locate_integration_test.cmx\n"
    );

    // Assert EOF.
    line.clear();
    reader.read_line(&mut line)?;
    assert_eq!(line.is_empty(), true);

    Ok(())
}
