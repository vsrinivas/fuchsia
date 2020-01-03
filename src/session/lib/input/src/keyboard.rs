// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::input_device::InputDeviceBinding,
    anyhow::{format_err, Error},
    async_trait::async_trait,
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport},
    fuchsia_async as fasync,
    futures::channel::mpsc::{Receiver, Sender},
    futures::StreamExt,
};

#[derive(Copy, Clone)]
pub struct KeyboardInputMessage {}

#[derive(Copy, Clone)]
pub struct KeyboardDescriptor {}

/// A [`KeyboardBinding`] represents a connection to a keyboard input device.
///
/// The [`KeyboardBinding`] parses and exposes keyboard descriptor properties (e.g., the available
/// keyboard keys) for the device it is associated with. It also parses [`InputReport`]s
/// from the device, and sends them to clients via the stream available at
/// [`KeyboardBinding::input_message_stream()`].
///
/// # Example
/// ```
/// let mut keyboard_device: KeyboardBinding = input_device::InputDeviceBinding::new().await?;
///
/// while let Some(report) = keyboard_device.input_message_stream().next().await {}
/// ```
pub struct KeyboardBinding {
    /// The channel to stream InputReports to
    message_sender: Sender<input_device::InputMessage>,

    /// The receiving end of the input report channel. Clients use this indirectly via
    /// [`input_messages()`].
    message_receiver: Receiver<input_device::InputMessage>,

    /// Holds information about this device.
    descriptor: KeyboardDescriptor,
}

#[async_trait]
impl input_device::InputDeviceBinding for KeyboardBinding {
    fn input_message_sender(&self) -> Sender<input_device::InputMessage> {
        self.message_sender.clone()
    }

    fn input_message_stream(&mut self) -> &mut Receiver<input_device::InputMessage> {
        return &mut self.message_receiver;
    }

    fn get_descriptor(&self) -> input_device::InputDescriptor {
        input_device::InputDescriptor::Keyboard(self.descriptor)
    }

    fn process_reports(
        report: InputReport,
        _previous_report: Option<InputReport>,
        _device_descriptor: &mut input_device::InputDescriptor,
        _input_message_sender: &mut Sender<input_device::InputMessage>,
    ) -> Option<InputReport> {
        Some(report)
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
            Some(fidl_fuchsia_input_report::KeyboardDescriptor {
                input: Some(fidl_fuchsia_input_report::KeyboardInputDescriptor { keys: _ }),
            }) => {
                let (message_sender, message_receiver) =
                    futures::channel::mpsc::channel(input_device::INPUT_MESSAGE_BUFFER_SIZE);
                Ok(KeyboardBinding {
                    message_sender,
                    message_receiver,
                    descriptor: KeyboardDescriptor {},
                })
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

/// Returns a stream of InputMessages from all keyboard devices.
///
/// # Errors
/// If there was an error binding to any keyboard.
pub async fn all_keyboard_messages() -> Result<Receiver<input_device::InputMessage>, Error> {
    let bindings = all_keyboard_bindings().await?;
    let (message_sender, message_receiver) =
        futures::channel::mpsc::channel(input_device::INPUT_MESSAGE_BUFFER_SIZE);

    for mut keyboard in bindings {
        let mut sender = message_sender.clone();
        fasync::spawn(async move {
            while let Some(report) = keyboard.input_message_stream().next().await {
                let _ = sender.try_send(report);
            }
        });
    }

    Ok(message_receiver)
}
