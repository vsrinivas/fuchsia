// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code, unused_variables)]

use super::types::Rgbc;
use crate::input_device::{self, Handled, InputDeviceBinding, InputDeviceDescriptor, InputEvent};
use anyhow::{format_err, Error};
use async_trait::async_trait;
use fidl_fuchsia_input_report::{InputDeviceProxy, InputReport, SensorDescriptor};
use fidl_fuchsia_ui_input_config::FeaturesRequest as InputConfigFeaturesRequest;
use fuchsia_syslog::fx_log_err;
use fuchsia_zircon as zx;
use futures::channel::mpsc::Sender;

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct LightSensorEvent {
    pub(crate) rgbc: Rgbc<u16>,
}

/// A [`LightSensorBinding`] represents a connection to a light sensor input device.
///
/// TODO more details
pub struct LightSensorBinding {
    /// The channel to stream InputEvents to.
    event_sender: Sender<input_device::InputEvent>,

    /// Holds information about this device.
    device_descriptor: LightSensorDeviceDescriptor,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct LightSensorDeviceDescriptor {
    /// The vendor id of the connected light sensor input device.
    pub vendor_id: u32,

    /// The product id of the connected light sensor input device.
    pub product_id: u32,

    /// The device id of the connected light sensor input device.
    pub device_id: u32,
}

#[async_trait]
impl InputDeviceBinding for LightSensorBinding {
    fn input_event_sender(&self) -> Sender<InputEvent> {
        self.event_sender.clone()
    }

    fn get_device_descriptor(&self) -> InputDeviceDescriptor {
        InputDeviceDescriptor::LightSensor(self.device_descriptor.clone())
    }

    async fn handle_input_config_request(
        &self,
        _request: &InputConfigFeaturesRequest,
    ) -> Result<(), Error> {
        Ok(())
    }
}

impl LightSensorBinding {
    /// Creates a new [`InputDeviceBinding`] from the `device_proxy`.
    ///
    /// The binding will start listening for input reports immediately and send new InputEvents
    /// to the device binding owner over `input_event_sender`.
    ///
    /// # Parameters
    /// - `device_proxy`: The proxy to bind the new [`InputDeviceBinding`] to.
    /// - `device_id`: The unique identifier of this device.
    /// - `input_event_sender`: The channel to send new InputEvents to.
    ///
    /// # Errors
    /// If there was an error binding to the proxy.
    pub async fn new(
        device_proxy: InputDeviceProxy,
        device_id: u32,
        input_event_sender: Sender<input_device::InputEvent>,
    ) -> Result<Self, Error> {
        let device_binding =
            Self::bind_device(&device_proxy, device_id, input_event_sender).await?;
        input_device::initialize_report_stream(
            device_proxy,
            device_binding.get_device_descriptor(),
            device_binding.input_event_sender(),
            Self::process_reports,
        );

        Ok(device_binding)
    }

    /// Binds the provided input device to a new instance of `LightSensorBinding`.
    ///
    /// # Parameters
    /// - `device`: The device to use to initialize the binding.
    /// - `device_id`: The device ID being bound.
    /// - `input_event_sender`: The channel to send new InputEvents to.
    ///
    /// # Errors
    /// If the device descriptor could not be retrieved, or the descriptor could not be parsed
    /// correctly.
    async fn bind_device(
        device: &InputDeviceProxy,
        device_id: u32,
        input_event_sender: Sender<input_device::InputEvent>,
    ) -> Result<Self, Error> {
        let descriptor = device.get_descriptor().await?;
        let device_info = descriptor.device_info.ok_or({
            // Logging in addition to returning an error, as in some test
            // setups the error may never be displayed to the user.
            fx_log_err!("DRIVER BUG: empty device_info for device_id: {}", device_id);
            format_err!("empty device info for device_id: {}", device_id)
        })?;
        match descriptor.sensor {
            Some(SensorDescriptor { input: Some(_), feature: Some(_), .. }) => {
                Ok(LightSensorBinding {
                    event_sender: input_event_sender,
                    device_descriptor: LightSensorDeviceDescriptor {
                        vendor_id: device_info.vendor_id,
                        product_id: device_info.product_id,
                        device_id,
                    },
                })
            }
            device_descriptor => Err(format_err!(
                "Light Sensor Device Descriptor failed to parse: \n {:?}",
                device_descriptor
            )),
        }
    }

    /// Parses an [`InputReport`] into one or more [`InputEvent`]s.
    ///
    /// The [`InputEvent`]s are sent to the device binding owner via [`input_event_sender`].
    ///
    /// # Parameters
    /// `report`: The incoming [`InputReport`].
    /// `previous_report`: The previous [`InputReport`] seen for the same device.
    /// `device_descriptor`: The descriptor for the input device generating the input reports.
    /// `input_event_sender`: The sender for the device binding's input event stream.
    ///
    /// # Returns
    /// An [`InputReport`] which will be passed to the next call to [`process_reports`], as
    /// [`previous_report`]. If `None`, the next call's [`previous_report`] will be `None`.
    fn process_reports(
        report: InputReport,
        previous_report: Option<InputReport>,
        device_descriptor: &input_device::InputDeviceDescriptor,
        input_event_sender: &mut Sender<input_device::InputEvent>,
    ) -> Option<InputReport> {
        let light_sensor_descriptor =
            if let input_device::InputDeviceDescriptor::LightSensor(ref light_sensor_descriptor) =
                device_descriptor
            {
                light_sensor_descriptor
            } else {
                unreachable!()
            };

        // Input devices can have multiple types so ensure `report` is a KeyboardInputReport.
        let sensor = match &report.sensor {
            None => return previous_report,
            Some(sensor) => sensor,
        };

        let values = match &sensor.values {
            None => return None,
            Some(values) => values,
        };

        let event_time: zx::Time = input_device::event_time_or_now(report.event_time);

        // TODO(fxbug.dev/100664) track layout in device registration, then update here.
        if let Err(e) = input_event_sender.try_send(input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::LightSensor(LightSensorEvent {
                rgbc: Rgbc { red: 0, green: 0, blue: 0, clear: 0 },
            }),
            device_descriptor: device_descriptor.clone(),
            event_time,
            handled: Handled::No,
            trace_id: None,
        }) {
            fx_log_err!("Failed to send LightSensorEvent with error: {e:?}");
        }

        Some(report)
    }
}
