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
    async fn any_input_device() -> Result<InputDeviceProxy, Error>;

    /// Retrieves a list of proxies to all devices of type `Self`.
    ///
    /// # Errors
    /// If no devices of the correct type exist.
    async fn all_devices() -> Result<Vec<InputDeviceProxy>, Error>;

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
    fn input_report_stream(&mut self) -> &mut Receiver<fidl_fuchsia_input_report::InputReport>;

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
        let device_proxy: InputDeviceProxy = Self::any_input_device().await?;
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

/// Returns true if the device type of `input_device` matches `device_type`.
///
/// # Parameters
/// - `input_device`: The InputDevice to check the type of.
/// - `device_type`: The type of the device to compare to.
pub async fn is_device_type(
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

/// Returns all the devices of the given device type.
///
/// # Parameters
/// - `device_type`: The type of devices to return.
///
/// # Errors
/// If the input device directory cannot be read.
pub async fn all_devices(
    device_type: InputDeviceType,
) -> Result<Vec<fidl_fuchsia_input_report::InputDeviceProxy>, Error> {
    let input_report_dir = Path::new(INPUT_REPORT_PATH);
    let entries: ReadDir = read_dir(input_report_dir)
        .with_context(|_| format!("Failed to read input report directory {}", INPUT_REPORT_PATH))?;

    let mut devices: Vec<fidl_fuchsia_input_report::InputDeviceProxy> = vec![];
    for entry in entries.filter_map(Result::ok) {
        let entry_path = entry.path();
        if let Ok(input_device) = get_device_from_dir_entry_path(&entry_path) {
            if is_device_type(&input_device, device_type).await {
                devices.push(input_device);
            }
        }
    }

    Ok(devices)
}

#[derive(Clone, Copy)]
pub enum InputDeviceType {
    Keyboard,
    Mouse,
    Touch,
}

/// The path to the input-report directory.
static INPUT_REPORT_PATH: &str = "/dev/class/input-report";

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

#[cfg(test)]
mod tests {
    use {
        super::*, fidl::endpoints::create_proxy_and_stream, fuchsia_async as fasync,
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
    async fn mouse_input_device_doesnt_exist() {
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
