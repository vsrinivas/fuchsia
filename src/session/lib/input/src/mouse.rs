// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::conversion_utils::to_range,
    crate::input_device,
    async_trait::async_trait,
    failure::{self, format_err, Error},
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport, MouseDescriptor},
    futures::channel::mpsc::{Receiver, Sender},
    std::ops::Range,
};

/// A [`MouseBinding`] represents a connection to a mouse input device.
///
/// The [`MouseBinding`] parses and exposes mouse descriptor properties (e.g., the range of
/// possible x values) for the device it is associated with. It also parses [`InputReport`]s
/// from the device, and sends them to clients via [`MouseBinding::report_stream`].
///
/// # Example
/// ```
/// let mut mouse_device: MouseBinding = input_device::InputDeviceBinding::new().await?;
///
/// while let Some(report) = mouse_device.input_reports().next().await {}
/// ```
pub struct MouseBinding {
    /// The sender used to send available input reports.
    report_sender: Sender<InputReport>,

    /// The receiving end of the input report channel. Clients use this indirectly via
    /// [`input_reports()`].
    report_receiver: Receiver<InputReport>,

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
    fn input_report_sender(&self) -> Sender<InputReport> {
        self.report_sender.clone()
    }

    fn input_reports(&mut self) -> &mut Receiver<fidl_fuchsia_input_report::InputReport> {
        return &mut self.report_receiver;
    }

    async fn default_input_device() -> Result<InputDeviceProxy, Error> {
        input_device::get_device(input_device::InputDeviceType::Mouse).await
    }

    async fn bind_device(device: &InputDeviceProxy) -> Result<Self, Error> {
        match device.get_descriptor().await?.mouse {
            Some(MouseDescriptor {
                movement_x: Some(x_axis),
                movement_y: Some(y_axis),
                scroll_v: vertical_scroll_range,
                scroll_h: horizontal_scroll_range,
                buttons,
            }) => {
                let (report_sender, report_receiver) = futures::channel::mpsc::channel(1);
                Ok(MouseBinding {
                    report_sender,
                    report_receiver,
                    _x_range: to_range(x_axis),
                    _y_range: to_range(y_axis),
                    _horizontal_scroll_range: horizontal_scroll_range.map(to_range),
                    _vertical_scroll_range: vertical_scroll_range.map(to_range),
                    _buttons: buttons.unwrap_or_default(),
                })
            }
            descriptor => Err(format_err!("Mouse Descriptor failed to parse: \n {:?}", descriptor)),
        }
    }
}
