// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::input_device::InputDeviceBinding,
    async_trait::async_trait,
    failure::{self, format_err, Error},
    fidl_fuchsia_input_report as fidl,
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport},
    fuchsia_async as fasync,
    futures::channel::mpsc::{Receiver, Sender},
    futures::StreamExt,
};

#[derive(Copy, Clone)]
pub struct TouchInputMessage {}

#[derive(Copy, Clone)]
pub struct TouchDescriptor {}

/// A [`TouchBinding`] represents a connection to a touch input device.
///
/// The [`TouchBinding`] parses and exposes touch descriptor properties (e.g., the range of
/// possible x values for touch contacts) for the device it is associated with.
/// It also parses [`InputReport`]s from the device, and sends them to clients
/// via [`TouchBinding::input_message_stream()`].
///
/// # Example
/// ```
/// let mut touch_device: TouchBinding = input_device::InputDeviceBinding::new().await?;
///
/// while let Some(report) = touch_device.input_message_stream().next().await {}
/// ```
pub struct TouchBinding {
    /// The channel to stream InputReports to
    message_sender: Sender<input_device::InputMessage>,

    message_receiver: Receiver<input_device::InputMessage>,

    descriptor: TouchDescriptor,
}

/// A [`ContactDescriptor`] describes the possible values touch contact properties can take on.
///
/// This descriptor can be used, for example, to determine where on a screen a touch made contact.
///
/// # Example
///
/// ```
/// // Determine the scaling factor between the display and the touch device's x range.
/// let scaling_factor =
///     display_width / (contact_descriptor._x_range.end - contact_descriptor._x_range.start);
/// // Use the scaling factor to scale the contact report's x position.
/// let hit_location = scaling_factor * contact_report.position_x;
/// ```
#[derive(Copy, Clone)]
pub struct ContactDescriptor {
    /// The range of possible x values for this touch contact.
    _x_range: fidl::Range,

    /// The range of possible y values for this touch contact.
    _y_range: fidl::Range,

    /// The range of possible pressure values for this touch contact.
    _pressure_range: Option<fidl::Range>,

    /// The range of possible widths for this touch contact.
    _width_range: Option<fidl::Range>,

    /// The range of possible heights for this touch contact.
    _height_range: Option<fidl::Range>,
}

#[async_trait]
impl input_device::InputDeviceBinding for TouchBinding {
    fn input_message_sender(&self) -> Sender<input_device::InputMessage> {
        self.message_sender.clone()
    }

    fn input_message_stream(&mut self) -> &mut Receiver<input_device::InputMessage> {
        return &mut self.message_receiver;
    }

    fn get_descriptor(&self) -> input_device::InputDescriptor {
        input_device::InputDescriptor::Touch(self.descriptor)
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
        devices.pop().ok_or(format_err!("Couldn't find a default touch device."))
    }

    async fn all_devices() -> Result<Vec<InputDeviceProxy>, Error> {
        input_device::all_devices(input_device::InputDeviceType::Touch).await
    }

    async fn bind_device(device: &InputDeviceProxy) -> Result<Self, Error> {
        match device.get_descriptor().await?.touch {
            Some(fidl_fuchsia_input_report::TouchDescriptor {
                contacts: Some(_contacts),
                max_contacts: _,
                touch_type: _,
            }) => {
                let (message_sender, message_receiver) =
                    futures::channel::mpsc::channel(input_device::INPUT_MESSAGE_BUFFER_SIZE);

                Ok(TouchBinding {
                    message_sender,
                    message_receiver,
                    descriptor: TouchDescriptor {
                        // _contacts: contacts
                        //     .iter()
                        //     .map(TouchBinding::parse_contact_descriptor)
                        //     .filter_map(Result::ok)
                        //     .collect(),
                    },
                })
            }
            descriptor => Err(format_err!("Touch Descriptor failed to parse: \n {:?}", descriptor)),
        }
    }
}

impl TouchBinding {
    /// Parses a FIDL contact descriptor into a [`ContactDescriptor`]
    ///
    /// # Parameters
    /// - `contact_descriptor`: The contact descriptor to parse.
    ///
    /// # Errors
    /// If the contact descripto fails to parse because required fields aren't present.
    #[allow(dead_code)]
    fn parse_contact_descriptor(
        contact_descriptor: &fidl::ContactDescriptor,
    ) -> Result<ContactDescriptor, Error> {
        match contact_descriptor {
            fidl::ContactDescriptor {
                position_x: Some(x_axis),
                position_y: Some(y_axis),
                pressure: pressure_axis,
                contact_width: width_axis,
                contact_height: height_axis,
            } => Ok(ContactDescriptor {
                _x_range: x_axis.range,
                _y_range: y_axis.range,
                _pressure_range: Some(pressure_axis.unwrap().range), // Unwrap may be unsafe here
                _width_range: Some(width_axis.unwrap().range),       // Unwrap may be unsafe here
                _height_range: Some(height_axis.unwrap().range),     // Unwrap may be unsafe here
            }),
            descriptor => {
                Err(format_err!("Touch Contact Descriptor failed to parse: \n {:?}", descriptor))
            }
        }
    }
}

/// Returns a vector of [`TouchBindings`] for all currently connected touch devices.
///
/// # Errors
/// If there was an error binding to any touch device.
async fn all_touch_bindings() -> Result<Vec<TouchBinding>, Error> {
    let device_proxies = input_device::all_devices(input_device::InputDeviceType::Touch).await?;
    let mut device_bindings: Vec<TouchBinding> = vec![];

    for device_proxy in device_proxies {
        let device_binding: TouchBinding =
            input_device::InputDeviceBinding::new(device_proxy).await?;
        device_bindings.push(device_binding);
    }

    Ok(device_bindings)
}

/// Returns a stream of InputMessages from all touch devices.
///
/// # Errors
/// If there was an error binding to any touch device.
pub async fn all_touch_messages() -> Result<Receiver<input_device::InputMessage>, Error> {
    let bindings = all_touch_bindings().await?;
    let (message_sender, message_receiver) =
        futures::channel::mpsc::channel(input_device::INPUT_MESSAGE_BUFFER_SIZE);

    for mut touch in bindings {
        let mut sender = message_sender.clone();
        fasync::spawn(async move {
            while let Some(report) = touch.input_message_stream().next().await {
                let _ = sender.try_send(report);
            }
        });
    }

    Ok(message_receiver)
}
