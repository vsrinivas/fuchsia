// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::input_device::InputDeviceBinding,
    async_trait::async_trait,
    failure::{self, format_err, Error},
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport, KeyboardDescriptor},
    fidl_fuchsia_ui_input2::Key,
    fuchsia_async as fasync,
    futures::channel::mpsc::{Receiver, Sender},
    futures::StreamExt,
};

/// A [`KeyboardBinding`] represents a connection to a keyboard input device.
///
/// The [`KeyboardBinding`] parses and exposes keyboard descriptor properties (e.g., the available
/// keyboard keys) for the device it is associated with. It also parses [`InputReport`]s
/// from the device, and sends them to clients via the stream available at
/// [`KeyboardBinding::input_report_stream()`].
///
/// # Example
/// ```
/// let mut keyboard_device: KeyboardBinding = input_device::InputDeviceBinding::new().await?;
///
/// while let Some(report) = keyboard_device.input_report_stream().next().await {}
/// ```
pub struct KeyboardBinding {
    /// The channel to stream InputReports to
    report_sender: Sender<InputReport>,

    /// The receiving end of the input report channel. Clients use this indirectly via
    /// [`input_report_stream()`].
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

/// Returns a vector of [`KeyboardBindings`] for all currently connected keyboards.
///
/// # Errors
/// If there was an error binding to any keyboard.
async fn all_keyboard_bindings() -> Result<Vec<KeyboardBinding>, Error> {
    let device_proxies = input_device::all_devices(input_device::InputDeviceType::Keyboard).await?;
    let mut device_bindings: Vec<KeyboardBinding> = vec![];

    for device_proxy in device_proxies {
        let device_binding: KeyboardBinding =
            input_device::InputDeviceBinding::new(device_proxy).await?;
        device_bindings.push(device_binding);
    }

    Ok(device_bindings)
}

/// Returns a stream of InputReports from all keyboard devices.
///
/// # Errors
/// If there was an error binding to any keyboard.
pub async fn all_keyboard_reports() -> Result<Receiver<InputReport>, Error> {
    let bindings = all_keyboard_bindings().await?;
    let (report_sender, report_receiver) = futures::channel::mpsc::channel(1);

    for mut keyboard in bindings {
        let mut sender = report_sender.clone();
        fasync::spawn(async move {
            while let Some(report) = keyboard.input_report_stream().next().await {
                let _ = sender.try_send(report);
            }
        });
    }

    Ok(report_receiver)
}
