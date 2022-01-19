// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;
use std::{fs, io};

use anyhow::{format_err, Context as _, Error};
use async_trait::async_trait;
use fidl_fuchsia_input_report::{
    DeviceDescriptor, InputDeviceMarker, InputDeviceProxy, InputReportsReaderMarker,
    InputReportsReaderProxy, SensorInputDescriptor, SensorType,
};
use fuchsia_syslog::fx_log_info;

#[derive(Debug)]
pub struct AmbientLightInputRpt {
    pub illuminance: f32,
    pub red: f32,
    pub green: f32,
    pub blue: f32,
}

struct AmbientLightComponent {
    pub report_index: usize,
    pub exponent: i32,
    pub report_id: u8, // report ID associated with descriptor
}

struct AmbientLightInputReportReaderProxy {
    pub proxy: InputReportsReaderProxy,

    pub illuminance: Option<AmbientLightComponent>,
    pub red: Option<AmbientLightComponent>,
    pub green: Option<AmbientLightComponent>,
    pub blue: Option<AmbientLightComponent>,
}

fn open_input_report_device(path: &str) -> Result<InputDeviceProxy, Error> {
    fx_log_info!("Opening sensor at {:?}", path);
    let (proxy, server) = fidl::endpoints::create_proxy::<InputDeviceMarker>()
        .context("Failed to create sensor proxy")?;
    fdio::service_connect(path, server.into_channel())
        .context("Failed to connect built-in service")?;
    Ok(proxy)
}

async fn open_sensor_input_report_reader<'a>() -> Result<AmbientLightInputReportReaderProxy, Error>
{
    let input_report_directory = "/dev/class/input-report";
    let dir_path = Path::new(input_report_directory);
    let entries = fs::read_dir(dir_path)?;
    for entry in entries {
        let entry = entry?;
        let device_path = entry.path();
        let device_path = device_path.to_str().expect("Bad path");
        let device = open_input_report_device(device_path)?;

        let get_sensor_input =
            |descriptor: &'a DeviceDescriptor| -> Result<&'a Vec<SensorInputDescriptor>, Error> {
                let sensor = descriptor.sensor.as_ref().context("device has no sensor")?;
                let input_desc = sensor.input.as_ref().context("sensor has no input descriptor")?;
                Ok(input_desc)
            };

        if let Ok(descriptor) = device.get_descriptor().await {
            match get_sensor_input(&descriptor) {
                Ok(input_desc) => {
                    let mut illuminance = None;
                    let mut red = None;
                    let mut green = None;
                    let mut blue = None;

                    for input in input_desc {
                        match &input.values {
                            Some(axes) => {
                                for (i, val) in axes.iter().enumerate() {
                                    let component = AmbientLightComponent {
                                        report_index: i,
                                        exponent: val.axis.unit.exponent,
                                        report_id: input.report_id.unwrap_or(0),
                                    };
                                    match val.type_ {
                                        SensorType::LightIlluminance => {
                                            illuminance = Some(component)
                                        }
                                        SensorType::LightRed => red = Some(component),
                                        SensorType::LightGreen => green = Some(component),
                                        SensorType::LightBlue => blue = Some(component),
                                        _ => {}
                                    }
                                }
                            }
                            _ => {}
                        }
                    }

                    if illuminance.is_some() {
                        let (proxy, server_end) =
                            fidl::endpoints::create_proxy::<InputReportsReaderMarker>()?;
                        if let Ok(()) = device.get_input_reports_reader(server_end) {
                            return Ok(AmbientLightInputReportReaderProxy {
                                proxy: proxy,
                                illuminance,
                                red,
                                blue,
                                green,
                            });
                        }
                    }
                }
                Err(e) => {
                    fx_log_info!("Skip device {}: {}", device_path, e);
                }
            };
        }
    }
    Err(io::Error::new(io::ErrorKind::NotFound, "no sensor found").into())
}

/// Reads the sensor's input report and decodes it.
async fn read_sensor_input_report(
    device: &AmbientLightInputReportReaderProxy,
) -> Result<Option<AmbientLightInputRpt>, Error> {
    let r = device.proxy.read_input_reports().await;

    match r {
        Ok(Ok(reports)) => {
            for report in reports {
                if report.report_id.unwrap_or(0) != device.illuminance.as_ref().unwrap().report_id {
                    continue;
                }

                if let Some(sensor) = report.sensor {
                    if let Some(values) = sensor.values {
                        let f = |component: &Option<AmbientLightComponent>| match component {
                            Some(val) => match val.exponent {
                                0 => values[val.report_index] as f32,
                                _ => {
                                    values[val.report_index] as f32
                                        * f32::powf(10.0, val.exponent as f32)
                                }
                            },
                            None => 0.0,
                        };

                        let illuminance = f(&device.illuminance);
                        let red = f(&device.red);
                        let green = f(&device.green);
                        let blue = f(&device.blue);

                        return Ok(Some(AmbientLightInputRpt { illuminance, red, blue, green }));
                    }
                }
            }
            Ok(None)
        }
        Ok(Err(e)) => Err(format_err!("ReadInputReports error: {}", e)),
        Err(e) => Err(format_err!("FIDL call failed: {}", e)),
    }
}

/// TODO(lingxueluo) Default and temporary report when sensor is not valid(fxbug.dev/42782).
fn default_report() -> Result<Option<AmbientLightInputRpt>, Error> {
    Ok(Some(AmbientLightInputRpt { illuminance: 200.0, red: 200.0, green: 200.0, blue: 200.0 }))
}

pub struct Sensor {
    proxy: Option<AmbientLightInputReportReaderProxy>,
}

impl Sensor {
    pub async fn new() -> Sensor {
        let proxy = open_sensor_input_report_reader().await;
        match proxy {
            Ok(proxy) => return Sensor { proxy: Some(proxy) },
            Err(_e) => {
                println!("No valid sensor found.");
                return Sensor { proxy: None };
            }
        }
    }

    async fn read(&self) -> Result<Option<AmbientLightInputRpt>, Error> {
        if self.proxy.is_none() {
            default_report()
        } else {
            read_sensor_input_report(self.proxy.as_ref().unwrap()).await
        }
    }
}

#[async_trait]
pub trait SensorControl: Send {
    async fn read(&self) -> Result<Option<AmbientLightInputRpt>, Error>;
}

#[async_trait]
impl SensorControl for Sensor {
    async fn read(&self) -> Result<Option<AmbientLightInputRpt>, Error> {
        self.read().await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;

    #[fasync::run_singlethreaded(test)]
    async fn test_open_sensor_error() {
        let sensor = Sensor { proxy: None };
        if let Some(ambient_light_input_rpt) = sensor.read().await.unwrap() {
            assert_eq!(ambient_light_input_rpt.illuminance, 200.0);
            assert_eq!(ambient_light_input_rpt.red, 200.0);
            assert_eq!(ambient_light_input_rpt.green, 200.0);
            assert_eq!(ambient_light_input_rpt.blue, 200.0);
        }
    }
}
