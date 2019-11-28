// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Connection, FidlConnection};
use crate::CtapDevice;
use async_trait::async_trait;
use bytes::Bytes;
use failure::{format_err, Error, ResultExt};
use fdio::service_connect;
use fidl::endpoints::create_proxy;
use fidl_fuchsia_hardware_input::DeviceMarker;
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

/// An representation of a valid FIDO CTAP HID device, backed by a connection used to communicate
/// directly with the device.
#[derive(Debug)]
pub struct Device<C: Connection> {
    /// The system path the device was installed from.
    path: String,
    /// A `Connection` used to communicate with the device.
    connection: C,
}

impl<C: Connection> Device<C> {
    /// Constructs a new `Device` using the supplied path and `Connection`
    ///
    /// Returns Ok(None) for valid connections that do not represent a FIDO device, and Err(err) if
    /// any errors are encountered.
    async fn new_from_connection(path: String, connection: C) -> Result<Option<Device<C>>, Error> {
        let report_descriptor =
            connection.report_descriptor().await.map_err(|err| format_err!("Error: {:?}", err))?;
        if &FIDO_REPORT_DESCRIPTOR[..] == &report_descriptor[..] {
            Ok(Some(Device { path, connection }))
        } else {
            Ok(None)
        }
    }

    /// Returns the raw report descriptor.
    pub async fn report_descriptor(&self) -> Result<Bytes, Error> {
        self.connection.report_descriptor().await
    }
}

impl Device<FidlConnection> {
    /// Constructs a new `Device` by connecting to a FIDL service at the specified path.
    ///
    /// Returns Ok(None) for valid paths that do not represent a FIDO device, and Err(err) if
    /// any errors are encountered.
    pub async fn new(path: String) -> Result<Option<Device<FidlConnection>>, Error> {
        let (proxy, server) = create_proxy::<DeviceMarker>().context("Failed to create proxy")?;
        service_connect(&path, server.into_channel()).context("Failed to connect to device")?;
        Device::new_from_connection(path, FidlConnection::new(proxy)).await
    }
}

#[async_trait]
impl CtapDevice for Device<FidlConnection> {
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
            match Device::new(path.to_string()).await {
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
    use crate::hid::connection::fake::FakeConnection;
    use fuchsia_async as fasync;

    const TEST_PATH: &str = "/dev/test-device";
    const BAD_REPORT_DESCRIPTOR: [u8; 3] = [0xba, 0xdb, 0xad];

    #[fasync::run_until_stalled(test)]
    async fn test_valid_device() -> Result<(), Error> {
        let con = FakeConnection::new(&FIDO_REPORT_DESCRIPTOR);
        let dev = Device::new_from_connection(TEST_PATH.to_string(), con)
            .await?
            .expect("Failed to create device");
        assert_eq!(dev.report_descriptor().await?, &FIDO_REPORT_DESCRIPTOR[..]);
        assert_eq!(dev.path, TEST_PATH);
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_non_fido_device() -> Result<(), Error> {
        let con = FakeConnection::new(&BAD_REPORT_DESCRIPTOR);
        assert!(Device::new_from_connection(TEST_PATH.to_string(), con).await?.is_none());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_fidl_error() -> Result<(), Error> {
        let con = FakeConnection::failing();
        Device::new_from_connection(TEST_PATH.to_string(), con)
            .await
            .expect_err("Should have failed to create device");
        Ok(())
    }
}
