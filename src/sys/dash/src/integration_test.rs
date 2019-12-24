// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_sys::FileDescriptor;
use fuchsia_async as fasync;
use fuchsia_component::client::{launch_with_options, launcher, LaunchOptions};
use fuchsia_runtime::HandleType;
use std::io::{BufRead, BufReader};

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
        "fuchsia-pkg://fuchsia.com/dash_test#meta/component_manager.cmx".to_string(),
        Some(vec!["fuchsia-pkg://fuchsia.com/dash_test#meta/dash_hello.cm".to_string()]),
        launch_options,
    )
    .expect("Failed to launch component_manager with dash_hello");

    let mut reader = BufReader::new(pipe);

    // Assert component manager as a v1 component was able to launch dash as a v2 component was
    // able to launch hello world as a binary.
    let mut line = String::new();
    reader.read_line(&mut line)?;
    assert_eq!(line, "Hippo: Hello World!\n");

    Ok(())
}
