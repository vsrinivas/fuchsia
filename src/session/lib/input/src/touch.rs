// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::conversion_utils::to_range,
    crate::input_device,
    async_trait::async_trait,
    failure::{self, format_err, Error},
    fidl_fuchsia_input_report as fidl,
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport, TouchDescriptor},
    futures::channel::mpsc::{Receiver, Sender},
    std::ops::Range,
};

/// A [`TouchBinding`] represents a connection to a touch input device.
///
/// The [`TouchBinding`] parses and exposes touch descriptor properties (e.g., the range of
/// possible x values for touch contacts) for the device it is associated with.
/// It also parses [`InputReport`]s from the device, and sends them to clients
/// via [`TouchBinding::report_stream`].
///
/// # Example
/// ```
/// let mut touch_device: TouchBinding = input_device::InputDeviceBinding::new().await?;
///
/// while let Some(report) = touch_device.input_reports().next().await {}
/// ```
pub struct TouchBinding {
    /// The channel to stream InputReports to
    report_sender: Sender<InputReport>,

    report_receiver: Receiver<InputReport>,

    /// The descriptors for each touch contact.
    _contacts: Vec<ContactDescriptor>,
}

/// A [`ContactDescriptor`] describes the possible values touch contact properties can take on.
///
/// This descriptor can be used, for example, to determine where on a screen a touch made contact.
///
/// # Example
///
/// ```
/// // Determine the scaling factor between the display and the touch device's x range.
/// let scaling_factor = display_width / (contact_descriptor._x_range.end - contact_descriptor._x_range.start);
/// // Use the scaling factor to scale the contact report's x position.
/// let hit_location = scaling_factor * contact_report.position_x;
/// ```
pub struct ContactDescriptor {
    /// The range of possible x values for this touch contact.
    _x_range: Range<i64>,

    /// The range of possible y values for this touch contact.
    _y_range: Range<i64>,

    /// The range of possible pressure values for this touch contact.
    _pressure_range: Option<Range<i64>>,

    /// The range of possible widths for this touch contact.
    _width_range: Option<Range<i64>>,

    /// The range of possible heights for this touch contact.
    _height_range: Option<Range<i64>>,
}

#[async_trait]
impl input_device::InputDeviceBinding for TouchBinding {
    fn input_report_sender(&self) -> Sender<InputReport> {
        self.report_sender.clone()
    }

    fn input_reports(&mut self) -> &mut Receiver<fidl_fuchsia_input_report::InputReport> {
        return &mut self.report_receiver;
    }

    async fn default_input_device() -> Result<InputDeviceProxy, Error> {
        input_device::get_device(input_device::InputDeviceType::Touch).await
    }

    async fn bind_device(device: &InputDeviceProxy) -> Result<Self, Error> {
        match device.get_descriptor().await?.touch {
            Some(TouchDescriptor { contacts: Some(contacts), max_contacts: _, touch_type: _ }) => {
                let (report_sender, report_receiver) = futures::channel::mpsc::channel(1);
                Ok(TouchBinding {
                    report_sender: report_sender,
                    report_receiver: report_receiver,
                    _contacts: contacts
                        .iter()
                        .map(TouchBinding::parse_contact_descriptor)
                        .filter_map(Result::ok)
                        .collect(),
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
                _x_range: to_range(*x_axis),
                _y_range: to_range(*y_axis),
                _pressure_range: pressure_axis.map(to_range),
                _width_range: width_axis.map(to_range),
                _height_range: height_axis.map(to_range),
            }),
            descriptor => {
                Err(format_err!("Touch Contact Descriptor failed to parse: \n {:?}", descriptor))
            }
        }
    }
}
