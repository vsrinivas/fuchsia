// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::ctap_hid::message::Message, anyhow::Error, async_trait::async_trait, std::fmt::Debug};

#[cfg(test)]
pub use self::fake::FakeConnection;
pub use self::fidl::FidlConnection;

/// A basic connection to a CTAPHID device.
#[async_trait(?Send)]
pub trait Connection: Sized + Debug {
    /// Receives a CTAP message composed from packets (aka HID reports) from the device.
    async fn read_message(&self, channel_id: u32) -> Result<Message, Error>;

    /// Writes a CTAP message to be deconstructed into packets (aka HID reports) to the device.
    async fn write_message(&self, packet: Message) -> Result<(), Error>;
}

/// An implementation of a `Connection` over the FIDL `fuchsia.fido.report.SecurityKeyDevice`
/// protocol.
pub mod fidl {
    use crate::ctap_hid::connection::Connection;
    use crate::ctap_hid::message::Message;
    use anyhow::{format_err, Error};
    use async_trait::async_trait;
    use fidl_fuchsia_fido_report::Message as FidoMessage;
    use fidl_fuchsia_fido_report::SecurityKeyDeviceProxy;
    use fuchsia_async::{Time, TimeoutExt};
    use fuchsia_zircon as zx;
    use futures::TryFutureExt;
    use lazy_static::lazy_static;
    use std::convert::TryInto;

    lazy_static! {
        /// Time to wait before declaring a FIDL call to be failed.
        // Note: This choice of time is somewhat arbitrary and may need tuning in the future. Its
        // intended to be large enough that a healthy system does not timeout and small enough that
        // waiting for FIDL doesn't interfere with the timeouts we set on the security key.
        static ref FIDL_TIMEOUT: zx::Duration = zx::Duration::from_millis(50);

        /// Time to wait before declaring a read failure. Set slightly higher than the 100ms
        /// KEEPALIVE requirement in the CTAPHID specification.
        static ref READ_MESSAGE_TIMEOUT: zx::Duration = zx::Duration::from_millis(110);
    }

    /// A connection to a CTAPHID device over the FIDL `Device` protocol.
    #[derive(Debug)]
    pub struct FidlConnection {
        proxy: SecurityKeyDeviceProxy,
    }

    impl FidlConnection {
        /// Constructs a new `FidlConnection` using the supplied `SecurityKeyDeviceProxy`.
        pub fn new(proxy: SecurityKeyDeviceProxy) -> FidlConnection {
            FidlConnection { proxy }
        }
    }

    #[async_trait(?Send)]
    impl Connection for FidlConnection {
        async fn read_message(&self, channel_id: u32) -> Result<Message, Error> {
            match self
                .proxy
                .get_message(channel_id)
                .map_err(|err| format_err!("FIDL error getting message: {:?}", err))
                .on_timeout(Time::after(*FIDL_TIMEOUT), || {
                    Err(format_err!("FIDL timeout on GetMessage"))
                })
                .await
            {
                Ok(res) => match res {
                    Ok(data) => return Ok(Message::from(data)),
                    Err(err) => {
                        return Err(format_err!(
                            "Received bad status on GetMessage: {:?}",
                            zx::Status::from_raw(err)
                        ))
                    }
                },
                Err(e) => return Err(e),
            }
        }

        async fn write_message(&self, message: Message) -> Result<(), Error> {
            let message: FidoMessage = message.try_into()?;
            match self
                .proxy
                .send_message(message)
                .map_err(|err| format_err!("FIDL error sending message: {:?}", err))
                .on_timeout(Time::after(*FIDL_TIMEOUT), || {
                    Err(format_err!("FIDL timeout on SendMessage"))
                })
                .await
            {
                Ok(res) => match res {
                    Ok(()) => Ok(()),
                    Err(e) => match zx::Status::from_raw(e) {
                        zx::Status::OK => Ok(()),
                        status => {
                            return Err(format_err!(
                                "Received not-ok status sending message: {:?}",
                                status
                            ))
                        }
                    },
                },
                Err(e) => return Err(e),
            }
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use fidl::endpoints::create_proxy_and_stream;
        use fidl_fuchsia_fido_report::{
            CtapHidCommand, SecurityKeyDeviceMarker, SecurityKeyDeviceRequest,
        };
        use fuchsia_async as fasync;
        use futures::TryStreamExt;

        const TEST_CHANNEL: u32 = 0xfeefbccb;
        const TEST_PAYLOAD: [u8; 5] = [0x86, 0x00, 0x02, 0x88, 0x99];

        /// Creates a mock device proxy that will invoke the supplied function on each request.
        fn mock_device_proxy<F>(request_fn: F) -> SecurityKeyDeviceProxy
        where
            F: (Fn(SecurityKeyDeviceRequest, u32) -> ()) + Send + 'static,
        {
            let (device_proxy, mut stream) = create_proxy_and_stream::<SecurityKeyDeviceMarker>()
                .expect("Failed to create proxy and stream");
            fasync::Task::spawn(async move {
                let mut req_num = 0u32;
                while let Some(req) = stream.try_next().await.expect("Failed to read req") {
                    req_num += 1;
                    request_fn(req, req_num)
                }
            })
            .detach();
            device_proxy
        }

        #[fasync::run_until_stalled(test)]
        async fn read_immediate_message() -> Result<(), Error> {
            let proxy = mock_device_proxy(|req, _| match req {
                SecurityKeyDeviceRequest::GetMessage { responder, .. } => {
                    let mut test_message: Result<fidl_fuchsia_fido_report::Message, i32> =
                        Ok(fidl_fuchsia_fido_report::Message {
                            channel_id: Some(TEST_CHANNEL),
                            command_id: Some(CtapHidCommand::Init),
                            data: Some(TEST_PAYLOAD.to_vec()),
                            payload_len: Some(TEST_PAYLOAD.len() as u16),
                            ..fidl_fuchsia_fido_report::Message::EMPTY
                        });

                    responder.send(&mut test_message).expect("failed to send response");
                }
                _ => panic!("got unexpected device request."),
            });
            let connection = FidlConnection::new(proxy);
            let test_message =
                Message::new(TEST_CHANNEL, CtapHidCommand::Init, &TEST_PAYLOAD).unwrap();
            assert_eq!(connection.read_message(TEST_CHANNEL).await?, test_message.into());
            Ok(())
        }

        #[fasync::run_singlethreaded(test)]
        async fn read_message_timeout() -> Result<(), Error> {
            let proxy = mock_device_proxy(|req, req_num| match (req, req_num) {
                (SecurityKeyDeviceRequest::GetMessage { .. }, 1) => {
                    // Never send a response.
                }
                (req, num) => panic!("got unexpected device request {:?} as num {:?}", req, num),
            });
            let connection = FidlConnection::new(proxy);
            assert!(connection.read_message(TEST_CHANNEL).await.is_err());
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn write_message() -> Result<(), Error> {
            let proxy = mock_device_proxy(|req, _| match req {
                SecurityKeyDeviceRequest::SendMessage { payload, responder } => {
                    if payload.data.unwrap()[..] != TEST_PAYLOAD[..] {
                        panic!("received unexpected packet.")
                    }
                    responder.send(&mut Err(zx::sys::ZX_OK)).expect("failed to send response");
                }
                _ => panic!("got unexpected device request."),
            });
            let connection = FidlConnection::new(proxy);
            connection
                .write_message(Message::new(TEST_CHANNEL, CtapHidCommand::Init, &TEST_PAYLOAD)?)
                .await
        }
    }
}
/// A fake implementation of a `Connection` to simplify unit testing.
#[cfg(test)]
pub mod fake {
    use {
        crate::ctap_hid::connection::Connection,
        crate::ctap_hid::message::Message,
        anyhow::{format_err, Error},
        async_trait::async_trait,
        futures::lock::Mutex,
        std::collections::VecDeque,
        std::thread,
    };

    /// A single operation for a fake connection
    #[derive(Debug)]
    enum Operation {
        /// Expect a call to write the specified packet and return success.
        SendSuccess(Message),
        /// Expect a call to write the specified packet and return an error.
        SendFail(Message),
        /// Expect a call to read a packet and return the supplied data packet.
        GetSuccess(Message),
        /// Expect a call to read a packet and return an error.
        GetFail(),
    }

    /// The mode that a fake connection should operate in.
    #[derive(Debug)]
    enum Mode {
        /// The connection returns errors on all calls.
        Invalid,
        /// The connection potentially returns valid data.
        Valid {
            /// A queue of expected operations and intended responses.
            operations: Mutex<VecDeque<Operation>>,
        },
    }

    /// A fake implementation of a `Connection` to a CTAPHID device to simplify unit testing.
    ///
    /// `FakeConnections` may either be set to be invalid, in which case they return errors for
    /// all calls, or valid. A valid `FakeConnection` can perform send and get operations
    /// following an expected set of operations supplied by the test code using it. If a
    /// `FakeConnection` receives requests that do not align with the exception it panics.
    #[derive(Debug)]
    pub struct FakeConnection {
        /// The current operational state of the connection.
        mode: Mode,
    }

    impl FakeConnection {
        /// Constructs a new `FidlConnection`.
        pub fn new() -> FakeConnection {
            FakeConnection { mode: Mode::Valid { operations: Mutex::new(VecDeque::new()) } }
        }

        /// Sets all further calls on this FakeConnection to return errors.
        /// This simulates the behavior of a connection to a device that has been removed.
        pub fn error(&mut self) {
            self.expect_complete();
            self.mode = Mode::Invalid;
        }

        /// Enqueues a single operation, provided the connection is valid.
        /// Panics if called on a connection that has been set to fail.
        fn enqueue(&self, operation: Operation) {
            match &self.mode {
                Mode::Valid { operations, .. } => {
                    operations.try_lock().unwrap().push_back(operation);
                }
                Mode::Invalid => panic!("Cannot queue operations on a failing FakeConnection"),
            }
        }

        /// Enqueues an expectation that send will be called on this connection with the supplied
        /// message. The connection will return success when this send operation occurs.
        /// Panics if called on a connection that has been set to fail.
        pub fn expect_send(&self, message: Message) {
            self.enqueue(Operation::SendSuccess(message));
        }

        /// Enqueues an expectation that send will be called on this connection with the supplied
        /// message. The connection will return a failure when this send operation occurs.
        /// Panics if called on a connection that has been set to fail.
        pub fn expect_send_error(&self, message: Message) {
            self.enqueue(Operation::SendFail(message));
        }

        /// Enqueues an expectation that get will be called on this connection. The connection
        /// will return success and the supplied message when this get operation occurs.
        /// Panics if called on a connection that has been set to fail.
        pub fn expect_get(&self, message: Message) {
            self.enqueue(Operation::GetSuccess(message));
        }

        /// Enqueues an expectation that get will be called on this connection. The connection
        /// will return a failure when this get operation occurs.
        /// Panics if called on a connection that has been set to fail.
        pub fn expect_get_error(&self) {
            self.enqueue(Operation::GetFail());
        }

        /// Verified that all expected operations have now been completed.
        /// Panics if this is not true.
        pub fn expect_complete(&self) {
            if let Mode::Valid { operations, .. } = &self.mode {
                let ops = operations.try_lock().unwrap();
                if !ops.is_empty() {
                    panic!(
                        "FakeConnection has expected operations that were not performed: {:?}",
                        ops
                    );
                }
            }
        }
    }

    impl Drop for FakeConnection {
        fn drop(&mut self) {
            if !thread::panicking() {
                self.expect_complete();
            }
        }
    }

    #[async_trait(?Send)]
    impl Connection for FakeConnection {
        async fn read_message(&self, channel_id: u32) -> Result<Message, Error> {
            match &self.mode {
                Mode::Valid { operations, .. } => match operations.lock().await.pop_front() {
                    Some(Operation::GetSuccess(message)) => {
                        if message.channel() == channel_id {
                            Ok(message)
                        } else {
                            Err(format_err!("no matching message on channel for read message"))
                        }
                    }
                    Some(Operation::GetFail()) => Err(format_err!("Read failing as requested")),
                    a => panic!("Received unexpected read request {:?} ", a),
                },
                Mode::Invalid => Err(format_err!("Read called on set-to-fail fake connection")),
            }
        }

        async fn write_message(&self, message: Message) -> Result<(), Error> {
            match &self.mode {
                Mode::Valid { operations, .. } => match operations.lock().await.pop_front() {
                    Some(Operation::SendSuccess(expected)) => {
                        if message != expected {
                            panic!(
                                "Received write request that did not match expectation:\n\
                             Expected={:?}\nReceived={:?}",
                                expected, message
                            );
                        }
                        Ok(())
                    }
                    Some(Operation::SendFail(expected)) => {
                        if message != expected {
                            panic!(
                                "Received write request that did not match expectation:\n\
                             Expected={:?}\nReceived={:?}",
                                expected, message
                            );
                        }
                        Err(format_err!("Write failing as requested"))
                    }
                    _ => panic!("Received unexpected write request"),
                },
                Mode::Invalid => Err(format_err!("Write called on set-to-fail fake connection")),
            }
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use crate::ctap_hid::message::Message;
        use fidl_fuchsia_fido_report::CtapHidCommand;
        use fuchsia_async as fasync;
        use lazy_static::lazy_static;

        lazy_static! {
            static ref TEST_MESSAGE_1: Message =
                Message::new(0x12345678, CtapHidCommand::Init, &vec![]).unwrap();
            static ref TEST_MESSAGE_2: Message =
                Message::new(0x23456789, CtapHidCommand::Wink, &vec![0xff, 0xee, 0xdd, 0xcc])
                    .unwrap();
            static ref TEST_MESSAGE_3: Message =
                Message::new(0x34567890, CtapHidCommand::Wink, &vec![0x99, 0x99]).unwrap();
        }

        #[fasync::run_until_stalled(test)]
        async fn read_write() -> Result<(), Error> {
            // Declare expected operations.
            let connection = FakeConnection::new();
            connection.expect_send_error(TEST_MESSAGE_1.clone());
            connection.expect_send(TEST_MESSAGE_1.clone());
            connection.expect_get_error();
            connection.expect_get(TEST_MESSAGE_2.clone());
            connection.expect_send(TEST_MESSAGE_3.clone());
            // Perform operations.
            connection
                .write_message(TEST_MESSAGE_1.clone())
                .await
                .expect_err("Write should have failed");
            connection
                .write_message(TEST_MESSAGE_1.clone())
                .await
                .expect("Write should have succeeded");
            connection
                .read_message(TEST_MESSAGE_2.channel())
                .await
                .expect_err("Read should have failed");
            assert_eq!(
                connection
                    .read_message(TEST_MESSAGE_2.channel())
                    .await
                    .expect("Read should have succeeded"),
                *TEST_MESSAGE_2
            );
            connection
                .write_message(TEST_MESSAGE_3.clone())
                .await
                .expect("Write should have succeeded");
            // Verify all expected operations occurred.
            connection.expect_complete();
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        #[should_panic]
        async fn write_unexpected_data() {
            let connection = FakeConnection::new();
            connection.expect_send(TEST_MESSAGE_1.clone());
            connection.write_message(TEST_MESSAGE_2.clone()).await.unwrap();
        }

        #[fasync::run_until_stalled(test)]
        #[should_panic]
        async fn write_when_expecting_read() {
            let connection = FakeConnection::new();
            connection.expect_get(TEST_MESSAGE_1.clone());
            connection.write_message(TEST_MESSAGE_1.clone()).await.unwrap();
        }

        #[fasync::run_until_stalled(test)]
        #[should_panic]
        async fn read_when_expecting_write() {
            let connection = FakeConnection::new();
            connection.expect_send(TEST_MESSAGE_1.clone());
            connection.read_message(TEST_MESSAGE_1.channel()).await.unwrap();
        }

        #[fasync::run_until_stalled(test)]
        #[should_panic]
        async fn incomplete_operations() {
            let connection = FakeConnection::new();
            connection.expect_send(TEST_MESSAGE_1.clone());
            // Dropping the connection should verify all expected operations are complete.
        }

        #[fasync::run_until_stalled(test)]
        async fn set_error() -> Result<(), Error> {
            let mut connection = FakeConnection::new();
            connection.error();
            connection
                .read_message(TEST_MESSAGE_1.channel())
                .await
                .expect_err("Should have failed to read packet");
            connection
                .write_message(TEST_MESSAGE_1.clone())
                .await
                .expect_err("Should have failed to write packet");
            Ok(())
        }
    }
}
