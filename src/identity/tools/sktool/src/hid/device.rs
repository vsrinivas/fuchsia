// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::CtapDevice;
use async_trait::async_trait;
use failure::{format_err, Error, ResultExt};
use fdio::service_connect;
use fidl::endpoints::create_proxy;
use fidl_fuchsia_hardware_input::{DeviceMarker, DeviceProxy};
use lazy_static::lazy_static;
use log::{info, warn};
use std::fs;
use std::path::PathBuf;

lazy_static! {
    /// The absolute path at which HID devices are exposed.
    static ref HID_PATH: PathBuf = PathBuf::from("/dev/class/input/");
}

/// Exact report descriptor for a Yubico 5 series security key (note the 0xF1DO near the start).
// TODO(jsankey): Use a library to parse report descriptors instead of hardcoding the expectation.
const FIDO_REPORT_DESCRIPTOR: [u8; 34] = [
    0x06, 0xd0, 0xf1, 0x09, 0x01, 0xa1, 0x01, 0x09, 0x20, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08,
    0x95, 0x40, 0x81, 0x02, 0x09, 0x21, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x40, 0x91,
    0x02, 0xc0,
];

/// An open connection to a valid FIDO HID USB CTAP device.
#[derive(Debug)]
pub struct HidCtapDevice {
    path: String,
    proxy: DeviceProxy,
    report_descriptor: Vec<u8>,
}

impl HidCtapDevice {
    /// Constructs a new `HidCtapDevice` by connecting to a device at the specified path.
    ///
    /// Returns Ok(None) for valid paths that do not represent a FIDO device, and Err(err) if
    /// any errors are encountered.
    pub async fn new(path: String) -> Result<Option<HidCtapDevice>, Error> {
        let (proxy, server) = create_proxy::<DeviceMarker>().context("Failed to create proxy")?;
        service_connect(&path, server.into_channel()).context("Failed to connect to device")?;
        Self::new_from_proxy(path, proxy).await
    }

    /// Constructs a new `HidCtapDevice` using the supplied path and `DeviceProxy`
    ///
    /// Returns Ok(None) for valid paths that do not represent a FIDO device, and Err(err) if
    /// any errors are encountered.
    async fn new_from_proxy(
        path: String,
        proxy: DeviceProxy,
    ) -> Result<Option<HidCtapDevice>, Error> {
        let report_descriptor =
            proxy.get_report_desc().await.map_err(|err| format_err!("FIDL error: {:?}", err))?;
        if &FIDO_REPORT_DESCRIPTOR[..] == &report_descriptor[..] {
            Ok(Some(HidCtapDevice { path, proxy, report_descriptor }))
        } else {
            Ok(None)
        }
    }

    /// Returns the raw report descriptor.
    pub fn report_descriptor(&self) -> &[u8] {
        &self.report_descriptor
    }

    /// Returns the maximum size of any input report.
    pub async fn max_input_report_size(&self) -> Result<u16, Error> {
        self.proxy
            .get_max_input_report_size()
            .await
            .map_err(|err| format_err!("FIDL error: {:?}", err))
    }
}

#[async_trait]
impl CtapDevice for HidCtapDevice {
    async fn devices() -> Result<Vec<Self>, Error> {
        let mut output = Vec::new();
        for entry in
            fs::read_dir(&*HID_PATH).map_err(|err| format_err!("Error reading path {:?}", err))?
        {
            // Note: Errors reading individual devices lead to log messages but not to failure of
            // the function.
            let path_buf = match entry {
                Ok(entry) => entry.path(),
                Err(err) => {
                    warn!("Error getting next path {:?}", err);
                    continue;
                }
            };
            let path = match path_buf.to_str() {
                Some(path) => path,
                None => {
                    warn!("Non unicode path");
                    continue;
                }
            };
            match HidCtapDevice::new(path.to_string()).await {
                Ok(Some(device)) => {
                    info!("Constructing valid CTAP device at {:?}", path);
                    output.push(device);
                }
                Ok(None) => info!("Skipping not CTAP device at {:?}", path),
                Err(err) => warn!("Error reading device {:?}", err),
            }
        }
        Ok(output)
    }

    fn path(&self) -> &str {
        &self.path
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::{create_proxy_and_stream, RequestStream};
    use fidl_fuchsia_hardware_input::DeviceRequest;
    use fuchsia_async as fasync;
    use futures::TryStreamExt;

    const TEST_PATH: &str = "/dev/test-device";
    const BAD_REPORT_DESCRIPTOR: [u8; 3] = [0xba, 0xdb, 0xad];
    const TEST_REPORT_SIZE: u16 = 99;

    /// Creates a fake device proxy that will respond with the report descriptor if supplied or
    /// immediately close the channel otherwise.
    fn fake_device_proxy(optional_report_descriptor: Option<&'static [u8]>) -> DeviceProxy {
        let (device_proxy, mut stream) =
            create_proxy_and_stream::<DeviceMarker>().expect("Failed to create proxy and stream");

        match optional_report_descriptor {
            None => stream.control_handle().shutdown(),
            Some(report_descriptor) => fasync::spawn(async move {
                while let Some(req) = stream.try_next().await.expect("Failed to read req") {
                    match req {
                        DeviceRequest::GetReportDesc { responder } => {
                            let response: Vec<u8> = report_descriptor.into();
                            responder
                                .send(&mut response.into_iter())
                                .expect("Failed to send response");
                        }
                        DeviceRequest::GetMaxInputReportSize { responder } => {
                            responder.send(TEST_REPORT_SIZE).expect("Failed to send response");
                        }
                        _ => panic!("Got unexpected device request."),
                    }
                }
            }),
        }
        device_proxy
    }

    #[fasync::run_until_stalled(test)]
    async fn test_valid_device() -> Result<(), Error> {
        let proxy = fake_device_proxy(Some(&FIDO_REPORT_DESCRIPTOR));
        let dev = HidCtapDevice::new_from_proxy(TEST_PATH.to_string(), proxy)
            .await?
            .expect("Failed to create device");
        assert_eq!(dev.report_descriptor(), &FIDO_REPORT_DESCRIPTOR[..]);
        assert_eq!(dev.path(), TEST_PATH);
        assert_eq!(dev.max_input_report_size().await?, TEST_REPORT_SIZE);
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_non_fido_device() -> Result<(), Error> {
        let proxy = fake_device_proxy(Some(&BAD_REPORT_DESCRIPTOR));
        assert!(HidCtapDevice::new_from_proxy(TEST_PATH.to_string(), proxy).await?.is_none());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_fidl_error() -> Result<(), Error> {
        let proxy = fake_device_proxy(None);
        HidCtapDevice::new_from_proxy(TEST_PATH.to_string(), proxy)
            .await
            .expect_err("Should have failed to create device");
        Ok(())
    }
}
