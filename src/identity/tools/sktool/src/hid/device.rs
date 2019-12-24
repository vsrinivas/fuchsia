// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::hid::connection::{Connection, FidlConnection};
use crate::hid::message::{Command, Packet};
use crate::CtapDevice;
use anyhow::{format_err, Context as _, Error};
use async_trait::async_trait;
use bitfield::bitfield;
use bytes::{Buf, Bytes, IntoBuf};
use fdio::service_connect;
use fidl::endpoints::create_proxy;
use fidl_fuchsia_hardware_input::DeviceMarker;
use fuchsia_async::{self as fasync, DurationExt};
use fuchsia_zircon as zx;
use lazy_static::lazy_static;
use log::{info, warn};
use rand::{rngs::OsRng, Rng};
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

/// The channel to use for the initial INIT packet.
const INIT_CHANNEL: u32 = 0xffffffff;

/// The number of bytes in an init nonce, as defined in the CTAP HID spec.
const NONCE_LENGTH: u16 = 8;

bitfield! {
    /// A `BitField` of device capabilities as documented in the CTAP HID spec.
    pub struct Capabilities(u8);
    impl Debug;
    // LSB is set if the device supports "Wink", i.e. some visible or audible indication.
    wink, _: 0;
    // Bit 2 is set if the device supports CBOR messages.
    cbor, _: 2;
    // Bit 3 is set if the device does *not* support encapsulated CTAP1 messages.
    nmsg, _: 3;
}

/// An representation of a valid FIDO CTAP HID device, backed by a connection used to communicate
/// directly with the device.
#[derive(Debug)]
pub struct Device<C: Connection, R: Rng> {
    /// The system path the device was installed from.
    path: String,
    /// A `Connection` used to communicate with the device.
    connection: C,
    /// A random number generator used for nonce generation.
    rng: R,
    /// The maximum length of outgoing packets.
    packet_length: u16,
    /// The CTAPHID channel to use for all packets.
    channel: u32,
    /// The version of the CTAPHID protocol used by the device.
    ctaphid_version: u8,
    /// The device's capabilities.
    capabilities: Capabilities,
}

impl<C: Connection, R: Rng> Device<C, R> {
    /// Constructs a new `Device` using the supplied path, `Connection`, and `Rng`
    ///
    /// Returns Ok(None) for valid connections that do not represent a FIDO device, and Err(err) if
    /// any errors are encountered.
    async fn new_from_connection(
        path: String,
        connection: C,
        mut rng: R,
    ) -> Result<Option<Device<C, R>>, Error> {
        // TODO(jsankey): Add a timeout in case the device stalls, on all calls.
        let report_descriptor =
            connection.report_descriptor().await.map_err(|err| format_err!("Error: {:?}", err))?;
        if &FIDO_REPORT_DESCRIPTOR[..] == &report_descriptor[..] {
            // TODO(jsankey): This logic is needed to acquire a channel during initialitation, but
            // its also eventually going to be needed to reinitialize a device that has reached
            // some bad state. Break it out into a separate method.
            let packet_length = connection.max_packet_length().await?;
            let nonce: [u8; NONCE_LENGTH as usize] = rng.gen();
            let init_packet =
                Packet::padded_initialization(INIT_CHANNEL, Command::Init, &nonce, packet_length)?;
            info!("Sending INIT packet to {:?}", path);
            connection
                .write_packet(init_packet)
                .await
                .map_err(|err| format_err!("Error writing init packet: {:?}", err))?;

            // TODO(jsankey): Replace sleep with an async wait inside connection.read_packet
            // using an event retrieved from `Device.GetReportsEvent`.
            let sleep_duration = zx::Duration::from_millis(250);
            fasync::Timer::new(sleep_duration.after_now()).await;
            let res = connection.read_packet().await;

            // TODO(jsankey): Currently this fails if the first packet we read from the connection
            // does not match our request, but if other clients are communicating with the device
            // this may occur. Continue polling until the desired channel, command and nonce are
            // received.
            let packet = res?;
            if packet.channel() != INIT_CHANNEL {
                return Err(format_err!("Received unexpected channel in init response."));
            } else if packet.command()? != Command::Init {
                return Err(format_err!("Received unexpected command in init response."));
            } else if packet.payload().len() < NONCE_LENGTH as usize + 9 {
                // Note: The 9 above = 4 bytes channel + 4 bytes version + 1 byte capability.
                return Err(format_err!(
                    "Received short init response ({:?} bytes).",
                    packet.payload().len()
                ));
            }

            let mut payload = packet.payload().into_buf();
            let mut received_nonce = [0u8; NONCE_LENGTH as usize];
            payload.copy_to_slice(&mut received_nonce);
            if received_nonce != nonce {
                return Err(format_err!("Received mismatched nonce in init response."));
            }
            let channel = payload.get_u32_be();
            let ctaphid_version = payload.get_u8();
            // Skip the 3 bytes of device-specific version information.
            payload.advance(3);
            let capabilities = Capabilities(payload.get_u8());

            Ok(Some(Device {
                path,
                connection,
                rng,
                packet_length,
                channel,
                ctaphid_version,
                capabilities,
            }))
        } else {
            Ok(None)
        }
    }

    /// Returns the raw report descriptor.
    pub async fn report_descriptor(&self) -> Result<Bytes, Error> {
        self.connection.report_descriptor().await
    }

    /// Returns the capabilities of the device.
    pub fn capabilities(&self) -> &Capabilities {
        &self.capabilities
    }
}

impl Device<FidlConnection, OsRng> {
    /// Constructs a new `Device` by connecting to a FIDL service at the specified path.
    ///
    /// Returns Ok(None) for valid paths that do not represent a FIDO device, and Err(err) if
    /// any errors are encountered.
    pub async fn new(path: String) -> Result<Option<Device<FidlConnection, OsRng>>, Error> {
        let (proxy, server) = create_proxy::<DeviceMarker>().context("Failed to create proxy")?;
        service_connect(&path, server.into_channel()).context("Failed to connect to device")?;
        Device::new_from_connection(path, FidlConnection::new(proxy), OsRng::new()?).await
    }
}

#[async_trait(?Send)]
impl CtapDevice for Device<FidlConnection, OsRng> {
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
                    info!(
                        "Constructing CTAP device at {:?} set to channel {:08x?}",
                        path, device.channel
                    );
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
    use crate::hid::connection::fake::{FakeConnection, REPORT_LENGTH};
    use bytes::BufMut;
    use fuchsia_async as fasync;
    use rand::{rngs::StdRng, SeedableRng};

    const TEST_PATH: &str = "/dev/test-device";
    const BAD_REPORT_DESCRIPTOR: [u8; 3] = [0xba, 0xdb, 0xad];
    const TEST_CHANNEL: u32 = 0x88776655;
    /// The nonce that a StdRng seeded with zero should generate.
    const EXPECTED_NONCE: [u8; 8] = [0xe2, 0xcf, 0x59, 0x54, 0x7a, 0x32, 0xae, 0xef];

    lazy_static! {
        static ref FIXED_SEED_RNG: StdRng = StdRng::seed_from_u64(0);
    }

    /// Builds a response packet for the initialization transaction.
    fn build_init_response(nonce: &[u8], channel: u32, capabilities: u8) -> Packet {
        let mut payload = Vec::from(nonce);
        payload.put_u32_be(channel);
        payload.put_u8(2); /* CTAPHID protocol */
        payload.put_u8(0xe1); /* Unused major version */
        payload.put_u8(0xe2); /* Unused minor version */
        payload.put_u8(0xe3); /* Unused build version */
        payload.put_u8(capabilities);
        Packet::padded_initialization(INIT_CHANNEL, Command::Init, &payload, REPORT_LENGTH).unwrap()
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_valid_device() -> Result<(), Error> {
        // Configure a fake connection to expect an init request and return a response.
        let con = FakeConnection::new(&FIDO_REPORT_DESCRIPTOR);
        con.expect_write(Packet::padded_initialization(
            INIT_CHANNEL,
            Command::Init,
            &EXPECTED_NONCE,
            REPORT_LENGTH,
        )?);
        con.expect_read(build_init_response(&EXPECTED_NONCE, TEST_CHANNEL, 0x04));

        // Create a device using this connection and a deterministic Rng.
        let dev = Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await?
            .expect("Failed to create device");

        assert_eq!(dev.report_descriptor().await?, &FIDO_REPORT_DESCRIPTOR[..]);
        assert_eq!(dev.path, TEST_PATH);
        assert_eq!(dev.channel, TEST_CHANNEL);
        assert_eq!(dev.capabilities.wink(), false); /* Capability=0x04 does not contain wink */
        assert_eq!(dev.capabilities.cbor(), true); /* Capability=0x04 dot contain CBOR */
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_mismatched_nonce_during_init() -> Result<(), Error> {
        // Configure a fake connection to expect an init request and return a response.
        let con = FakeConnection::new(&FIDO_REPORT_DESCRIPTOR);
        con.expect_write(Packet::padded_initialization(
            INIT_CHANNEL,
            Command::Init,
            &EXPECTED_NONCE,
            REPORT_LENGTH,
        )?);
        con.expect_read(build_init_response(&vec![1, 2, 3, 4, 5, 6, 7, 8], TEST_CHANNEL, 0x04));

        // Attempt to create a device using this connection and a deterministic Rng.
        Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await
            .expect_err("Should fail to create device with mismatched nonce");
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_non_fido_device() -> Result<(), Error> {
        let con = FakeConnection::new(&BAD_REPORT_DESCRIPTOR);
        assert!(Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await?
            .is_none());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_fidl_error() -> Result<(), Error> {
        let mut con = FakeConnection::new(&BAD_REPORT_DESCRIPTOR);
        con.error();
        Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await
            .expect_err("Should have failed to create device");
        Ok(())
    }
}
