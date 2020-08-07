// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copied from src/ui/bin/brightness_manager
// TODO(fxb/36843) consolidate usages

use crate::call;
use crate::display::light_sensor_config::LightSensorConfig;
use crate::service_context::{ExternalServiceProxy, ServiceContextHandle};

use std::path::Path;
use std::{fs, io};

use anyhow::{format_err, Error};
use fidl_fuchsia_input_report::{
    DeviceInfo, InputDeviceMarker, InputDeviceProxy, InputReport, InputReportsReaderMarker,
    InputReportsReaderProxy, SensorAxis, SensorType,
};

#[derive(Debug)]
pub struct AmbientLightInputRpt {
    pub rpt_id: u64,
    pub illuminance: i64,
    pub red: i64,
    pub green: i64,
    pub blue: i64,
}

/// Struct for managing reads to the light sensor
#[derive(Debug, Clone)]
pub struct Sensor {
    /// A reader proxy used to query the latest light sensor values.
    reader: ExternalServiceProxy<InputReportsReaderProxy>,

    /// This vector represents the order that the various axes will arrive in when being sent
    /// over IPC. Since the interface can be implemented by various drivers and devices, we need to
    /// keep track of the order so we can properly deserialize the response.
    sensor_axes: Vec<SensorAxis>,
}

impl Sensor {
    pub async fn new(
        proxy: &ExternalServiceProxy<InputDeviceProxy>,
        service_context: &ServiceContextHandle,
    ) -> Result<Self, Error> {
        let (reader, server) = fidl::endpoints::create_proxy::<InputReportsReaderMarker>()?;
        call!(proxy => get_input_reports_reader(server))?;
        let reader = service_context.lock().await.wrap_proxy(reader).await;

        let sensor_axes = proxy
            .call_async(InputDeviceProxy::get_descriptor)
            .await?
            .sensor
            .and_then(|sensor| sensor.input)
            .and_then(|input| input.values)
            .ok_or_else(|| format_err!("Missing sensor descriptors"))?;

        Ok(Self { reader, sensor_axes })
    }
}

/// Opens the sensor's device file.
/// Tries all the input devices until the one with the correct signature is found.
pub async fn open_sensor(
    service_context: ServiceContextHandle,
    config: LightSensorConfig,
) -> Result<Sensor, Error> {
    const INPUT_DEVICES_DIRECTORY: &str = "/dev/class/input-report";
    let path = Path::new(INPUT_DEVICES_DIRECTORY);
    let entries = fs::read_dir(path)?;
    for entry in entries {
        let path = entry?.path();
        let path = path.to_str().expect("Bad path");
        let proxy = service_context.lock().await.connect_path::<InputDeviceMarker>(path).await?;
        let res = proxy.call_async(InputDeviceProxy::get_descriptor).await;
        if let Ok(device_descriptor) = res {
            if let Some(DeviceInfo { vendor_id, product_id, .. }) = device_descriptor.device_info {
                let LightSensorConfig::VendorAndProduct { vendor_id: v, product_id: p } = config;
                if vendor_id == v && product_id == p {
                    return Sensor::new(&proxy, &service_context).await;
                }
            }
        }
    }

    Err(io::Error::new(io::ErrorKind::NotFound, "no sensor found").into())
}

async fn get_reports(sensor: &Sensor) -> Result<InputReport, Error> {
    let reports = sensor
        .reader
        .call_async(InputReportsReaderProxy::read_input_reports)
        .await?
        .map_err(|status| format_err!("Error reading reports: {}", status))?;
    reports.into_iter().last().ok_or_else(|| format_err!("Missing report"))
}

/// Reads the sensor's HID record and decodes it.
pub async fn read_sensor(sensor: &Sensor) -> Result<AmbientLightInputRpt, Error> {
    let report = get_reports(sensor).await?;

    let rpt_id = report.trace_id.ok_or_else(|| format_err!("Report missing trace_id"))?;
    let report = report.sensor.ok_or_else(|| format_err!("Report missing sensor"))?;
    let values = report.values.ok_or_else(|| format_err!("Report missing values"))?;
    let mut illuminance = None;
    let mut red = None;
    let mut green = None;
    let mut blue = None;
    for (sensor_axis, value) in sensor.sensor_axes.iter().zip(values.into_iter()) {
        match sensor_axis.type_ {
            SensorType::LightIlluminance => {
                illuminance = Some(value);
            }
            SensorType::LightRed => {
                red = Some(value);
            }
            SensorType::LightGreen => {
                green = Some(value);
            }
            SensorType::LightBlue => blue = Some(value),
            _ => {}
        }
    }

    if let (Some(illuminance), Some(red), Some(green), Some(blue)) = (illuminance, red, green, blue)
    {
        Ok(AmbientLightInputRpt { rpt_id, illuminance, red, green, blue })
    } else {
        Err(format_err!("Missing light data from sensor report"))
    }
}

#[cfg(test)]
pub mod testing {
    use fidl_fuchsia_input_report::{
        Axis, DeviceDescriptor, DeviceInfo, InputDeviceRequest, InputDeviceRequestStream,
        InputReport, InputReportsReaderReadInputReportsResponder, InputReportsReaderRequest, Range,
        SensorAxis, SensorDescriptor, SensorInputDescriptor, SensorInputReport, SensorType, Unit,
        UnitType, VendorGoogleProductId, VendorId,
    };
    use fuchsia_async as fasync;
    use futures::prelude::*;
    use std::future::Future;

    pub const TEST_LUX_VAL: i64 = 605;
    pub const TEST_RED_VAL: i64 = 345;
    pub const TEST_BLUE_VAL: i64 = 133;
    pub const TEST_GREEN_VAL: i64 = 164;

    pub fn get_mock_sensor_response(
    ) -> (Vec<SensorAxis>, impl Fn() -> Vec<InputReport> + Clone + Send + Sync + 'static) {
        // Axis copied from real data.
        let axis = Axis {
            range: Range { min: 0, max: 65535 },
            unit: Unit { type_: UnitType::None, exponent: 0 },
        };
        (
            vec![
                SensorAxis { axis, type_: SensorType::LightRed },
                SensorAxis { axis, type_: SensorType::LightIlluminance },
                SensorAxis { axis, type_: SensorType::LightBlue },
                SensorAxis { axis, type_: SensorType::LightGreen },
            ],
            || {
                vec![InputReport {
                    event_time: Some(65),
                    mouse: None,
                    trace_id: Some(45),
                    sensor: Some(SensorInputReport {
                        values: Some(vec![
                            TEST_RED_VAL,
                            TEST_LUX_VAL,
                            TEST_BLUE_VAL,
                            TEST_GREEN_VAL,
                        ]),
                    }),
                    touch: None,
                    keyboard: None,
                    consumer_control: None,
                }]
            },
        )
    }

    pub fn mock_descriptor_from_axes(axes: Vec<SensorAxis>) -> DeviceDescriptor {
        DeviceDescriptor {
            device_info: Some(DeviceInfo {
                vendor_id: VendorId::Google.into_primitive(),
                product_id: VendorGoogleProductId::AmsLightSensor.into_primitive(),
                version: 0,
            }),
            mouse: None,
            sensor: Some(SensorDescriptor {
                input: Some(SensorInputDescriptor { values: Some(axes) }),
                feature: None,
            }),
            touch: None,
            keyboard: None,
            consumer_control: None,
        }
    }

    pub fn spawn_mock_sensor_with_data<F>(
        stream: InputDeviceRequestStream,
        axes: Vec<SensorAxis>,
        data_fn: impl FnMut() -> F + Clone + Send + Sync + 'static,
    ) where
        F: Future<Output = Vec<InputReport>> + Send,
    {
        spawn_mock_sensor_with_handler(stream, axes, move |responder| {
            let mut data_fn = data_fn.clone();
            async move {
                let data = data_fn().await;
                responder.send(&mut Ok(data)).unwrap();
            }
        });
    }

    pub fn spawn_mock_sensor_with_handler<F>(
        mut stream: InputDeviceRequestStream,
        axes: Vec<SensorAxis>,
        handler: impl FnMut(InputReportsReaderReadInputReportsResponder) -> F
            + Clone
            + Send
            + Sync
            + 'static,
    ) where
        F: Future<Output = ()> + Send,
    {
        fasync::Task::spawn(async move {
            while let Some(request) = stream.try_next().await.unwrap() {
                match request {
                    InputDeviceRequest::GetInputReportsReader { reader, control_handle: _ } => {
                        let mut stream = reader.into_stream().unwrap();
                        let mut handler = handler.clone();
                        fasync::Task::spawn(async move {
                            while let Some(request) = stream.try_next().await.unwrap() {
                                let InputReportsReaderRequest::ReadInputReports { responder } =
                                    request;
                                handler(responder).await;
                            }
                        })
                        .detach();
                    }
                    InputDeviceRequest::GetDescriptor { responder } => {
                        responder.send(mock_descriptor_from_axes(axes.clone())).unwrap();
                    }
                    _ => {}
                }
            }
        })
        .detach();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::service_context::ServiceContext;
    use futures::future;
    use testing;

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_read_sensor() {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<InputDeviceMarker>().unwrap();
        let (axes, data_fn) = testing::get_mock_sensor_response();
        testing::spawn_mock_sensor_with_data(stream, axes, move || future::ready(data_fn()));
        let proxy = ExternalServiceProxy::new(proxy, None);
        let service_context = ServiceContext::create(None, None);
        let sensor = Sensor::new(&proxy, &service_context).await.unwrap();

        let result = read_sensor(&sensor).await;
        match result {
            Ok(input_rpt) => {
                assert_eq!(input_rpt.illuminance, testing::TEST_LUX_VAL);
                assert_eq!(input_rpt.red, testing::TEST_RED_VAL);
                assert_eq!(input_rpt.green, testing::TEST_GREEN_VAL);
                assert_eq!(input_rpt.blue, testing::TEST_BLUE_VAL);
            }
            Err(e) => {
                panic!("Sensor read failed: {:?}", e);
            }
        }
    }
}
