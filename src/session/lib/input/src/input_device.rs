// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    failure::{self, format_err, Error, ResultExt},
    fdio,
    fidl_fuchsia_input_report::InputDeviceMarker,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::channel::mpsc::Sender,
    std::{
        fs::{read_dir, ReadDir},
        path::{Path, PathBuf},
    },
};

#[derive(Clone, Copy)]
pub enum InputDeviceType {
    Keyboard,
    Mouse,
    Touch,
}

/// Connects an InputDevice to a session.
#[async_trait]
pub trait InputDeviceBinding: Sized {
    /// Creates a new InputDeviceBinding.
    ///
    /// # Errors
    /// If a connection couldn't be made to the device or a device was not found.
    async fn new(
        report_stream: Sender<fidl_fuchsia_input_report::InputReport>,
    ) -> Result<Self, Error>;

    // Returns a clone of the Sender to the channel reporting InputReports to the session.
    /// TODO(vickiecheng): Stream InputMessages instead of InputReports to the session.
    fn get_report_stream(&self) -> Sender<fidl_fuchsia_input_report::InputReport>;

    // Spawns a thread that continuously polls for InputReports. Sends the reports to the binding's
    // `report_stream`.
    fn listen_for_reports(&self, device_proxy: fidl_fuchsia_input_report::InputDeviceProxy) {
        let mut channel_endpoint = self.get_report_stream();
        fasync::spawn(async move {
            let (_status, event) = match device_proxy.get_reports_event().await {
                Ok((s, e)) => (s, e),
                Err(e) => {
                    panic!("Failed to get reports event with error: {}.", e);
                }
            };

            loop {
                let _ = fasync::OnSignals::new(&event, zx::Signals::USER_0).await;

                let input_reports: Vec<fidl_fuchsia_input_report::InputReport> =
                    match device_proxy.get_reports().await {
                        Ok(reports) => reports,
                        Err(e) => {
                            panic!("Failed to get reports with error: {}", e);
                        }
                    };

                for report in input_reports {
                    channel_endpoint.try_send(report).unwrap();
                }
            }
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
