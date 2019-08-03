// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;
use std::{fs, io};

use byteorder::{ByteOrder, LittleEndian};
use failure::{self, bail, Error, ResultExt};
use fidl_fuchsia_hardware_input::{
    DeviceMarker as SensorMarker, DeviceProxy as SensorProxy, ReportType,
};
use fuchsia_syslog::fx_log_info;

/// Unique signature for the light sensor
const HID_SENSOR_DESCRIPTOR: [u8; 4] = [5, 32, 9, 65];

#[derive(Debug)]
pub struct AmbientLightInputRpt {
    pub rpt_id: u8,
    pub state: u8,
    pub event: u8,
    pub illuminance: u16,
    pub red: u16,
    pub green: u16,
    pub blue: u16,
}

/// Opens the sensor's device file.
/// Tries all the input devices until the one with the correct signature is found.
pub async fn open_sensor() -> Result<SensorProxy, Error> {
    let input_devices_directory = "/dev/class/input";
    let path = Path::new(input_devices_directory);
    let entries = fs::read_dir(path)?;
    for entry in entries {
        let entry = entry?;
        let device = open_input_device(entry.path().to_str().expect("Bad path"))?;
        if let Ok(device_descriptor) = device.get_report_desc().await {
            if device_descriptor.len() < 4 {
                bail!("Short HID header");
            }
            let device_header = &device_descriptor[0..4];
            if device_header == HID_SENSOR_DESCRIPTOR {
                return Ok(device);
            }
        }
    }
    Err(io::Error::new(io::ErrorKind::NotFound, "no sensor found").into())
}

/// Reads the sensor's HID record and decodes it.
pub async fn read_sensor(sensor: &SensorProxy) -> Result<AmbientLightInputRpt, Error> {
    let report = sensor.get_report(ReportType::Input, 1).await?;
    let report = report.1;
    if report.len() < 11 {
        bail!("Sensor HID report too short");
    }
    Ok(AmbientLightInputRpt {
        rpt_id: report[0],
        state: report[1],
        event: report[2],
        illuminance: LittleEndian::read_u16(&report[3..5]),
        red: LittleEndian::read_u16(&report[5..7]),
        green: LittleEndian::read_u16(&report[7..9]),
        blue: LittleEndian::read_u16(&report[9..11]),
    })
}

fn open_input_device(path: &str) -> Result<SensorProxy, Error> {
    fx_log_info!("Opening sensor at {:?}", path);
    let (proxy, server) =
        fidl::endpoints::create_proxy::<SensorMarker>().context("Failed to create sensor proxy")?;
    fdio::service_connect(path, server.into_channel())
        .context("Failed to connect built-in service")?;
    Ok(proxy)
}
