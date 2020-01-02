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
use fuchsia_async::{Time, TimeoutExt};
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

/// The channel to use for the initial init packet.
const INIT_CHANNEL: u32 = 0xffffffff;

/// The number of bytes in an init nonce, as defined in the CTAP HID spec.
const NONCE_LENGTH: u16 = 8;

lazy_static! {
    /// Time to wait while a device is sending valid packets but ones the don't match our
    /// request before we declare a transaction failure. Set to a large value in case the device
    /// is in the middle of responding to a slow operation for a different client.
    static ref TRANSACTION_TIMEOUT: zx::Duration = zx::Duration::from_millis(2000);
}

bitfield! {
    /// A `BitField` of device capabilities as documented in the CTAP HID spec.
    pub struct Capabilities(u8);
    impl Debug;
    // LSB is set if the device supports "Wink", i.e. some visible or audible indication.
    pub wink, _: 0;
    // Bit 2 is set if the device supports CBOR messages.
    pub cbor, _: 2;
    // Bit 3 is set if the device does *not* support encapsulated CTAP1 messages.
    pub nmsg, _: 3;
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

/// The properties that we learn about a connection as we initialize it.
struct ConnectionProperties {
    /// The assigned CTAPHID channel to use for all packets.
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
        let report_descriptor =
            connection.report_descriptor().await.map_err(|err| format_err!("Error: {:?}", err))?;
        if &FIDO_REPORT_DESCRIPTOR[..] == &report_descriptor[..] {
            info!("Attempting first initialization on {:?}", path);
            let properties =
                Self::initialize_connection(&connection, &mut rng, INIT_CHANNEL).await?;
            let packet_length = connection.max_packet_length().await?;

            Ok(Some(Device {
                path,
                connection,
                rng,
                packet_length,
                channel: properties.channel,
                ctaphid_version: properties.ctaphid_version,
                capabilities: properties.capabilities,
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

    /// Instructs the device to wink, i.e. perform some vendor-defined visual or audible
    /// identification.
    pub async fn wink(&self) -> Result<(), Error> {
        let out =
            Packet::padded_initialization(self.channel, Command::Wink, &[], self.packet_length)?;
        self.connection
            .write_packet(out)
            .await
            .map_err(|err| format_err!("Error writing wink packet: {:?}", err))?;

        self.connection
            .read_matching_packet(|packet| {
                if packet.channel() != self.channel {
                    // Its normal for reponses to other clients to be present on other channels.
                    return Ok(false);
                } else if packet.command()? != Command::Wink {
                    return Err(format_err!("Received unexpected command in wink response."));
                }
                Ok(true)
            })
            .on_timeout(Time::after(*TRANSACTION_TIMEOUT), || {
                Err(format_err!("Timed out waiting for wink response"))
            })
            .await?;
        Ok(())
    }

    /// Performs CTAPHID initialization on the supplied connection using the supplied channel,
    /// returning values extracted from the init response packet if successful.
    async fn initialize_connection(
        connection: &C,
        rng: &mut R,
        init_channel: u32,
    ) -> Result<ConnectionProperties, Error> {
        let packet_length = connection.max_packet_length().await?;
        let nonce: [u8; NONCE_LENGTH as usize] = rng.gen();
        let init_packet =
            Packet::padded_initialization(init_channel, Command::Init, &nonce, packet_length)?;
        connection
            .write_packet(init_packet)
            .await
            .map_err(|err| format_err!("Error writing init packet: {:?}", err))?;

        let packet = connection
            .read_matching_packet(|packet| {
                if packet.channel() != init_channel {
                    // It's normal for reponses to other clients to be present on other channels.
                    return Ok(false);
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
                    // It's potentially possible to recieve another client's init response, whose
                    // nonce won't match the one we sent. Unlikely enough to log though.
                    info!("Received init packet with an unexpected nonce.");
                    return Ok(false);
                }
                Ok(true)
            })
            .on_timeout(Time::after(*TRANSACTION_TIMEOUT), || {
                Err(format_err!("Timed out waiting for valid init response"))
            })
            .await?;

        let mut payload = packet.payload().into_buf();
        // Skip the nonce we validated while matching the packet.
        payload.advance(NONCE_LENGTH as usize);
        let channel = payload.get_u32_be();
        let ctaphid_version = payload.get_u8();
        // Skip the 3 bytes of device-specific version information.
        payload.advance(3);
        let capabilities = Capabilities(payload.get_u8());
        Ok(ConnectionProperties { channel, ctaphid_version, capabilities })
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
    const BAD_CHANNEL: u32 = 0xffeeffee;
    /// The nonce that a StdRng seeded with zero should generate.
    const EXPECTED_NONCE: [u8; 8] = [0xe2, 0xcf, 0x59, 0x54, 0x7a, 0x32, 0xae, 0xef];
    const DIFFERENT_NONCE: [u8; 8] = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08];

    lazy_static! {
        static ref FIXED_SEED_RNG: StdRng = StdRng::seed_from_u64(0);
    }

    /// Builds an expected request packet for the initialization transaction.
    fn build_init_request(nonce: &[u8]) -> Packet {
        Packet::padded_initialization(INIT_CHANNEL, Command::Init, nonce, REPORT_LENGTH).unwrap()
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

    #[fasync::run_until_stalled(test)]
    async fn test_init_valid_device() -> Result<(), Error> {
        // Configure a fake connection to expect an init request and return a response.
        let con = FakeConnection::new(&FIDO_REPORT_DESCRIPTOR);
        con.expect_write(build_init_request(&EXPECTED_NONCE));
        con.expect_read(build_init_response(&EXPECTED_NONCE, TEST_CHANNEL, 0x04));

        // Create a device using this connection and a deterministic Rng.
        let dev = Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await?
            .expect("Failed to create device");

        assert_eq!(dev.report_descriptor().await?, &FIDO_REPORT_DESCRIPTOR[..]);
        assert_eq!(dev.path, TEST_PATH);
        assert_eq!(dev.channel, TEST_CHANNEL);
        assert_eq!(dev.capabilities.wink(), false); /* Capability=0x04 does not contain wink */
        assert_eq!(dev.capabilities.cbor(), true); /* Capability=0x04 does contain CBOR */
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_init_while_other_traffic_present() -> Result<(), Error> {
        // Configure a fake connection to expect an init request and return a response.
        let con = FakeConnection::new(&FIDO_REPORT_DESCRIPTOR);
        con.expect_write(build_init_request(&EXPECTED_NONCE));
        // Return a valid response to a different command on a different channel. The code under
        // test should ignore this and attempt another read.
        con.expect_read(
            Packet::padded_initialization(BAD_CHANNEL, Command::Wink, &[], REPORT_LENGTH).unwrap(),
        );
        // Return a valid init packet with a different nonce, possibly in response to a different
        // client that is also going through init. The code under test should ignore this and
        // attempt another read.
        con.expect_read(build_init_response(&DIFFERENT_NONCE, 0x99999999, 0x04));
        // Return the valid init packet in response to our request.
        con.expect_read(build_init_response(&EXPECTED_NONCE, TEST_CHANNEL, 0x04));

        // Attempt to create a device using this connection and a deterministic Rng.
        let dev = Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await?
            .expect("Failed to create device");

        assert_eq!(dev.report_descriptor().await?, &FIDO_REPORT_DESCRIPTOR[..]);
        assert_eq!(dev.path, TEST_PATH);
        assert_eq!(dev.channel, TEST_CHANNEL);
        assert_eq!(dev.capabilities.wink(), false); /* Capability=0x04 does not contain wink */
        assert_eq!(dev.capabilities.cbor(), true); /* Capability=0x04 does contain CBOR */
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_init_non_fido_device() -> Result<(), Error> {
        let con = FakeConnection::new(&BAD_REPORT_DESCRIPTOR);
        assert!(Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await?
            .is_none());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_init_fidl_error() -> Result<(), Error> {
        let mut con = FakeConnection::new(&BAD_REPORT_DESCRIPTOR);
        con.error();
        Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await
            .expect_err("Should have failed to create device");
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_wink() -> Result<(), Error> {
        // Configure a fake connection and expect standard initialization.
        let con = FakeConnection::new(&FIDO_REPORT_DESCRIPTOR);
        con.expect_write(build_init_request(&EXPECTED_NONCE));
        con.expect_read(build_init_response(&EXPECTED_NONCE, TEST_CHANNEL, 0x01));
        // Expect a wink request but return a packet on a different channel before success.
        con.expect_write(
            Packet::padded_initialization(TEST_CHANNEL, Command::Wink, &[], REPORT_LENGTH).unwrap(),
        );
        con.expect_read(
            Packet::padded_initialization(BAD_CHANNEL, Command::Wink, &[], REPORT_LENGTH).unwrap(),
        );
        con.expect_read(
            Packet::padded_initialization(TEST_CHANNEL, Command::Wink, &[], REPORT_LENGTH).unwrap(),
        );
        // Now expect a second wink request and return an invalid message on the correct channel.
        con.expect_write(
            Packet::padded_initialization(TEST_CHANNEL, Command::Wink, &[], REPORT_LENGTH).unwrap(),
        );
        con.expect_read(
            Packet::padded_initialization(TEST_CHANNEL, Command::Init, &[], REPORT_LENGTH).unwrap(),
        );

        // Attempt to create a device and resquest two winks, one successful one not.
        let dev = Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await?
            .expect("Failed to create device");
        assert!(dev.wink().await.is_ok());
        assert!(dev.wink().await.is_err());
        Ok(())
    }
}
