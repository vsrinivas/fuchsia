// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    failure::{self, format_err, Error, ResultExt},
    fdio,
    fidl_fuchsia_input_report::{InputDeviceMarker, InputDeviceProxy, InputReport},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::channel::mpsc::{Receiver, Sender},
    std::{
        fs::{read_dir, ReadDir},
        path::{Path, PathBuf},
    },
};

/// An [`InputDeviceBinding`] represents a binding to an input device (e.g., a mouse).
///
/// [`InputDeviceBinding`]s expose information about the bound device. For example, a
/// [`MouseBinding`] exposes the ranges of possible x and y values the device can generate.
///
/// An [`InputDeviceBinding`] also forwards the bound input device's input report stream.
/// The receiving end of the input stream is accessed by [`InputDeviceBinding::input_reports()`].
///
/// # Example
///
/// ```
/// let mouse_binding = InputDeviceBinding::new().await;
/// while let Some(input_report) = mouse_binding.report_stream_receiver().next().await {
///     // Handle the input report.
/// }
/// ```
#[async_trait]
pub trait InputDeviceBinding: Sized {
    /// Retrieves a proxy to the default input device of type `Self`.
    ///
    /// For example, [`MouseBinding`] finds the first available input device
    /// which is a mouse and returns a proxy to it.
    ///
    /// This allows [`InputDeviceBinding::new`] to have a default implementation
    /// that reduces the boiler plate in each trait-implementer.
    ///
    /// # Errors
    /// If no default device exists.
    async fn default_input_device() -> Result<InputDeviceProxy, Error>;

    /// Binds the provided input device to a new instance of `Self`.
    ///
    /// Trait-implementers are expected to get the device descriptor and make
    /// sure it is of the correct type. For example, a [`MouseBinding`] would
    /// verify that the device has a mouse descriptor.
    ///
    /// The [`MouseBinding`] then uses the device's mouse descriptor to
    /// initialize its fields (e.g., the range of possible x values).
    ///
    /// # Parameters
    /// - `device`: The device to use to initalize the binding.
    ///
    /// # Errors
    /// If the device descriptor could not be retrieved, or the descriptor could
    /// not be parsed correctly.
    async fn bind_device(device: &InputDeviceProxy) -> Result<Self, Error>;

    /// Returns a stream of input events generated from the bound device.
    fn input_reports(&mut self) -> &mut Receiver<fidl_fuchsia_input_report::InputReport>;

    /// Returns the input event stream's sender.
    fn input_report_sender(&self) -> Sender<InputReport>;

    /// Creates a new [`InputDeviceBinding`] for the input type's default input device.
    ///
    /// The binding will start listening for input reports immediately, and they
    /// can be read from [`input_reports()`].
    ///
    /// # Errors
    /// If there was an error finding or binding to the default input device.
    async fn new() -> Result<Self, Error> {
        let device_proxy: InputDeviceProxy = Self::default_input_device().await?;
        let device_binding = Self::bind_device(&device_proxy).await?;
        device_binding.initialize_report_stream(device_proxy);

        Ok(device_binding)
    }

    /// Initializes the input report stream for the bound device.
    ///
    /// Spawns a future which awaits input reports from the device and forwards them to
    /// clients via [`input_report_sender()`]. The reports are observed via [`input_reports()`].
    ///
    /// # Parameters
    /// - `device_proxy`: The device proxy which is used to get input reports.
    fn initialize_report_stream(&self, device_proxy: fidl_fuchsia_input_report::InputDeviceProxy) {
        let mut report_sender = self.input_report_sender();
        fasync::spawn(async move {
            if let Ok((_status, event)) = device_proxy.get_reports_event().await {
                if let Ok(_) = fasync::OnSignals::new(&event, zx::Signals::USER_0).await {
                    while let Ok(input_reports) = device_proxy.get_reports().await {
                        for report in input_reports {
                            let _ = report_sender.try_send(report);
                        }
                    }
                }
            }
            // TODO(lindkvist): Add signaling for when this loop exits, since it means the device
            // binding is no longer functional.
        });
    }
}

/// Returns a proxy to the first InputDevice of type `device_type` in `input_report_path`.
///
/// # Parameters
/// - `device_type`: The type of device to get.
///
/// # Errors
/// If there is an error reading `input_report_path`, or no input device of type `device_type`
/// is found.
pub async fn get_device(
    device_type: InputDeviceType,
) -> Result<fidl_fuchsia_input_report::InputDeviceProxy, Error> {
    let input_report_dir = Path::new(INPUT_REPORT_PATH);
    let entries: ReadDir = read_dir(input_report_dir)
        .with_context(|_| format!("Failed to read {}", INPUT_REPORT_PATH))?;

    for entry in entries {
        let entry_path: PathBuf = entry?.path();
        if let Ok(input_device) = get_device_from_dir_entry_path(&entry_path) {
            if is_device_type(&input_device, device_type).await {
                return Ok(input_device);
                // NOTE: if there are multiple keyboards (e.g., a physical keyboard and a virtual
                // keyboard connected via something like Chrome Remote Desktop), returning just
                // one match is an arbitrary decision, and there's no way to know which one is
                // the "active" keyboard. (In fact, multiple keyboards could be equally active
                // in some configurations as well.)
                // This method of selecting the "first" keyboard in the directory is not
                // practical. But if you call this, you might consider whether to skip
                // the first keyboard if there is more than one.
            }
        }
    }

    Err(format_err!("No input devices found."))
}

#[derive(Clone, Copy)]
pub enum InputDeviceType {
    Keyboard,
    Mouse,
    Touch,
}

/// The path to the input-report directory.
static INPUT_REPORT_PATH: &str = "/dev/class/input-report";

/// Returns true if the device type of `input_device` matches `device_type`.
///
/// # Parameters
/// - `input_device`: The InputDevice to check the type of.
/// - `device_type`: The type of the device to compare to.
async fn is_device_type(
    input_device: &fidl_fuchsia_input_report::InputDeviceProxy,
    device_type: InputDeviceType,
) -> bool {
    let device_descriptor = match input_device.get_descriptor().await {
        Ok(descriptor) => descriptor,
        Err(_) => {
            return false;
        }
    };

    // Return if the device type matches the desired `device_type`.
    match device_type {
        InputDeviceType::Mouse => device_descriptor.mouse.is_some(),
        InputDeviceType::Touch => device_descriptor.touch.is_some(),
        InputDeviceType::Keyboard => device_descriptor.keyboard.is_some(),
    }
}

/// Returns a proxy to the InputDevice in `entry` if it exists.
///
/// # Parameters
/// - `entry`: The directory entry that contains an InputDevice.
///
/// # Errors
/// If there is an error connecting to the InputDevice in `entry`.
fn get_device_from_dir_entry_path(
    entry_path: &PathBuf,
) -> Result<fidl_fuchsia_input_report::InputDeviceProxy, Error> {
    let input_device_path = entry_path.to_str();
    if input_device_path.is_none() {
        return Err(format_err!("Failed to get entry path as a string."));
    }

    let (input_device, server) = fidl::endpoints::create_proxy::<InputDeviceMarker>()?;
    fdio::service_connect(input_device_path.unwrap(), server.into_channel())?;
    Ok(input_device)
}
