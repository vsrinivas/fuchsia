// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ctap_hid::connection::{Connection, FidlConnection},
    crate::ctap_hid::message::Message,
    crate::CtapDevice,
    anyhow::{format_err, Context, Error},
    async_trait::async_trait,
    bitfield::bitfield,
    bytes::{Buf, BufMut, Bytes, BytesMut},
    fidl::endpoints::Proxy,
    fidl_fuchsia_fido_report::{CtapHidCommand, SecurityKeyDeviceMarker},
    fidl_fuchsia_io as fio,
    fuchsia_async::{Time, TimeoutExt},
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    futures::TryFutureExt,
    lazy_static::lazy_static,
    rand::{rngs::OsRng, Rng},
    std::io::Read,
    std::path::PathBuf,
    tracing::{info, warn},
};

/// The broadcast channel to use for the initial init request, as defined in the CTAP HID spec.
const INIT_CHANNEL: u32 = 0xffffffff;

/// The number of bytes in an init nonce, as defined in the CTAP HID spec.
const NONCE_LENGTH: u16 = 8;

/// The number of bytes containing the channel, version, and capabilities in a CTAPHID INIT
/// response.
const INIT_FIELDS_LENGTH: u16 = 9;

/// Arbitrary data for ping.
const PING_PAYLOAD: [u8; 4] = [0x12, 0x34, 0x56, 0x78];

lazy_static! {
    /// Time to wait while a device is sending valid packets before we declare a transaction
    /// failure. Set to a large value in case the device is in the middle of responding to a
    /// slow operation for a different client.
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
    #[allow(unused)]
    path: String,
    /// A `Connection` used to communicate with the device.
    connection: C,
    /// A random number generator used for nonce generation.
    /// The rng is mutex-wrapped to facilitate connection error handling without a mutable reference
    /// to the device. A lock is held on this mutex during device operations that should not be
    /// overlapped.
    rng: Mutex<R>,
    /// The CTAPHID channel to use for all packets.
    channel: u32,
    /// The version of the CTAPHID protocol used by the device.
    #[allow(unused)]
    ctaphid_version: u8,
    /// The device's capabilities.
    capabilities: Capabilities,
}

/// The properties that we learn about a connection as we initialize it.
struct ConnectionProperties {
    /// The assigned CTAPHID channel to use for all packets.
    channel: u32,
    /// The version of the CTAPHID protocol used by the device.
    #[allow(unused)]
    ctaphid_version: u8,
    /// The device's capabilities.
    capabilities: Capabilities,
}

#[allow(unused)]
impl<C: Connection, R: Rng> Device<C, R> {
    /// Constructs a new `Device` using the supplied path, `Connection`, and `Rng`
    async fn new_from_connection(
        path: String,
        connection: C,
        mut rng: R,
    ) -> Result<Device<C, R>, Error> {
        info!("Attempting first initialization on {:?}", path);
        let properties = Self::initialize_connection(&connection, &mut rng, INIT_CHANNEL).await?;

        Ok(Device {
            path,
            connection,
            rng: Mutex::new(rng),
            channel: properties.channel,
            ctaphid_version: properties.ctaphid_version,
            capabilities: properties.capabilities,
        })
    }

    /// Returns the capabilities of the device.
    pub fn capabilities(&self) -> &Capabilities {
        &self.capabilities
    }

    /// Returns the channel of this message. Used in tests.
    #[allow(dead_code)]
    pub fn channel(&self) -> u32 {
        self.channel
    }

    /// Returns the ctaphid_version of the device.
    pub fn ctaphid_version(&self) -> &u8 {
        &self.ctaphid_version
    }

    /// Instructs the device to wink, i.e. perform some vendor-defined visual or audible
    /// identification.
    pub async fn wink(&self) -> Result<(), Error> {
        self.perform_transaction(CtapHidCommand::Wink, &[]).await?;
        Ok(())
    }

    /// Performs a ping on the device, to check the device is still present. I.e. sends a
    /// payload of 4 bytes and ensures the same payload is received in response.
    pub async fn ping(&self) -> Result<(), Error> {
        let mut request = BytesMut::with_capacity(4 as usize);
        request.put(&PING_PAYLOAD[..]);
        let response = self.perform_transaction(CtapHidCommand::Ping, &request).await?;
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
        let nonce: [u8; NONCE_LENGTH as usize] = rng.gen();
        let init_message = Message::new(init_channel, CtapHidCommand::Init, &nonce)?;
        connection
            .write_message(init_message)
            .await
            .map_err(|err| format_err!("Error writing init message: {:?}", err))?;

        let message_response = connection
            .read_message(init_channel)
            .on_timeout(Time::after(*TRANSACTION_TIMEOUT), || {
                Err(format_err!("Timed out waiting for valid init response"))
            })
            .await?;

        if message_response.command() != CtapHidCommand::Init {
            return Err(format_err!("Received unexpected command in init response."));
        } else if message_response.payload().len() < (NONCE_LENGTH + INIT_FIELDS_LENGTH) as usize {
            return Err(format_err!(
                "Received short init response ({:?} bytes).",
                message_response.payload().len()
            ));
        }

        let mut payload = message_response.payload().clone();
        let mut received_nonce = [0u8; NONCE_LENGTH as usize];
        payload.copy_to_slice(&mut received_nonce);
        if received_nonce != nonce {
            // It's potentially possible to receive another client's init response, whose
            // nonce won't match the one we sent.
            return Err(format_err!("Received init message with an unexpected nonce."));
        }
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
    async fn send_message(&self, command: CtapHidCommand, payload: &[u8]) -> Result<(), Error> {
        let request = Message::new(self.channel, command, payload)?;
        self.connection
            .write_message(request)
            .await
            .map_err(|err| format_err!("Error writing transaction message: {:?}", err))?;
        Ok(())
    }

    /// Receives a complete CTAPHID message over the connection. This method will not timeout and
    /// will return any errors immediately without trying to correct the state of the connection.
    ///
    /// Callers must ensure that no other operations are conducted on the connection while this
    /// function is in progress.
    async fn receive_message(&self) -> Result<Message, Error> {
        let response_message = loop {
            let curr_message = self.connection.read_message(self.channel).await?;
            if curr_message.command() != CtapHidCommand::Keepalive {
                break curr_message;
            }
        };
        Ok(response_message)
    }

    /// Attempts to cancel a pending CTAPHID transaction by sending a cancellation packet and
    /// swallowing the resulting error packet.
    ///
    /// Callers must ensure that no other operations are conducted on the connection while this
    /// function is in progress.
    async fn cancel_transaction(&self) -> Result<(), Error> {
        let cancel_message = Message::new(self.channel, CtapHidCommand::Cancel, &[])?;
        self.connection
            .write_message(cancel_message)
            .await
            .map_err(|err| format_err!("Error writing cancellation packet: {:?}", err))?;
        Ok(())
    }

    /// Performs a CTAPHID transaction, sending the supplied request payload and returning the
    /// response payload from the device on success.
    ///
    /// Reading the response will timeout after waiting for a maximum of TRANSACTION_TIMEOUT, the
    /// method will attempt to correct connection state if errors are encountered, which may take
    /// up to one additional TRANSACTION_TIMEOUT.
    pub async fn perform_transaction(
        &self,
        command: CtapHidCommand,
        payload: &[u8],
    ) -> Result<Bytes, Error> {
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
                } else if message.command() == CtapHidCommand::Error {
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

#[async_trait(?Send)]
impl CtapDevice for Device<FidlConnection, OsRng> {
    async fn device(dir_proxy: &fio::DirectoryProxy, entry_path: &PathBuf) -> Result<Self, Error> {
        let device_path =
            entry_path.to_str().context(format_err!("Failed to get entry path as a string."))?;
        let (device_proxy, server) = fidl::endpoints::create_proxy::<SecurityKeyDeviceMarker>()?;
        fdio::service_connect_at(
            dir_proxy.as_channel().as_ref(),
            &device_path,
            server.into_channel(),
        )
        .context(format_err!("Failed to connect to CtapHidDevice."))?;
        Device::new_from_connection(
            device_path.to_string(),
            FidlConnection::new(device_proxy),
            Default::default(),
        )
        .await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ctap_hid::connection::fake::FakeConnection;
    use bytes::BufMut;
    use fuchsia_async as fasync;
    use rand::rngs::mock::StepRng;

    const TEST_PATH: &str = "/dev/test-device";
    const TEST_CHANNEL: u32 = 0x88776655;
    /// The first two nonces that `FIXED_SEED_RNG` should generate.
    const FIRST_NONCE: [u8; 8] = [0xad, 0x9c, 0x8b, 0x7a, 0x69, 0x58, 0x47, 0x36];
    const SECOND_NONCE: [u8; 8] = [0x25, 0x14, 0x03, 0xf2, 0xe1, 0xd0, 0xbf, 0xae];

    /// A small payload we use in test errors.
    const ERROR_PAYLOAD: [u8; 1] = [0xcc];

    lazy_static! {
        static ref FIXED_SEED_RNG: StepRng = StepRng::new(0xdead, 0xbeef);
        static ref TRANSACTION_REQUEST: Message =
            Message::new(TEST_CHANNEL, CtapHidCommand::Msg, &[0x11, 100]).unwrap();
        static ref TRANSACTION_RESPONSE: Message =
            Message::new(TEST_CHANNEL, CtapHidCommand::Msg, &[0x22, 100]).unwrap();
    }

    /// Builds an expected request packet for an initialization transaction sent on channel.
    fn build_init_request(nonce: &[u8], channel: u32) -> Message {
        Message::new(channel, CtapHidCommand::Init, nonce).unwrap()
    }

    /// Builds a response packet for an initialization transaction.
    fn build_init_response(
        nonce: &[u8],
        send_channel: u32,
        response_channel: u32,
        capabilities: u8,
    ) -> Message {
        let mut payload = Vec::from(nonce);
        payload.put_u32(response_channel);
        payload.put_u8(2); /* CTAPHID protocol */
        payload.put_u8(0xe1); /* Unused major version */
        payload.put_u8(0xe2); /* Unused minor version */
        payload.put_u8(0xe3); /* Unused build version */
        payload.put_u8(capabilities);
        Message::new(send_channel, CtapHidCommand::Init, &payload).unwrap()
    }

    #[fasync::run_until_stalled(test)]
    async fn new_from_connection_valid_device() -> Result<(), Error> {
        // Configure a fake connection to expect an init request and return a response.
        let con = FakeConnection::new();
        con.expect_send(build_init_request(&FIRST_NONCE, INIT_CHANNEL));
        con.expect_get(build_init_response(&FIRST_NONCE, INIT_CHANNEL, TEST_CHANNEL, 0x04));

        // Create a device using this connection and a deterministic Rng.
        let dev =
            Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone()).await?;
        assert_eq!(dev.path, TEST_PATH);
        assert_eq!(dev.channel, TEST_CHANNEL);
        assert_eq!(dev.capabilities.wink(), false); /* Capability=0x04 does not contain wink */
        assert_eq!(dev.capabilities.cbor(), true); /* Capability=0x04 does contain CBOR */
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn new_from_connection_fidl_error() -> Result<(), Error> {
        let mut con = FakeConnection::new();
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
        let con = FakeConnection::new();
        con.expect_send(build_init_request(&FIRST_NONCE, INIT_CHANNEL));
        con.expect_get(build_init_response(&FIRST_NONCE, INIT_CHANNEL, TEST_CHANNEL, 0x01));
        con.expect_send_error(Message::new(TEST_CHANNEL, CtapHidCommand::Cancel, &[]).unwrap());

        // Attempt to create a device using this connection then perform a cancel.
        let dev =
            Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone()).await?;
        assert!(dev.cancel_transaction().await.is_err());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn cancel_transaction_success() -> Result<(), Error> {
        // Configure a fake connection to expect a valid initialization then a cancel request.
        let con = FakeConnection::new();
        con.expect_send(build_init_request(&FIRST_NONCE, INIT_CHANNEL));
        con.expect_get(build_init_response(&FIRST_NONCE, INIT_CHANNEL, TEST_CHANNEL, 0x01));
        con.expect_send(Message::new(TEST_CHANNEL, CtapHidCommand::Cancel, &[]).unwrap());

        // Attempt to create a device using this connection then perform a cancel.
        let dev =
            Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone()).await?;
        assert!(dev.cancel_transaction().await.is_ok());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn perform_transaction_success() -> Result<(), Error> {
        // Configure a fake connection and expect standard initialization.
        let con = FakeConnection::new();
        con.expect_send(build_init_request(&FIRST_NONCE, INIT_CHANNEL));
        con.expect_get(build_init_response(&FIRST_NONCE, INIT_CHANNEL, TEST_CHANNEL, 0x01));

        // Expect writes for all request packets then reads for all response packets, with a
        // keepalive in between.
        con.expect_send(TRANSACTION_REQUEST.clone());
        con.expect_get(Message::new(TEST_CHANNEL, CtapHidCommand::Keepalive, &[]).unwrap());
        con.expect_get(TRANSACTION_RESPONSE.clone());

        // Attempt to create a device and request a transaction.
        let dev =
            Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone()).await?;
        assert_eq!(
            dev.perform_transaction(CtapHidCommand::Msg, &TRANSACTION_REQUEST.payload()).await?,
            TRANSACTION_RESPONSE.payload()
        );
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn perform_transaction_error_response() -> Result<(), Error> {
        // Configure a fake connection and expect standard initialization.
        let con = FakeConnection::new();
        con.expect_send(build_init_request(&FIRST_NONCE, INIT_CHANNEL));
        con.expect_get(build_init_response(&FIRST_NONCE, INIT_CHANNEL, TEST_CHANNEL, 0x01));

        // Expect writes for all request packets then return an error response.
        con.expect_send(TRANSACTION_REQUEST.clone());
        con.expect_get(Message::new(TEST_CHANNEL, CtapHidCommand::Error, &ERROR_PAYLOAD).unwrap());

        // Attempt to create a device and request a transaction. This should fail with a debug
        // string containing the payload of the error.
        let dev =
            Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone()).await?;
        let transaction_error = dev
            .perform_transaction(CtapHidCommand::Msg, &TRANSACTION_REQUEST.payload())
            .await
            .expect_err("Transaction with an error response should fail");
        assert!(format!("{:?}", transaction_error).contains(&format!("{:?}", ERROR_PAYLOAD)));
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn perform_transaction_requiring_reinitialization() -> Result<(), Error> {
        // Configure a fake connection and expect standard initialization.
        let con = FakeConnection::new();
        con.expect_send(build_init_request(&FIRST_NONCE, INIT_CHANNEL));
        con.expect_get(build_init_response(&FIRST_NONCE, INIT_CHANNEL, TEST_CHANNEL, 0x01));

        // Expect writes for all request packets then fail on the read.
        con.expect_send(TRANSACTION_REQUEST.clone());
        con.expect_get_error();
        // Expect the device to reinitialize the connection.
        con.expect_send(build_init_request(&SECOND_NONCE, TEST_CHANNEL));
        con.expect_get(build_init_response(&SECOND_NONCE, TEST_CHANNEL, TEST_CHANNEL, 0x01));

        // Attempt to create a device and request a transaction. This should fail and invoke a
        // reinitialization along the way.
        let dev =
            Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone()).await?;
        assert!(dev
            .perform_transaction(CtapHidCommand::Msg, &TRANSACTION_REQUEST.payload())
            .await
            .is_err());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn wink() -> Result<(), Error> {
        // Configure a fake connection and expect standard initialization.
        let con = FakeConnection::new();
        con.expect_send(build_init_request(&FIRST_NONCE, INIT_CHANNEL));
        con.expect_get(build_init_response(&FIRST_NONCE, INIT_CHANNEL, TEST_CHANNEL, 0x01));
        // Expect a wink request but return a packet on a different channel before success.
        con.expect_send(Message::new(TEST_CHANNEL, CtapHidCommand::Wink, &[]).unwrap());
        con.expect_get(Message::new(TEST_CHANNEL, CtapHidCommand::Wink, &[]).unwrap());
        // Now expect a second wink request and return an invalid message on the correct channel.
        con.expect_send(Message::new(TEST_CHANNEL, CtapHidCommand::Wink, &[]).unwrap());
        con.expect_get(Message::new(TEST_CHANNEL, CtapHidCommand::Msg, &[]).unwrap());

        // Attempt to create a device and request two winks, one successful one not.
        let dev =
            Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone()).await?;
        assert!(dev.wink().await.is_ok());
        assert!(dev.wink().await.is_err());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn ping() -> Result<(), Error> {
        // Configure a fake connection and expect standard initialization.
        let con = FakeConnection::new();
        con.expect_send(build_init_request(&FIRST_NONCE, INIT_CHANNEL));
        con.expect_get(build_init_response(&FIRST_NONCE, INIT_CHANNEL, TEST_CHANNEL, 0x01));

        // Expect a small ping message and the same message in response.
        let mut payload = BytesMut::with_capacity(4 as usize);
        payload.put(&PING_PAYLOAD[..]);

        let message = Message::new(TEST_CHANNEL, CtapHidCommand::Ping, &payload)?;
        con.expect_send(message.clone());
        con.expect_get(message);

        // Attempt to create a device and request a ping.
        let dev =
            Device::new_from_connection(TEST_PATH.to_string(), con, FIXED_SEED_RNG.clone()).await?;
        assert!(dev.ping().await.is_ok());
        Ok(())
    }
}
