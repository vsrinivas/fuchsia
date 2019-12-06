// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{self, format_err, Error, ResultExt},
    fdio,
    fidl_fuchsia_input_report::InputDeviceMarker,
    std::fs::{read_dir, ReadDir},
    std::path::{Path, PathBuf},
};

/// The path to the input-report directory.
static INPUT_REPORT_PATH: &str = "/dev/class/input-report";

/// The input device types supported by this library.
#[derive(PartialEq, Copy, Clone, Debug)]
pub enum InputDeviceType {
    Mouse,
    Touch,
    Keyboard,
}

/// Holds the InputDevice client interface along with some metadata about the device.
/// get_input_devices() loops through all entries in the INPUT_REPORT_PATH, and adds
/// |InputDeviceWithType| objects to the result vector. If a service reports itself as more than one
/// type, an |InputDeviceWithType| is created for each type.
pub struct InputDeviceWithType {
    /// client-side handle to the input device
    pub device: fidl_fuchsia_input_report::InputDeviceProxy,

    /// The service directory path (for logging/debug purposes)
    pub report_path: String,

    /// The enum value for the type of device, as determined from its DeviceDescriptor
    pub device_type: InputDeviceType,
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

/// Returns a proxy to the first available keyboard input device.
///
/// # Errors
/// If there is an error reading the directory, or no keyboard input device is found.
pub async fn get_keyboard_input_device(
) -> Result<fidl_fuchsia_input_report::InputDeviceProxy, Error> {
    get_device(INPUT_REPORT_PATH, InputDeviceType::Keyboard).await
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

/// Loops through the service directory for all known input devices and returns a vector of
/// InputDeviceWithType objects for each device and device type. If a device reports more than one
/// type, an InputDeviceWithType object is added for each type.
///
/// # Errors
/// Returns an Err if no input devices are found, rather than failing silently by returning an
/// empty vector.
pub async fn get_input_devices() -> Result<Vec<InputDeviceWithType>, Error> {
    let mut inputs = vec![];
    let input_report_dir = Path::new(INPUT_REPORT_PATH);
    let entries: ReadDir = read_dir(input_report_dir)
        .with_context(|_| format!("Failed to read {}", INPUT_REPORT_PATH))?;

    // TODO(richkadel - No bug ID because I plan to address this in a subsequent change, while
    // or after merging in vickiecheng@'s changes.
    //
    // Suggestion from lindkvist@
    //
    // Consider extracting helper methods, and using something like:
    // ```
    //   let input_devices = entries.map(|entry| {
    //     get_device_type(entry)
    //   }).filter(|optional_type| optional_type.is_some()).collect();
    //   input_devices
    // ```

    for entry in entries {
        let entry_path: PathBuf = entry?.path();
        if let Ok(input_device) = get_device_from_dir_entry_path(&entry_path) {
            if let Ok(descriptor) = input_device.get_descriptor().await {
                let report_path = entry_path.to_str().unwrap_or("");
                if descriptor.mouse.is_some() {
                    inputs.push(InputDeviceWithType {
                        // I will clean this up while responding to Kevin's comments, before
                        // submitting. For consistency, each if block gets a new device channel
                        // client. In some cases the descriptor matches more than one type
                        // (mouse and touch, for instance) and until we have a better approach,
                        // I return a device client handle for each.
                        device: get_device_from_dir_entry_path(&entry_path).unwrap(), // validated above
                        report_path: report_path.to_string(),
                        device_type: InputDeviceType::Mouse,
                    });
                }
                if descriptor.touch.is_some() {
                    inputs.push(InputDeviceWithType {
                        device: get_device_from_dir_entry_path(&entry_path).unwrap(), // validated above
                        report_path: report_path.to_string(),
                        device_type: InputDeviceType::Touch,
                    });
                }
                if descriptor.keyboard.is_some() {
                    inputs.push(InputDeviceWithType {
                        device: get_device_from_dir_entry_path(&entry_path).unwrap(), // validated above
                        report_path: report_path.to_string(),
                        device_type: InputDeviceType::Keyboard,
                    });
                }
                if descriptor.sensor.is_some() {
                    // Not yet supported
                }
                // If none matched, this could be reported as an error
                //     return Err(format_err!("Unhandled device type!"));
            }
        }
    }

    if inputs.len() == 0 {
        // As a precaution, return an error so the caller has to handle the unexpected possibility
        // that there are no devices. While it's possible to get no devices back, it would be
        // unusual for applications to call this method expecting that possibility. So the likely
        // reason for getting no devices would be due to a device, system, or library bug or
        // misconfiguration.
        //
        // Returning an Ok result with an empty vector just allows the application to "fail
        // silently". Instead, we return an Err result so the caller is immediatly forced to
        // handle the situation.
        Err(format_err!("No input devices found."))
    } else {
        Ok(inputs)
    }
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

    // Tests that is_device_type() returns true for InputDeviceType::Keyboard when a keyboard
    // exists.
    #[fasync::run_singlethreaded(test)]
    async fn keyboard_input_device_exists() {
        let input_device_proxy =
            spawn_input_device_server(move |input_device_request| match input_device_request {
                fidl_fuchsia_input_report::InputDeviceRequest::GetDescriptor { responder } => {
                    let _ = responder.send(fidl_fuchsia_input_report::DeviceDescriptor {
                        device_info: None,
                        mouse: None,
                        sensor: None,
                        touch: None,
                        keyboard: Some(fidl_fuchsia_input_report::KeyboardDescriptor {
                            keys: None,
                        }),
                    });
                }
                _ => {
                    assert!(false);
                }
            });

        assert!(is_device_type(&input_device_proxy, InputDeviceType::Keyboard).await);
    }

    // Tests that is_device_type() returns true for InputDeviceType::Keyboard when a keyboard
    // exists.
    #[fasync::run_singlethreaded(test)]
    async fn keyboard_input_device_doesnt_exist() {
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

        assert!(!is_device_type(&input_device_proxy, InputDeviceType::Keyboard).await);
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
                        keyboard: Some(fidl_fuchsia_input_report::KeyboardDescriptor {
                            keys: None,
                        }),
                    });
                }
                _ => {
                    assert!(false);
                }
            });

        assert!(is_device_type(&input_device_proxy, InputDeviceType::Mouse).await);
        assert!(is_device_type(&input_device_proxy, InputDeviceType::Touch).await);
        assert!(is_device_type(&input_device_proxy, InputDeviceType::Keyboard).await);
    }
}
