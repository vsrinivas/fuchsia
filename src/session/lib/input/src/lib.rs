// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{self, format_err, Error, ResultExt},
    fdio,
    fidl_fuchsia_input_report::InputDeviceMarker,
    std::fs::{read_dir, DirEntry, ReadDir},
    std::io,
    std::path::{Path, PathBuf},
};

/// The path to the input-report directory.
static INPUT_REPORT_PATH: &str = "/dev/class/input-report";

/// The input device types supported by this library.
#[derive(PartialEq, Copy, Clone)]
pub enum InputDeviceType {
    Mouse,
    Touch,
}

/// Returns a proxy to the first available mouse input device.
///
/// # Errors
/// If there is an error reading the directory, or no mouse input device is found.
pub async fn get_mouse_input_device() -> Result<fidl_fuchsia_input_report::InputDeviceProxy, Error>
{
    get_device(INPUT_REPORT_PATH, InputDeviceType::Mouse).await
}

/// Returns a proxy to the first available touch input device.
///
/// # Errors
/// If there is an error reading the directory, or no touch input device is found.
pub async fn get_touch_input_device() -> Result<fidl_fuchsia_input_report::InputDeviceProxy, Error>
{
    get_device(INPUT_REPORT_PATH, InputDeviceType::Touch).await
}

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
    }
}

/// Returns a proxy to the InputDevice in `entry` if it exists.
///
/// # Parameters
/// - `entry`: The directory entry that contains an InputDevice.
///
/// # Errors
/// If there is an error connecting to the InputDevice in `entry`.
fn get_device_from_dir_entry(
    entry: io::Result<DirEntry>,
) -> Result<fidl_fuchsia_input_report::InputDeviceProxy, Error> {
    let entry_path: PathBuf = entry?.path();
    let input_device_path: Option<&str> = entry_path.to_str();
    if input_device_path.is_none() {
        return Err(format_err!("Failed to get entry path as a string."));
    }

    let (input_device, server) = fidl::endpoints::create_proxy::<InputDeviceMarker>()?;
    fdio::service_connect(input_device_path.unwrap(), server.into_channel())?;
    Ok(input_device)
}

/// Returns a proxy to the first InputDevice of type `device_type` in `input_report_path`.
///
/// # Parameters
/// - `input_report_path`: The path to the device's InputReports.
/// - `device_type`: The type of device to get.
///
/// # Errors
/// If there is an error reading `input_report_path`, or no input device of type `device_type`
/// is found.
async fn get_device(
    input_report_path: &str,
    device_type: InputDeviceType,
) -> Result<fidl_fuchsia_input_report::InputDeviceProxy, Error> {
    let input_report_dir = Path::new(input_report_path);
    let entries: ReadDir = read_dir(input_report_dir)
        .with_context(|_| format!("Failed to read {}", input_report_path))?;

    for entry in entries {
        if let Ok(input_device) = get_device_from_dir_entry(entry) {
            if is_device_type(&input_device, device_type).await {
                return Ok(input_device);
            }
        }
    }

    Err(format_err!("No input devices found."))
}

#[cfg(test)]
mod tests {
    use {
        super::{is_device_type, InputDeviceType},
        fidl::endpoints::create_proxy_and_stream,
        fuchsia_async as fasync,
        futures::prelude::*,
    };

    /// Spawns a local `fidl_fuchsia_input_report::InputDevice` server, and returns a proxy to the
    /// spawned server.
    /// The provided `request_handler` is notified when an incoming request is received.
    ///
    /// # Parameters
    /// - `request_handler`: A function which is called with incoming requests to the spawned
    ///                      `InputDevice` server.
    /// # Returns
    /// A `InputDeviceProxy` to the spawned server.
    fn spawn_input_device_server<F: 'static>(
        request_handler: F,
    ) -> fidl_fuchsia_input_report::InputDeviceProxy
    where
        F: Fn(fidl_fuchsia_input_report::InputDeviceRequest) + Send,
    {
        let (input_device_proxy, mut input_device_server) =
            create_proxy_and_stream::<fidl_fuchsia_input_report::InputDeviceMarker>()
                .expect("Failed to create InputDevice proxy and server.");

        fasync::spawn(async move {
            while let Some(input_device_request) = input_device_server.try_next().await.unwrap() {
                request_handler(input_device_request);
            }
        });

        input_device_proxy
    }

    // Tests that is_device_type() returns true for InputDeviceType::Mouse when a mouse exists.
    #[fasync::run_singlethreaded(test)]
    async fn mouse_input_device_exists() {
        let input_device_proxy =
            spawn_input_device_server(move |input_device_request| match input_device_request {
                fidl_fuchsia_input_report::InputDeviceRequest::GetDescriptor { responder } => {
                    let _ = responder.send(fidl_fuchsia_input_report::DeviceDescriptor {
                        device_info: None,
                        mouse: Some(fidl_fuchsia_input_report::MouseDescriptor {
                            movement_x: None,
                            movement_y: None,
                            scroll_v: None,
                            scroll_h: None,
                            buttons: None,
                        }),
                        sensor: None,
                        touch: None,
                        keyboard: None,
                    });
                }
                _ => {
                    assert!(false);
                }
            });

        assert!(is_device_type(&input_device_proxy, InputDeviceType::Mouse).await);
    }

    // Tests that is_device_type() returns true for InputDeviceType::Mouse when a mouse doesn't
    // exist.
    #[fasync::run_singlethreaded(test)]
    async fn mouse_input_device_doesnt_exists() {
        let input_device_proxy =
            spawn_input_device_server(move |input_device_request| match input_device_request {
                fidl_fuchsia_input_report::InputDeviceRequest::GetDescriptor { responder } => {
                    let _ = responder.send(fidl_fuchsia_input_report::DeviceDescriptor {
                        device_info: None,
                        mouse: None,
                        sensor: None,
                        touch: None,
                        keyboard: None,
                    });
                }
                _ => {
                    assert!(false);
                }
            });

        assert!(!is_device_type(&input_device_proxy, InputDeviceType::Mouse).await);
    }

    // Tests that is_device_type() returns true for InputDeviceType::Touch when a touchscreen
    // exists.
    #[fasync::run_singlethreaded(test)]
    async fn touch_input_device_exists() {
        let input_device_proxy =
            spawn_input_device_server(move |input_device_request| match input_device_request {
                fidl_fuchsia_input_report::InputDeviceRequest::GetDescriptor { responder } => {
                    let _ = responder.send(fidl_fuchsia_input_report::DeviceDescriptor {
                        device_info: None,
                        mouse: None,
                        sensor: None,
                        touch: Some(fidl_fuchsia_input_report::TouchDescriptor {
                            contacts: None,
                            max_contacts: None,
                            touch_type: None,
                        }),
                        keyboard: None,
                    });
                }
                _ => {
                    assert!(false);
                }
            });

        assert!(is_device_type(&input_device_proxy, InputDeviceType::Touch).await);
    }

    // Tests that is_device_type() returns true for InputDeviceType::Touch when a touchscreen
    // exists.
    #[fasync::run_singlethreaded(test)]
    async fn touch_input_device_doesnt_exist() {
        let input_device_proxy =
            spawn_input_device_server(move |input_device_request| match input_device_request {
                fidl_fuchsia_input_report::InputDeviceRequest::GetDescriptor { responder } => {
                    let _ = responder.send(fidl_fuchsia_input_report::DeviceDescriptor {
                        device_info: None,
                        mouse: None,
                        sensor: None,
                        touch: None,
                        keyboard: None,
                    });
                }
                _ => {
                    assert!(false);
                }
            });

        assert!(!is_device_type(&input_device_proxy, InputDeviceType::Touch).await);
    }

    // Tests that is_device_type() returns true for every input device type that exists.
    #[fasync::run_singlethreaded(test)]
    async fn no_input_device_match() {
        let input_device_proxy =
            spawn_input_device_server(move |input_device_request| match input_device_request {
                fidl_fuchsia_input_report::InputDeviceRequest::GetDescriptor { responder } => {
                    let _ = responder.send(fidl_fuchsia_input_report::DeviceDescriptor {
                        device_info: None,
                        mouse: Some(fidl_fuchsia_input_report::MouseDescriptor {
                            movement_x: None,
                            movement_y: None,
                            scroll_v: None,
                            scroll_h: None,
                            buttons: None,
                        }),
                        sensor: None,
                        touch: Some(fidl_fuchsia_input_report::TouchDescriptor {
                            contacts: None,
                            max_contacts: None,
                            touch_type: None,
                        }),
                        keyboard: None,
                    });
                }
                _ => {
                    assert!(false);
                }
            });

        assert!(is_device_type(&input_device_proxy, InputDeviceType::Mouse).await);
        assert!(is_device_type(&input_device_proxy, InputDeviceType::Touch).await);
    }
}
