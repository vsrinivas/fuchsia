// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    async_trait::async_trait,
    failure::{self, format_err, Error},
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport, KeyboardDescriptor},
    fidl_fuchsia_ui_input2::Key,
    futures::channel::mpsc::{Receiver, Sender},
};

/// A [`KeyboardBinding`] represents a connection to a keyboard input device.
///
/// The [`KeyboardBinding`] parses and exposes keyboard descriptor properties (e.g., the available
/// keyboard keys) for the device it is associated with. It also parses [`InputReport`]s
/// from the device, and sends them to clients via the stream available at
/// [`KeyboardBinding::input_reports()`].
///
/// # Example
/// ```
/// let mut keyboard_device: KeyboardBinding = input_device::InputDeviceBinding::new().await?;
///
/// while let Some(report) = keyboard_device.input_reports().next().await {}
/// ```
pub struct KeyboardBinding {
    /// The channel to stream InputReports to
    report_sender: Sender<InputReport>,

    /// The receiving end of the input report channel. Clients use this indirectly via
    /// [`input_reports()`].
    report_receiver: Receiver<InputReport>,

    /// The keys available on the keyboard.
    _keys: Vec<Key>,
}

#[async_trait]
impl input_device::InputDeviceBinding for KeyboardBinding {
    fn input_report_sender(&self) -> Sender<InputReport> {
        self.report_sender.clone()
    }

    fn input_report_stream(&mut self) -> &mut Receiver<fidl_fuchsia_input_report::InputReport> {
        return &mut self.report_receiver;
    }

    async fn any_input_device() -> Result<InputDeviceProxy, Error> {
        let mut devices = Self::all_devices().await?;
        devices.pop().ok_or(format_err!("Couldn't find a default keyboard."))
    }

    async fn all_devices() -> Result<Vec<InputDeviceProxy>, Error> {
        input_device::all_devices(input_device::InputDeviceType::Keyboard).await
    }

    async fn bind_device(device: &InputDeviceProxy) -> Result<Self, Error> {
        match device.get_descriptor().await?.keyboard {
            Some(KeyboardDescriptor { keys: Some(keys) }) => {
                let (report_sender, report_receiver) = futures::channel::mpsc::channel(1);
                Ok(KeyboardBinding { report_sender, report_receiver, _keys: keys })
            }
            descriptor => {
                Err(format_err!("Keyboard Descriptor failed to parse: \n {:?}", descriptor))
            }
        }
    }
}
