// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    async_trait::async_trait,
    failure::{self, format_err, Error},
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport, KeyboardDescriptor},
    fidl_fuchsia_ui_input2::Key,
    futures::channel::mpsc::Sender,
};

/// Returns a proxy to the first available keyboard input device.
///
/// # Errors
/// If there is an error reading the directory, or no keyboard input device is found.
#[allow(dead_code)]
pub async fn get_keyboard_input_device() -> Result<InputDeviceProxy, Error> {
    input_device::get_device(input_device::InputDeviceType::Keyboard).await
}

/// A [`KeyboardBinding`] represents a connection to a keyboard input device.
///
/// The [`KeyboardBinding`] parses and exposes keyboard descriptor properties (e.g., the available
/// keyboard keys) for the device it is associated with. It also parses [`InputReport`]s
/// from the device, and sends them to clients via [`KeyboardBinding::report_stream`].
///
/// # Example
/// ```
/// let (sender, keyboard_report_receiver) = futures::channel::mpsc::channel(1);
/// let keyboard_device: KeyboardBinding = input_device::InputDeviceBinding::new(sender)).await?;
///
/// while let Some(report) = keyboard_report_receiver.next().await {}
/// ```
pub struct KeyboardBinding {
    /// The channel to stream InputReports to
    report_sender: Sender<InputReport>,

    /// The keys available on the keyboard.
    _keys: Vec<Key>,
}

#[async_trait]
impl input_device::InputDeviceBinding for KeyboardBinding {
    async fn new(report_stream: Sender<InputReport>) -> Result<Self, Error> {
        let device_proxy: InputDeviceProxy = get_keyboard_input_device().await?;

        let keyboard =
            KeyboardBinding::create_keyboard_binding(&device_proxy, report_stream).await?;
        keyboard.listen_for_reports(device_proxy);

        Ok(keyboard)
    }

    fn get_report_stream(&self) -> Sender<InputReport> {
        self.report_sender.clone()
    }
}

impl KeyboardBinding {
    /// Creates a [`KeyboardBinding`] from an [`InputDeviceProxy`]
    ///
    /// # Parameters
    /// - `keyboard_device`: An input device associated with a keyboard device.
    /// - `report_sender`: The sender to which the [`KeyboardBinding`] will send input reports.
    ///
    /// # Errors
    /// If the `keyboard_device` does not represent a keyboard, or the parsing of the device's descriptor
    /// fails.
    async fn create_keyboard_binding(
        keyboard_device: &InputDeviceProxy,
        report_sender: Sender<fidl_fuchsia_input_report::InputReport>,
    ) -> Result<KeyboardBinding, Error> {
        match keyboard_device.get_descriptor().await?.keyboard {
            Some(KeyboardDescriptor { keys: Some(keys) }) => {
                Ok(KeyboardBinding { report_sender, _keys: keys })
            }
            descriptor => {
                Err(format_err!("Keyboard Descriptor failed to parse: \n {:?}", descriptor))
            }
        }
    }
}
