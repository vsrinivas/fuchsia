// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::conversion_utils::to_range,
    crate::input_device,
    crate::input_device::InputDeviceBinding,
    async_trait::async_trait,
    failure::{self, format_err, Error},
    fidl_fuchsia_input_report::{InputDeviceProxy, InputReport, MouseDescriptor},
    fuchsia_async as fasync,
    futures::channel::mpsc::{Receiver, Sender},
    futures::StreamExt,
    std::ops::Range,
};

/// A [`MouseBinding`] represents a connection to a mouse input device.
///
/// The [`MouseBinding`] parses and exposes mouse descriptor properties (e.g., the range of
/// possible x values) for the device it is associated with. It also parses [`InputReport`]s
/// from the device, and sends them to clients via [`MouseBinding::input_report_stream()`].
///
/// # Example
/// ```
/// let mut mouse_device: MouseBinding = input_device::InputDeviceBinding::new().await?;
///
/// while let Some(report) = mouse_device.input_report_stream().next().await {}
/// ```
pub struct MouseBinding {
    /// The sender used to send available input reports.
    report_sender: Sender<InputReport>,

    /// The receiving end of the input report channel. Clients use this indirectly via
    /// [`input_report_stream()`].
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

    fn input_report_stream(&mut self) -> &mut Receiver<fidl_fuchsia_input_report::InputReport> {
        return &mut self.report_receiver;
    }

    async fn any_input_device() -> Result<InputDeviceProxy, Error> {
        let mut devices = Self::all_devices().await?;
        devices.pop().ok_or(format_err!("Couldn't find a default mouse."))
    }

    async fn all_devices() -> Result<Vec<InputDeviceProxy>, Error> {
        input_device::all_devices(input_device::InputDeviceType::Mouse).await
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

/// Returns a vector of [`MouseBindings`] for all currently connected mice.
///
/// # Errors
/// If there was an error binding to any mouse.
async fn get_all_mouse_bindings() -> Result<Vec<MouseBinding>, Error> {
    let device_proxies = input_device::all_devices(input_device::InputDeviceType::Mouse).await?;
    let mut device_bindings: Vec<MouseBinding> = vec![];

    for device_proxy in device_proxies {
        let device_binding: MouseBinding =
            input_device::InputDeviceBinding::new(device_proxy).await?;
        device_bindings.push(device_binding);
    }

    Ok(device_bindings)
}

/// Returns a stream of InputReports from all mouse devices.
///
/// # Errors
/// If there was an error binding to any mouse.
pub async fn all_mouse_reports() -> Result<Receiver<InputReport>, Error> {
    let bindings = get_all_mouse_bindings().await?;
    let (report_sender, report_receiver) = futures::channel::mpsc::channel(1);

    for mut mouse in bindings {
        let mut sender = report_sender.clone();
        fasync::spawn(async move {
            while let Some(report) = mouse.input_report_stream().next().await {
                let _ = sender.try_send(report);
            }
        });
    }

    Ok(report_receiver)
}
