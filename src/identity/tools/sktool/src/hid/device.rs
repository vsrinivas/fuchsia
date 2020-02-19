// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::hid::command::Command;
use crate::hid::connection::{Connection, FidlConnection};
use crate::hid::message::{BuilderStatus, Message, MessageBuilder};
use crate::hid::packet::Packet;
use crate::CtapDevice;
use anyhow::{format_err, Context as _, Error};
use async_trait::async_trait;
use bitfield::bitfield;
use bytes::{Buf, BufMut, Bytes, BytesMut};
use fdio::service_connect;
use fidl::endpoints::create_proxy;
use fidl_fuchsia_hardware_input::DeviceMarker;
use fuchsia_async::{Time, TimeoutExt};
use fuchsia_zircon as zx;
use futures::lock::Mutex;
use futures::TryFutureExt;
use lazy_static::lazy_static;
use log::{info, warn};
use rand::{rngs::OsRng, Rng};
use std::convert::TryFrom;
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
    /// The rng is mutex-wrapped to faciliate connection error handling without a mutable reference
    /// to the device. A lock is held on this mutex during device operations that should not be
    /// overlapped.
    rng: Mutex<R>,
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
                rng: Mutex::new(rng),
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
        self.perform_transaction(Command::Wink, &[]).await?;
        Ok(())
    }

    /// Performs a ping on the device, i.e. send a payload of the specified length and ensures the
    /// same payload is received in response. The payload is composed of incrementing 16 bit
    /// numbers and so length must be an even number.
    pub async fn ping(&self, length: u16) -> Result<(), Error> {
        if length % 2 != 0 {
            return Err(format_err!("Ping length must be even"));
        }
        let mut request = BytesMut::with_capacity(length as usize);
        for num in 1..=length / 2 {
            request.put_u16(num);
        }
        let response = self.perform_transaction(Command::Ping, &request).await?;
        if response == request {
            Ok(())
        } else {
            Err(format_err!(
                "Ping request did not match response: {:02x?} vs {:02x?}",
                request.bytes(),
                response.bytes()
            ))
        }
    }

    /// Performs CTAPHID initialization on the supplied connection using the supplied channel,
    /// returning values extracted from the init response packet if successful.
    ///
    /// Callers must ensure that no other operations are conducted on the connection while this
    /// function is in progress.
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

                let mut payload = packet.payload().clone();
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

        let mut payload = packet.payload().clone();
        // Skip the nonce we validated while matching the packet.
        payload.advance(NONCE_LENGTH as usize);
        let channel = payload.get_u32();
        let ctaphid_version = payload.get_u8();
        // Skip the 3 bytes of device-specific version information.
        payload.advance(3);
        let capabilities = Capabilities(payload.get_u8());
        Ok(ConnectionProperties { channel, ctaphid_version, capabilities })
    }

    /// Builds and sends a CTAPHID message over the connection.
    ///
    /// Callers must ensure that no other operations are conducted on the connection while this
    /// function is in progress.
    async fn send_message(&self, command: Command, payload: &[u8]) -> Result<(), Error> {
        let request = Message::new(self.channel, command, payload, self.packet_length)?;
        for request_packet in request {
            self.connection
                .write_packet(request_packet)
                .await
                .map_err(|err| format_err!("Error writing transaction packet: {:?}", err))?;
        }
        Ok(())
    }

    /// Receives a complete CTAPHID message over the connection. This method will not timeout and
    /// will return any errors immediately without trying to correct the state of the connection.
    ///
    /// Callers must ensure that no other operations are conducted on the connection while this
    /// function is in progress.
    async fn receive_message(&self) -> Result<Message, Error> {
        let mut response_builder = MessageBuilder::new();
        loop {
            let response_packet = self
                .connection
                .read_matching_packet(|packet| {
                    if packet.channel() != self.channel {
                        // It's normal to see responses to other clients on other channels.
                        return Ok(false);
                    } else if packet.is_command(Command::Keepalive) {
                        // It's normal to receive a keepalive while the device is thinking.
                        return Ok(false);
                    }
                    Ok(true)
                })
                .await?;
            if response_builder.append(response_packet)? == BuilderStatus::Complete {
                return Message::try_from(response_builder);
            }
        }
    }

    /// Attempts to cancel a pending CTAPHID transaction by sending a cancellation packet and
    /// swallowing the resulting error packet.
    ///
    /// Callers must ensure that no other operations are conducted on the connection while this
    /// function is in progress.
    async fn cancel_transaction(&self) -> Result<(), Error> {
        let cancel_packet =
            Packet::padded_initialization(self.channel, Command::Cancel, &[], self.packet_length)?;
        self.connection
            .write_packet(cancel_packet)
            .await
            .map_err(|err| format_err!("Error writing cancellation packet: {:?}", err))?;
        // If there was a transaction in progress when we sent the cancellation packet it will
        // fail with an error packet. We wait for and consume any remaining valid packets and the
        // error packet so the next transaction doesn't have to deal with them.
        //
        // If the transaction completed at exactly the same time as we decided to cancel it, no
        // transaction is in progress and no error packet will be received. In this rare case we
        // consume any remaining valid packets and continue after TRANSACTION_TIMEOUT.
        self.connection
            .read_matching_packet(|packet| {
                Ok(packet.channel() == self.channel && packet.is_command(Command::Error))
            })
            .map_ok(|_| ())
            .on_timeout(Time::after(*TRANSACTION_TIMEOUT), || Ok(()))
            .await?;
        Ok(())
    }

    /// Performs a CTAPHID transaction, sending the supplied request payload and returning the
    /// response payload from the device on success.
    ///
    /// Reading the response will timeout after waiting for a maximum of TRANSACTION_TIMEOUT, the
    /// method will attempt to correct connection state if errors are encountered, which may take
    /// up to one additional TRANSACTION_TIMEOUT.
    async fn perform_transaction(&self, command: Command, payload: &[u8]) -> Result<Bytes, Error> {
        // We already need our rng wrapped in a Mutex so it can generate the nonce of a
        // reinitialization packet if an error occurs. To prevent overlapping transactions on the
        // same connection we hold the lock on this rng mutex throughout the transaction.
        let mut rng_lock = self.rng.lock().await;
        self.send_message(command, payload).await?;
        let receive_result = self
            .receive_message()
            .map_ok(|message| Some(message))
            .on_timeout(Time::after(*TRANSACTION_TIMEOUT), || Ok(None))
            .await;
        match receive_result {
            Err(err) => {
                // If a transaction fails, reinitialize the state of the connection.
                warn!("Reinitializing connection following a transaction failure");
                if let Err(new_err) =
                    Self::initialize_connection(&self.connection, &mut rng_lock, self.channel).await
                {
                    warn!(
                        "Error reinitializing connection following a transaction failure {:?}",
                        new_err
                    );
                }
                warn!("Completed reinitialization following a transaction failure");
                // But still return the original error.
                Err(err)
            }
            Ok(None) => {
                // If a transaction timed out, cancel it on the connection.
                warn!("Cancelling pending transaction following a transaction timeout");
                self.cancel_transaction().await?;
                Err(format_err!("Timed out waiting for response to {:?} transaction", command))
            }
            Ok(Some(message)) => {
                if message.command() == command {
                    Ok(message.payload())
                } else if message.command() == Command::Error {
                    Err(format_err!(
                        "Received CTAPHID error {:?} in response to {:?} transaction",
                        message.payload().bytes(),
                        command
                    ))
                } else {
                    Err(format_err!(
                        "Received unexpected command {:?} in response to {:?} transaction",
                        message.command(),
                        command
                    ))
                }
            }
        }
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
    /// The first two nonces that a StdRng seeded with zero should generate.
    const FIRST_NONCE: [u8; 8] = [0xe2, 0xcf, 0x59, 0x54, 0x7a, 0x32, 0xae, 0xef];
    const SECOND_NONCE: [u8; 8] = [0xfa, 0x5c, 0x4d, 0xed, 0x87, 0x82, 0xb9, 0xad];
    const DIFFERENT_NONCE: [u8; 8] = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08];

    /// A small payload we use in test errors.
    const ERROR_PAYLOAD: [u8; 1] = [0xcc];

    lazy_static! {
        static ref FIXED_SEED_RNG: StdRng = StdRng::seed_from_u64(0);
        static ref TRANSACTION_REQUEST: Message =
            Message::new(TEST_CHANNEL, Command::Msg, &[0x11, 100], REPORT_LENGTH).unwrap();
        static ref TRANSACTION_RESPONSE: Message =
            Message::new(TEST_CHANNEL, Command::Msg, &[0x22, 100], REPORT_LENGTH).unwrap();
    }

    /// Builds an expected request packet for an initialization transaction sent on channel.
    fn build_init_request(nonce: &[u8], channel: u32) -> Packet {
        Packet::padded_initialization(channel, Command::Init, nonce, REPORT_LENGTH).unwrap()
    }

    /// Builds a response packet for an initialization transaction.
    fn build_init_response(
        nonce: &[u8],
        send_channel: u32,
        response_channel: u32,
        capabilities: u8,
    ) -> Packet {
        let mut payload = Vec::from(nonce);
        payload.put_u32(response_channel);
        payload.put_u8(2); /* CTAPHID protocol */
        payload.put_u8(0xe1); /* Unused major version */
        payload.put_u8(0xe2); /* Unused minor version */
        payload.put_u8(0xe3); /* Unused build version */
        payload.put_u8(capabilities);
        Packet::padded_initialization(send_channel, Command::Init, &payload, REPORT_LENGTH).unwrap()
    }

    /// Builds an simple padded initialization packet sent on channel.
    fn build_packet(channel: u32, command: Command, payload: &[u8]) -> Packet {
        Packet::padded_initialization(channel, command, payload, REPORT_LENGTH).unwrap()
    }

    #[fasync::run_until_stalled(test)]
    async fn new_from_connection_valid_device() -> Result<(), Error> {
        // Configure a fake connection to expect an init request and return a response.
        let con = FakeConnection::new(&FIDO_REPORT_DESCRIPTOR);
        con.expect_write(build_init_request(&FIRST_NONCE, INIT_CHANNEL));
        con.expect_read(build_init_response(&FIRST_NONCE, INIT_CHANNEL, TEST_CHANNEL, 0x04));

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
    async fn new_from_connection_while_other_traffic_present() -> Result<(), Error> {
        // Configure a fake connection to expect an init request and return a response.
        let con = FakeConnection::new(&FIDO_REPORT_DESCRIPTOR);
        con.expect_write(build_init_request(&FIRST_NONCE, INIT_CHANNEL));
        // Return a valid response to a different command on a different channel. The code under
        // test should ignore this and attempt another read.
        con.expect_read(build_packet(BAD_CHANNEL, Command::Wink, &[]));
        // Return a valid init packet with a different nonce, possibly in response to a different
        // client that is also going through init. The code under test should ignore this and
        // attempt another read.
        con.expect_read(build_init_response(&DIFFERENT_NONCE, INIT_CHANNEL, 0x99999999, 0x04));
        // Return the valid init packet in response to our request.
        con.expect_read(build_init_response(&FIRST_NONCE, INIT_CHANNEL, TEST_CHANNEL, 0x04));

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
    async fn new_from_connection_non_fido_device() -> Result<(), Error> {
        let con = FakeConnection::new(&BAD_REPORT_DESCRIPTOR);
        assert!(Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await?
            .is_none());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn new_from_connection_fidl_error() -> Result<(), Error> {
        let mut con = FakeConnection::new(&BAD_REPORT_DESCRIPTOR);
        con.error();
        Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await
            .expect_err("Should have failed to create device");
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn cancel_transaction_failure() -> Result<(), Error> {
        // Configure a fake connection to expect a valid initialization then a cancel request,
        // to which we return a failure.
        let con = FakeConnection::new(&FIDO_REPORT_DESCRIPTOR);
        con.expect_write(build_init_request(&FIRST_NONCE, INIT_CHANNEL));
        con.expect_read(build_init_response(&FIRST_NONCE, INIT_CHANNEL, TEST_CHANNEL, 0x01));
        con.expect_write_error(build_packet(TEST_CHANNEL, Command::Cancel, &[]));

        // Attempt to create a device using this connection then perform a cancel.
        let dev = Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await?
            .expect("Failed to create device");
        assert!(dev.cancel_transaction().await.is_err());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn cancel_transaction_success() -> Result<(), Error> {
        // Configure a fake connection to expect a valid initialization then a cancel request.
        let con = FakeConnection::new(&FIDO_REPORT_DESCRIPTOR);
        con.expect_write(build_init_request(&FIRST_NONCE, INIT_CHANNEL));
        con.expect_read(build_init_response(&FIRST_NONCE, INIT_CHANNEL, TEST_CHANNEL, 0x01));
        con.expect_write(build_packet(TEST_CHANNEL, Command::Cancel, &[]));
        // Return a packet on a different channel, and then the error cancel is waiting for.
        con.expect_read(build_packet(BAD_CHANNEL, Command::Wink, &[]));
        con.expect_read(build_packet(TEST_CHANNEL, Command::Error, &[0xbb]));

        // Attempt to create a device using this connection then perform a cancel.
        let dev = Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await?
            .expect("Failed to create device");
        assert!(dev.cancel_transaction().await.is_ok());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn perform_transaction_success() -> Result<(), Error> {
        // Configure a fake connection and expect standard initialization.
        let con = FakeConnection::new(&FIDO_REPORT_DESCRIPTOR);
        con.expect_write(build_init_request(&FIRST_NONCE, INIT_CHANNEL));
        con.expect_read(build_init_response(&FIRST_NONCE, INIT_CHANNEL, TEST_CHANNEL, 0x01));

        // Expect writes for all request packets then reads for all response packets, with a
        // message on a different channel and a keepalive in between.
        con.expect_message_write(TRANSACTION_REQUEST.clone());
        con.expect_read(build_packet(BAD_CHANNEL, Command::Wink, &[]));
        con.expect_read(build_packet(TEST_CHANNEL, Command::Keepalive, &[]));
        con.expect_message_read(TRANSACTION_RESPONSE.clone());

        // Attempt to create a device and request a transaction.
        let dev = Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await?
            .expect("Failed to create device");
        assert_eq!(
            dev.perform_transaction(Command::Msg, &TRANSACTION_REQUEST.payload()).await?,
            TRANSACTION_RESPONSE.payload()
        );
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn perform_transaction_error_response() -> Result<(), Error> {
        // Configure a fake connection and expect standard initialization.
        let con = FakeConnection::new(&FIDO_REPORT_DESCRIPTOR);
        con.expect_write(build_init_request(&FIRST_NONCE, INIT_CHANNEL));
        con.expect_read(build_init_response(&FIRST_NONCE, INIT_CHANNEL, TEST_CHANNEL, 0x01));

        // Expect writes for all request packets then return an error response.
        con.expect_message_write(TRANSACTION_REQUEST.clone());
        con.expect_read(build_packet(TEST_CHANNEL, Command::Error, &ERROR_PAYLOAD));

        // Attempt to create a device and request a transaction. This should fail with a debug
        // string containing the payload of the error.
        let dev = Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await?
            .expect("Failed to create device");
        let transaction_error = dev
            .perform_transaction(Command::Msg, &TRANSACTION_REQUEST.payload())
            .await
            .expect_err("Transaction with an error response should fail");
        assert!(format!("{:?}", transaction_error).contains(&format!("{:?}", ERROR_PAYLOAD)));
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn perform_transaction_requiring_reinitialization() -> Result<(), Error> {
        // Configure a fake connection and expect standard initialization.
        let con = FakeConnection::new(&FIDO_REPORT_DESCRIPTOR);
        con.expect_write(build_init_request(&FIRST_NONCE, INIT_CHANNEL));
        con.expect_read(build_init_response(&FIRST_NONCE, INIT_CHANNEL, TEST_CHANNEL, 0x01));

        // Expect writes for all request packets then fail on the read.
        con.expect_message_write(TRANSACTION_REQUEST.clone());
        con.expect_read_error();
        // Expect the device to reinitialize the connection.
        con.expect_write(build_init_request(&SECOND_NONCE, TEST_CHANNEL));
        con.expect_read(build_init_response(&SECOND_NONCE, TEST_CHANNEL, TEST_CHANNEL, 0x01));

        // Attempt to create a device and request a transaction. This should fail and invoke a
        // reinitialization along the way.
        let dev = Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await?
            .expect("Failed to create device");
        assert!(dev
            .perform_transaction(Command::Msg, &TRANSACTION_REQUEST.payload())
            .await
            .is_err());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn wink() -> Result<(), Error> {
        // Configure a fake connection and expect standard initialization.
        let con = FakeConnection::new(&FIDO_REPORT_DESCRIPTOR);
        con.expect_write(build_init_request(&FIRST_NONCE, INIT_CHANNEL));
        con.expect_read(build_init_response(&FIRST_NONCE, INIT_CHANNEL, TEST_CHANNEL, 0x01));
        // Expect a wink request but return a packet on a different channel before success.
        con.expect_write(build_packet(TEST_CHANNEL, Command::Wink, &[]));
        con.expect_read(build_packet(BAD_CHANNEL, Command::Wink, &[]));
        con.expect_read(build_packet(TEST_CHANNEL, Command::Wink, &[]));
        // Now expect a second wink request and return an invalid message on the correct channel.
        con.expect_write(build_packet(TEST_CHANNEL, Command::Wink, &[]));
        con.expect_read(build_packet(TEST_CHANNEL, Command::Msg, &[]));

        // Attempt to create a device and request two winks, one successful one not.
        let dev = Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await?
            .expect("Failed to create device");
        assert!(dev.wink().await.is_ok());
        assert!(dev.wink().await.is_err());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn ping() -> Result<(), Error> {
        // Configure a fake connection and expect standard initialization.
        let con = FakeConnection::new(&FIDO_REPORT_DESCRIPTOR);
        con.expect_write(build_init_request(&FIRST_NONCE, INIT_CHANNEL));
        con.expect_read(build_init_response(&FIRST_NONCE, INIT_CHANNEL, TEST_CHANNEL, 0x01));

        // Expect a small ping message and the same message in response.
        let mut payload = BytesMut::with_capacity(100);
        for num in 1..=50 {
            payload.put_u16(num);
        }
        let message = Message::new(TEST_CHANNEL, Command::Ping, &payload, REPORT_LENGTH)?;
        con.expect_message_write(message.clone());
        con.expect_message_read(message);

        // Attempt to create a device and request a ping.
        let dev = Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone())
            .await?
            .expect("Failed to create device");
        assert!(dev.ping(100).await.is_ok());
        Ok(())
    }
}
