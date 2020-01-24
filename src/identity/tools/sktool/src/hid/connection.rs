// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::hid::message::Packet;
use anyhow::Error;
use async_trait::async_trait;
use bytes::Bytes;
use std::fmt::Debug;

#[cfg(test)]
pub use self::fake::FakeConnection;
pub use self::fidl::FidlConnection;

/// A basic connection to a HID device.
#[async_trait(?Send)]
pub trait Connection: Sized + Debug {
    /// Receives a single CTAP packet (aka HID report) from the device.
    async fn read_packet(&self) -> Result<Packet, Error>;

    /// Writes a single CTAP packet (aka HID report) to the device.
    async fn write_packet(&self, packet: Packet) -> Result<(), Error>;

    /// Returns the report descriptor for this device.
    async fn report_descriptor(&self) -> Result<Bytes, Error>;

    /// Returns the maximum length of packets this device can read.
    async fn max_packet_length(&self) -> Result<u16, Error>;

    /// Waits for a CTAP packet (aka HID report) on which the supplied predicate returns true.
    /// If the predicate returns an error the packet is considered illegal and the function will
    /// terminate immediately with this error.
    async fn read_matching_packet<F>(&self, predicate: F) -> Result<Packet, Error>
    where
        F: Fn(&Packet) -> Result<bool, Error>,
    {
        loop {
            let packet = self.read_packet().await?;
            if predicate(&packet)? {
                return Ok(packet);
            }
        }
    }
}

/// An implementation of a `Connection` over the FIDL `fuchsia.hardware.input.Device` protocol.
pub mod fidl {
    use crate::hid::connection::Connection;
    use crate::hid::message::Packet;
    use anyhow::{format_err, Error};
    use async_trait::async_trait;
    use bytes::Bytes;
    use fidl_fuchsia_hardware_input::{DeviceProxy, ReportType};
    use fuchsia_async::{self as fasync, Time, TimeoutExt};
    use fuchsia_zircon as zx;
    use futures::TryFutureExt;
    use lazy_static::lazy_static;
    use std::convert::TryFrom;

    // TODO(jsankey): Don't hardcode the report IDs, although its hard to imagine other values
    const OUTPUT_REPORT_ID: u8 = 0;
    #[allow(dead_code)]
    const INPUT_REPORT_ID: u8 = 0;

    lazy_static! {
        /// Time to wait before declaring a FIDL call to be failed.
        // Note: This choice of time is somewhat arbitrary and may need tuning in the future. Its
        // intended to be large enough that a healthy system does not timeout and small enough that
        // waiting for FIDL doesn't interfere with the timeouts we set on the security key.
        static ref FIDL_TIMEOUT: zx::Duration = zx::Duration::from_millis(50);

        /// Time to wait before declaring a read failure. Set slightly higher than the 100ms
        /// KEEPALIVE requirement in the CTAPHID specification.
        static ref READ_PACKET_TIMEOUT: zx::Duration = zx::Duration::from_millis(110);
    }

    /// An connection to a HID device over the FIDL `Device` protocol.
    #[derive(Debug)]
    pub struct FidlConnection {
        proxy: DeviceProxy,
    }

    impl FidlConnection {
        /// Constructs a new `FidlConnection` using the supplied `DeviceProxy`.
        pub fn new(proxy: DeviceProxy) -> FidlConnection {
            FidlConnection { proxy }
        }

        /// Helper method to call the FIDL `ReadReports` method and format the results.
        async fn read_reports(&self) -> Result<(zx::Status, Vec<u8>), Error> {
            self.proxy
                .read_reports()
                .map_err(|err| format_err!("FIDL error on ReadReports: {:?}", err))
                .on_timeout(Time::after(*FIDL_TIMEOUT), || {
                    Err(format_err!("FIDL timeout on ReadReports"))
                })
                .await
                .map(|(status, data)| (zx::Status::from_raw(status), data))
        }

        /// Helper method to call the FIDL `GetReportsEvent` method and format the results.
        async fn get_reports_event(&self) -> Result<zx::Event, Error> {
            let (status, event) = self
                .proxy
                .get_reports_event()
                .map_err(|err| format_err!("FIDL error on GetReportsEvent: {:?}", err))
                .on_timeout(Time::after(*FIDL_TIMEOUT), || {
                    Err(format_err!("FIDL timeout on GetReportsEvent"))
                })
                .await
                .map(|(status, event)| (zx::Status::from_raw(status), event))?;
            if status != zx::Status::OK {
                return Err(format_err!("Bad status on GetReportsEvent: {:?}", status));
            }
            Ok(event)
        }
    }

    #[async_trait(?Send)]
    impl Connection for FidlConnection {
        async fn read_packet(&self) -> Result<Packet, Error> {
            // Poll once to see if a report is already waiting.
            let (status, data) = self.read_reports().await?;
            match status {
                zx::Status::OK => return Packet::try_from(data),
                zx::Status::SHOULD_WAIT => (),
                _ => return Err(format_err!("Received bad status on ReadReports: {:?}", status)),
            }

            // If we were told to wait, use GetReportEvent to do that. Although not documented in
            // FIDL, GetReportsEvent communicates through `DEV_STATE_READABLE` which is defined as
            // `ZX_USER_SIGNAL_0` in ddk/device.h.
            let event = self.get_reports_event().await?;
            fasync::OnSignals::new(&event, zx::Signals::USER_0)
                .on_timeout(Time::after(*READ_PACKET_TIMEOUT), || Err(zx::Status::SHOULD_WAIT))
                .await
                .map_err(|err| {
                    if err == zx::Status::SHOULD_WAIT {
                        format_err!("Timeout on GetReportsEvent")
                    } else {
                        format_err!("Error waiting on event: {:?}", err)
                    }
                })?;

            // Now we expect a report to be waiting, poll again
            let (status, data) = self.read_reports().await?;
            match status {
                zx::Status::OK => Packet::try_from(data),
                _ => {
                    Err(format_err!("Received bad status on post-event ReadReports: {:?}", status))
                }
            }
        }

        async fn write_packet(&self, packet: Packet) -> Result<(), Error> {
            match self
                .proxy
                .set_report(ReportType::Output, OUTPUT_REPORT_ID, &mut packet.into_iter())
                .await
                .map_err(|err| format_err!("FIDL error writing packet: {:?}", err))
                .map(|status| zx::Status::from_raw(status))?
            {
                zx::Status::OK => Ok(()),
                s => Err(format_err!("Received not-ok status sending packet: {:?}", s)),
            }
        }

        async fn report_descriptor(&self) -> Result<Bytes, Error> {
            self.proxy
                .get_report_desc()
                .map_err(|err| format_err!("FIDL error: {:?}", err))
                .on_timeout(Time::after(*FIDL_TIMEOUT), || {
                    Err(format_err!("FIDL timeout on GetReportDesc"))
                })
                .await
                .map(|vec| Bytes::from(vec))
        }

        async fn max_packet_length(&self) -> Result<u16, Error> {
            self.proxy
                .get_max_input_report_size()
                .map_err(|err| format_err!("FIDL error: {:?}", err))
                .on_timeout(Time::after(*FIDL_TIMEOUT), || {
                    Err(format_err!("FIDL timeout on GetMaxinputReportSize"))
                })
                .await
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use fidl::endpoints::{create_proxy_and_stream, RequestStream};
        use fidl_fuchsia_hardware_input::{DeviceMarker, DeviceRequest};
        use fuchsia_async as fasync;
        use fuchsia_zircon::AsHandleRef;
        use futures::TryStreamExt;

        const TEST_REPORT_LENGTH: u16 = 99;
        const TEST_REPORT_DESCRIPTOR: [u8; 8] = [0x06, 0xd0, 0xf1, 0x09, 0x01, 0xa1, 0x01, 0x09];
        const TEST_CHANNEL: u32 = 0xfeefbccb;
        const TEST_PACKET: [u8; 9] = [0xfe, 0xef, 0xbc, 0xcb, 0x86, 0x00, 0x02, 0x88, 0x99];
        const DIFFERENT_PACKET: [u8; 9] = [0x12, 0x21, 0x34, 0x43, 0x86, 0x00, 0x02, 0x88, 0x99];

        /// Creates a mock device proxy that will invoke the supplied function on each request.
        fn valid_mock_device_proxy<F>(request_fn: F) -> DeviceProxy
        where
            F: (Fn(DeviceRequest, u32) -> ()) + Send + 'static,
        {
            let (device_proxy, mut stream) = create_proxy_and_stream::<DeviceMarker>()
                .expect("Failed to create proxy and stream");
            fasync::spawn(async move {
                let mut req_num = 0u32;
                while let Some(req) = stream.try_next().await.expect("Failed to read req") {
                    req_num += 1;
                    request_fn(req, req_num)
                }
            });
            device_proxy
        }

        /// Creates a mock device proxy that will immediately close the channel.
        fn invalid_mock_device_proxy() -> DeviceProxy {
            let (device_proxy, stream) = create_proxy_and_stream::<DeviceMarker>()
                .expect("Failed to create proxy and stream");
            stream.control_handle().shutdown();
            device_proxy
        }

        #[fasync::run_until_stalled(test)]
        async fn test_read_immediate_packet() -> Result<(), Error> {
            let proxy = valid_mock_device_proxy(|req, _| match req {
                DeviceRequest::ReadReports { responder } => {
                    let response = TEST_PACKET.to_vec();
                    responder
                        .send(zx::sys::ZX_OK, &mut response.into_iter())
                        .expect("failed to send response");
                }
                _ => panic!("got unexpected device request."),
            });
            let connection = FidlConnection::new(proxy);
            assert_eq!(connection.read_packet().await?, Packet::try_from(TEST_PACKET.to_vec())?);
            Ok(())
        }

        #[fasync::run_singlethreaded(test)]
        async fn test_read_delayed_packet() -> Result<(), Error> {
            let proxy = valid_mock_device_proxy(|req, req_num| match (req, req_num) {
                (DeviceRequest::ReadReports { responder }, 1) => {
                    responder
                        .send(zx::sys::ZX_ERR_SHOULD_WAIT, &mut vec![].into_iter())
                        .expect("failed to send response");
                }
                (DeviceRequest::GetReportsEvent { responder }, 2) => {
                    let event = zx::Event::create().unwrap();
                    event.signal_handle(zx::Signals::NONE, zx::Signals::USER_0).unwrap();
                    responder.send(zx::sys::ZX_OK, event).expect("failed to send response");
                }
                (DeviceRequest::ReadReports { responder }, 3) => {
                    let response = TEST_PACKET.to_vec();
                    responder
                        .send(zx::sys::ZX_OK, &mut response.into_iter())
                        .expect("failed to send response");
                }
                (req, num) => panic!("got unexpected device request {:?} as num {:?}", req, num),
            });
            let connection = FidlConnection::new(proxy);
            assert_eq!(connection.read_packet().await?, Packet::try_from(TEST_PACKET.to_vec())?);
            Ok(())
        }

        #[fasync::run_singlethreaded(test)]
        async fn test_read_packet_timeout() -> Result<(), Error> {
            let proxy = valid_mock_device_proxy(|req, req_num| match (req, req_num) {
                (DeviceRequest::ReadReports { responder }, 1) => {
                    responder
                        .send(zx::sys::ZX_ERR_SHOULD_WAIT, &mut vec![].into_iter())
                        .expect("failed to send response");
                }
                (DeviceRequest::GetReportsEvent { responder }, 2) => {
                    // Generate an event but never signal it.
                    let event = zx::Event::create().unwrap();
                    responder.send(zx::sys::ZX_OK, event).expect("failed to send response");
                }
                (req, num) => panic!("got unexpected device request {:?} as num {:?}", req, num),
            });
            let connection = FidlConnection::new(proxy);
            assert!(connection.read_packet().await.is_err());
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn test_read_matching_packet_success() -> Result<(), Error> {
            let proxy = valid_mock_device_proxy(|req, req_num| match (req, req_num) {
                (DeviceRequest::ReadReports { responder }, 1) => {
                    let response = DIFFERENT_PACKET.to_vec();
                    responder
                        .send(zx::sys::ZX_OK, &mut response.into_iter())
                        .expect("failed to send response");
                }
                (DeviceRequest::ReadReports { responder }, 2) => {
                    let response = TEST_PACKET.to_vec();
                    responder
                        .send(zx::sys::ZX_OK, &mut response.into_iter())
                        .expect("failed to send response");
                }
                _ => panic!("got unexpected device request."),
            });
            let connection = FidlConnection::new(proxy);
            let received = connection
                .read_matching_packet(|packet| Ok(packet.channel() == TEST_CHANNEL))
                .await?;
            assert_eq!(received, Packet::try_from(TEST_PACKET.to_vec())?);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn test_read_matching_packet_fail() -> Result<(), Error> {
            let proxy = valid_mock_device_proxy(|req, req_num| match (req, req_num) {
                (DeviceRequest::ReadReports { responder }, 1) => {
                    let response = DIFFERENT_PACKET.to_vec();
                    responder
                        .send(zx::sys::ZX_OK, &mut response.into_iter())
                        .expect("failed to send response");
                }
                (DeviceRequest::ReadReports { responder }, 2) => {
                    let response = TEST_PACKET.to_vec();
                    responder
                        .send(zx::sys::ZX_OK, &mut response.into_iter())
                        .expect("failed to send response");
                }
                _ => panic!("got unexpected device request."),
            });
            let connection = FidlConnection::new(proxy);
            connection
                .read_matching_packet(|packet| {
                    if packet.channel() == TEST_CHANNEL {
                        Err(format_err!("expected"))
                    } else {
                        Ok(false)
                    }
                })
                .await
                .expect_err("Should have failed read matching packet on TEST_CHANNEL receipt");
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn test_write_packet() -> Result<(), Error> {
            let proxy = valid_mock_device_proxy(|req, _| match req {
                DeviceRequest::SetReport {
                    type_: ReportType::Output,
                    id: 0,
                    report,
                    responder,
                } => {
                    if report != &TEST_PACKET[..] {
                        panic!("received unexpected packet.")
                    }
                    responder.send(zx::sys::ZX_OK).expect("failed to send response");
                }
                _ => panic!("got unexpected device request."),
            });
            let connection = FidlConnection::new(proxy);
            connection.write_packet(Packet::try_from(TEST_PACKET.to_vec())?).await
        }

        #[fasync::run_until_stalled(test)]
        async fn test_report_descriptor() -> Result<(), Error> {
            let proxy = valid_mock_device_proxy(|req, _| match req {
                DeviceRequest::GetReportDesc { responder } => {
                    let response = TEST_REPORT_DESCRIPTOR.to_vec();
                    responder.send(&mut response.into_iter()).expect("failed to send response");
                }
                _ => panic!("got unexpected device request."),
            });
            let connection = FidlConnection::new(proxy);
            assert_eq!(connection.report_descriptor().await?, &TEST_REPORT_DESCRIPTOR[..]);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn test_max_packet_length() -> Result<(), Error> {
            let proxy = valid_mock_device_proxy(|req, _| match req {
                DeviceRequest::GetMaxInputReportSize { responder } => {
                    responder.send(TEST_REPORT_LENGTH).expect("failed to send response");
                }
                _ => panic!("got unexpected device request."),
            });
            let connection = FidlConnection::new(proxy);
            assert_eq!(connection.max_packet_length().await?, TEST_REPORT_LENGTH);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn test_fidl_error() -> Result<(), Error> {
            let connection = FidlConnection::new(invalid_mock_device_proxy());
            connection.report_descriptor().await.expect_err("Should have failed to get descriptor");
            connection.max_packet_length().await.expect_err("Should have failed to get packet len");
            Ok(())
        }
    }
}

/// A fake implementation of a `Connection` to simplify unit testing.
#[cfg(test)]
pub mod fake {
    use crate::hid::connection::Connection;
    use crate::hid::message::Packet;
    use anyhow::{format_err, Error};
    use async_trait::async_trait;
    use bytes::Bytes;
    use fuchsia_async::futures::lock::Mutex;
    use std::collections::VecDeque;
    use std::thread;

    /// A fixed packet length used by all fake devices.
    pub const REPORT_LENGTH: u16 = 64;

    /// A single operation for a fake connection
    #[derive(Debug)]
    enum Operation {
        /// Expect a call to write the specified packet and return success.
        WriteSuccess(Packet),
        /// Expect a call to write the specified packet and return an error.
        WriteFail(Packet),
        /// Expect a call to read a packet and return the supplied data packet.
        ReadSuccess(Packet),
        /// Expect a call to read a packet and return an error.
        ReadFail(),
    }

    /// The mode that a fake connection should operate in.
    #[derive(Debug)]
    enum Mode {
        /// The connection returns errors on all calls.
        Invalid,
        /// The connection potentially returns valid data.
        Valid {
            /// The report descriptor to return.
            report_desc: Bytes,
            /// A queue of expected operations and intended responses.
            operations: Mutex<VecDeque<Operation>>,
        },
    }

    /// A fake implmentation of a `Connection` to a HID device to simplify unit testing.
    ///
    /// `FakeConnections` may either be set to be invalid, in which casse they return errors for
    /// all calls, or valid. A valid `FakeConnection` can perform read and write operations
    /// following an expected set of operations supplied by the test code using it. If a
    /// `FakeConnection` receives requests that do not align with the expection it panics.
    #[derive(Debug)]
    pub struct FakeConnection {
        /// The current operational state of the connection.
        mode: Mode,
    }

    impl FakeConnection {
        /// Constructs a new `FidlConnection` that will return the supplied report_descriptor.
        pub fn new(report_descriptor: &'static [u8]) -> FakeConnection {
            FakeConnection {
                mode: Mode::Valid {
                    report_desc: Bytes::from(report_descriptor),
                    operations: Mutex::new(VecDeque::new()),
                },
            }
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

        /// Enqueues an expectation that write will be called on this connection with the supplied
        /// packet. The connection will return success when this write operation occurs.
        /// Panics if called on a connection that has been set to fail.
        pub fn expect_write(&self, packet: Packet) {
            &self.enqueue(Operation::WriteSuccess(packet));
        }

        /// Enqueues an expectation that write will be called on this connection with the supplied
        /// packet. The connection will return a failure when this write operation occurs.
        /// Panics if called on a connection that has been set to fail.
        pub fn expect_write_error(&self, packet: Packet) {
            &self.enqueue(Operation::WriteFail(packet));
        }

        /// Enqueues an expectation that read will be called on this connection. The connection
        /// will return success and the supplied packet when this read operation occurs.
        /// Panics if called on a connection that has been set to fail.
        pub fn expect_read(&self, packet: Packet) {
            &self.enqueue(Operation::ReadSuccess(packet));
        }

        /// Enqueues an expectation that read will be called on this connection. The connection
        /// will return a failure when this write operation occurs.
        /// Panics if called on a connection that has been set to fail.
        pub fn expect_read_error(&self) {
            &self.enqueue(Operation::ReadFail());
        }

        /// Verified that all expected operations have now been completed.
        /// Panics if this is not true.
        pub fn expect_complete(&self) {
            if let Mode::Valid { operations, .. } = &self.mode {
                let ops = operations.try_lock().unwrap();
                if !ops.is_empty() {
                    panic!(
                        "FakeConnection has {:?} expected operations that were not performed",
                        ops.len()
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
        async fn read_packet(&self) -> Result<Packet, Error> {
            match &self.mode {
                Mode::Valid { operations, .. } => match operations.lock().await.pop_front() {
                    Some(Operation::ReadSuccess(packet)) => Ok(packet),
                    Some(Operation::ReadFail()) => Err(format_err!("Read failing as requested")),
                    _ => panic!("Received unexpected read request"),
                },
                Mode::Invalid => Err(format_err!("Read called on set-to-fail fake connection")),
            }
        }

        async fn write_packet(&self, packet: Packet) -> Result<(), Error> {
            match &self.mode {
                Mode::Valid { operations, .. } => match operations.lock().await.pop_front() {
                    Some(Operation::WriteSuccess(expected)) => {
                        if packet != expected {
                            panic!(
                                "Received write request that did not match expectation:\n\
                                 Expected={:?}\nReceived={:?}",
                                expected, packet
                            );
                        }
                        Ok(())
                    }
                    Some(Operation::WriteFail(expected)) => {
                        if packet != expected {
                            panic!(
                                "Received write request that did not match expectation:\n\
                                 Expected={:?}\nReceived={:?}",
                                expected, packet
                            );
                        }
                        Err(format_err!("Write failing as requested"))
                    }
                    _ => panic!("Received unexpected write request"),
                },
                Mode::Invalid => Err(format_err!("Write called on set-to-fail fake connection")),
            }
        }

        async fn report_descriptor(&self) -> Result<Bytes, Error> {
            match &self.mode {
                Mode::Valid { report_desc, .. } => Ok(Bytes::clone(&report_desc)),
                Mode::Invalid => Err(format_err!("Method called on set-to-fail fake connection")),
            }
        }

        async fn max_packet_length(&self) -> Result<u16, Error> {
            match self.mode {
                Mode::Valid { .. } => Ok(REPORT_LENGTH),
                Mode::Invalid => Err(format_err!("Method called on set-to-fail fake connection")),
            }
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use crate::hid::message::{Command, Packet};
        use fuchsia_async as fasync;
        use lazy_static::lazy_static;

        const TEST_REPORT_DESCRIPTOR: [u8; 8] = [0x06, 0xd0, 0xf1, 0x09, 0x01, 0xa1, 0x01, 0x09];

        lazy_static! {
            static ref TEST_PACKET_1: Packet =
                Packet::initialization(0x12345678, Command::Init, 0, vec![]).unwrap();
            static ref TEST_PACKET_2: Packet =
                Packet::initialization(0x23456789, Command::Wink, 4, vec![0xff, 0xee, 0xdd, 0xcc])
                    .unwrap();
            static ref TEST_PACKET_3: Packet =
                Packet::continuation(0x34567890, 3, vec![0x99, 0x99]).unwrap();
        }

        #[fasync::run_until_stalled(test)]
        async fn test_static_properties() -> Result<(), Error> {
            let connection = FakeConnection::new(&TEST_REPORT_DESCRIPTOR);
            assert_eq!(connection.report_descriptor().await?, &TEST_REPORT_DESCRIPTOR[..]);
            assert_eq!(connection.max_packet_length().await?, REPORT_LENGTH);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn test_read_write() -> Result<(), Error> {
            // Declare expected operations.
            let connection = FakeConnection::new(&TEST_REPORT_DESCRIPTOR);
            connection.expect_write_error(TEST_PACKET_1.clone());
            connection.expect_write(TEST_PACKET_1.clone());
            connection.expect_read_error();
            connection.expect_read(TEST_PACKET_2.clone());
            connection.expect_write(TEST_PACKET_3.clone());
            // Perform operations.
            connection
                .write_packet(TEST_PACKET_1.clone())
                .await
                .expect_err("Write should have failed");
            connection
                .write_packet(TEST_PACKET_1.clone())
                .await
                .expect("Write should have succeeded");
            connection.read_packet().await.expect_err("Read should have failed");
            assert_eq!(
                connection.read_packet().await.expect("Read should have succeeded"),
                *TEST_PACKET_2
            );
            connection
                .write_packet(TEST_PACKET_3.clone())
                .await
                .expect("Write should have succeeded");
            // Verify all expected operations occurred.
            connection.expect_complete();
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        #[should_panic]
        async fn test_write_unexpected_data() {
            let connection = FakeConnection::new(&TEST_REPORT_DESCRIPTOR);
            connection.expect_write(TEST_PACKET_1.clone());
            connection.write_packet(TEST_PACKET_2.clone()).await.unwrap();
        }

        #[fasync::run_until_stalled(test)]
        #[should_panic]
        async fn test_write_when_expecting_read() {
            let connection = FakeConnection::new(&TEST_REPORT_DESCRIPTOR);
            connection.expect_read(TEST_PACKET_1.clone());
            connection.write_packet(TEST_PACKET_1.clone()).await.unwrap();
        }

        #[fasync::run_until_stalled(test)]
        #[should_panic]
        async fn test_read_when_expecting_write() {
            let connection = FakeConnection::new(&TEST_REPORT_DESCRIPTOR);
            connection.expect_write(TEST_PACKET_1.clone());
            connection.read_packet().await.unwrap();
        }

        #[fasync::run_until_stalled(test)]
        #[should_panic]
        async fn test_incomplete_operations() {
            let connection = FakeConnection::new(&TEST_REPORT_DESCRIPTOR);
            connection.expect_write(TEST_PACKET_1.clone());
            // Dropping the connection should verify all expected operations are complete.
        }

        #[fasync::run_until_stalled(test)]
        async fn test_invalid() -> Result<(), Error> {
            let mut connection = FakeConnection::new(&TEST_REPORT_DESCRIPTOR);
            connection.report_descriptor().await.expect("Should have initially suceeded");
            connection.error();
            connection.report_descriptor().await.expect_err("Should have failed to get descriptor");
            connection.max_packet_length().await.expect_err("Should have failed to get packet len");
            connection.read_packet().await.expect_err("Should have failed to read packet");
            connection
                .write_packet(TEST_PACKET_1.clone())
                .await
                .expect_err("Should have failed to write packet");
            Ok(())
        }
    }
}
