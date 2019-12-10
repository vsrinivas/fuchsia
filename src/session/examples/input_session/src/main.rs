// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fuchsia_async as fasync,
    futures::StreamExt,
    input::{input_device::InputDeviceBinding, mouse::MouseBinding},
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["input_session"]).expect("Failed to initialize logger.");

    let mut mouse: MouseBinding = InputDeviceBinding::new().await?;

    while let Some(_report) = mouse.input_reports().next().await {}
    Ok(())
}
