// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    async_trait::async_trait,
    failure::{self, format_err, Error},
    fidl_fuchsia_input_report as input_report,
    futures::channel::mpsc::Sender,
};

/// Returns a proxy to the first available keyboard input device.
///
/// # Errors
/// If there is an error reading the directory, or no keyboard input device is found.
#[allow(dead_code)]
pub async fn get_keyboard_input_device() -> Result<input_report::InputDeviceProxy, Error> {
    input_device::get_device(input_device::InputDeviceType::Keyboard).await
}

pub struct KeyboardBinding {
    /// The channel to stream InputReports to
    report_stream: Sender<input_report::InputReport>,
}

#[async_trait]
impl input_device::InputDeviceBinding for KeyboardBinding {
    async fn new(_report_stream: Sender<input_report::InputReport>) -> Result<Self, Error> {
        Err(format_err!("Unable to create new Keyboard binding."))
    }

    fn get_report_stream(&self) -> Sender<input_report::InputReport> {
        self.report_stream.clone()
    }
}
