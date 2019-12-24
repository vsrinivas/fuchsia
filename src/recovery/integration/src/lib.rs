// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use anyhow::{Context as _, Error};
use fidl_fuchsia_sys::FileDescriptor;
use fuchsia_async as fasync;
use fuchsia_component::client::{launch_with_options, launcher, LaunchOptions};
use fuchsia_runtime::HandleType;
use std::io::{BufRead, BufReader};

#[fasync::run_singlethreaded]
#[test]
async fn test_startup() -> Result<(), Error> {
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

    let launcher = launcher().context("Failed to open launcher service")?;
    let _recovery = launch_with_options(
        &launcher,
        "fuchsia-pkg://fuchsia.com/system_recovery#meta/system_recovery.cmx".to_string(),
        None,
        launch_options,
    )
    .context("Failed to launch system_recovery")?;

    let mut reader = BufReader::new(pipe);
    let mut line = String::new();
    reader.read_line(&mut line)?;

    assert_eq!(line, "recovery: started\n");

    Ok(())
}
