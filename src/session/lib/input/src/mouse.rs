// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::conversion_utils::to_range,
    crate::input_device,
    async_trait::async_trait,
    failure::{self, format_err, Error},
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport, MouseDescriptor},
    futures::channel::mpsc::Sender,
    std::ops::Range,
};

/// Returns a proxy to the first available mouse input device.
///
/// # Errors
/// If there is an error reading the input directory, or no mouse input device is found.
pub async fn get_mouse_input_device() -> Result<InputDeviceProxy, Error> {
    input_device::get_device(input_device::InputDeviceType::Mouse).await
}

/// A [`MouseBinding`] represents a connection to a mouse input device.
///
/// The [`MouseBinding`] parses and exposes mouse descriptor properties (e.g., the range of
/// possible x values) for the device it is associated with. It also parses [`InputReport`]s
/// from the device, and sends them to clients via [`MouseBinding::report_stream`].
///
/// # Example
/// ```
/// let (sender, mouse_report_receiver) = futures::channel::mpsc::channel(1);
/// let mouse_device: MouseBinding = input_device::InputDeviceBinding::new(sender)).await?;
///
/// while let Some(report) = mouse_report_receiver.next().await {}
/// ```
pub struct MouseBinding {
    /// The sender used to send available input reports.
    report_sender: Sender<InputReport>,

    /// The range of possible x values for this device.
    _x_range: Range<i64>,

    /// The range of possible y values for this device.
    _y_range: Range<i64>,

    /// The range of possible horizontal scroll values possible for this device, if the device
    /// has horizontal scroll.
    _horizontal_scroll_range: Option<Range<i64>>,

    /// The range of possible vertical scroll values possible for this device, if the device
    /// has vertical scroll.
    _vertical_scroll_range: Option<Range<i64>>,

    /// The buttons that are currently pressed on the mouse.
    /// TODO(vickiecheng): Change u8 to button phases.
    _buttons: Vec<u8>,
}

#[async_trait]
impl input_device::InputDeviceBinding for MouseBinding {
    async fn new(report_stream: Sender<InputReport>) -> Result<Self, Error> {
        let device_proxy: InputDeviceProxy = get_mouse_input_device().await?;

        let mouse = MouseBinding::create_mouse_binding(&device_proxy, report_stream).await?;
        mouse.listen_for_reports(device_proxy);

        Ok(mouse)
    }

    fn get_report_stream(&self) -> Sender<fidl_fuchsia_input_report::InputReport> {
        self.report_sender.clone()
    }
}

impl MouseBinding {
    /// Creates a [`MouseBinding`] from an [`InputDeviceProxy`]
    ///
    /// # Parameters
    /// - `mouse_device`: An input device associated with a mouse device.
    /// - `report_sender`: The sender to which the [`MouseBinding`] will send input reports.
    ///
    /// # Errors
    /// If the `mouse_device` does not represent a mouse, or the parsing of the device's descriptor
    /// fails.
    async fn create_mouse_binding(
        mouse_device: &InputDeviceProxy,
        report_sender: Sender<fidl_fuchsia_input_report::InputReport>,
    ) -> Result<MouseBinding, Error> {
        match mouse_device.get_descriptor().await?.mouse {
            Some(MouseDescriptor {
                movement_x: Some(x_axis),
                movement_y: Some(y_axis),
                scroll_v: vertical_scroll_range,
                scroll_h: horizontal_scroll_range,
                buttons,
            }) => Ok(MouseBinding {
                report_sender,
                _x_range: to_range(x_axis),
                _y_range: to_range(y_axis),
                _horizontal_scroll_range: horizontal_scroll_range.map(to_range),
                _vertical_scroll_range: vertical_scroll_range.map(to_range),
                _buttons: buttons.unwrap_or_default(),
            }),
            descriptor => Err(format_err!("Mouse Descriptor failed to parse: \n {:?}", descriptor)),
        }
    }
}
