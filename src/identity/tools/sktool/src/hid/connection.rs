// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_trait::async_trait;
use bytes::Bytes;
use failure::Error;
use std::fmt::Debug;

/// A basic connection to a HID device.
#[async_trait]
pub trait Connection: Sized + Debug {
    // TODO(jsankey): Add methods to send and receive packets.

    /// Returns the report descriptor for this device.
    async fn report_descriptor(&self) -> Result<Bytes, Error>;

    /// Returns the maximum size of packets this device can read.
    async fn max_packet_size(&self) -> Result<u16, Error>;
}

/// An implementation of a `Connection` over the FIDL `fuchsia.hardware.input.Device` protocol.
pub mod fidl {
    use super::Connection;
    use async_trait::async_trait;
    use bytes::Bytes;
    use failure::{format_err, Error};
    use fidl_fuchsia_hardware_input::DeviceProxy;

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
    }

    #[async_trait]
    impl Connection for FidlConnection {
        async fn report_descriptor(&self) -> Result<Bytes, Error> {
            self.proxy
                .get_report_desc()
                .await
                .map(|vec| Bytes::from(vec))
                .map_err(|err| format_err!("FIDL error: {:?}", err))
        }

        async fn max_packet_size(&self) -> Result<u16, Error> {
            self.proxy
                .get_max_input_report_size()
                .await
                .map_err(|err| format_err!("FIDL error: {:?}", err))
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use fidl::endpoints::{create_proxy_and_stream, RequestStream};
        use fidl_fuchsia_hardware_input::{DeviceMarker, DeviceRequest};
        use fuchsia_async as fasync;
        use futures::TryStreamExt;

        const TEST_REPORT_SIZE: u16 = 99;
        const TEST_REPORT_DESCRIPTOR: [u8; 8] = [0x06, 0xd0, 0xf1, 0x09, 0x01, 0xa1, 0x01, 0x09];

        /// Creates a fake device proxy that will respond with the test constants.
        fn valid_fake_device_proxy() -> DeviceProxy {
            let (device_proxy, mut stream) = create_proxy_and_stream::<DeviceMarker>()
                .expect("Failed to create proxy and stream");
            fasync::spawn(async move {
                while let Some(req) = stream.try_next().await.expect("Failed to read req") {
                    match req {
                        DeviceRequest::GetReportDesc { responder } => {
                            let response = TEST_REPORT_DESCRIPTOR.to_vec();
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
            });
            device_proxy
        }

        /// Creates a fake device proxy that will immediately close the channel.
        fn invalid_fake_device_proxy() -> DeviceProxy {
            let (device_proxy, stream) = create_proxy_and_stream::<DeviceMarker>()
                .expect("Failed to create proxy and stream");
            stream.control_handle().shutdown();
            device_proxy
        }

        #[fasync::run_until_stalled(test)]
        async fn test_valid() -> Result<(), Error> {
            let connection = FidlConnection::new(valid_fake_device_proxy());
            assert_eq!(connection.report_descriptor().await?, &TEST_REPORT_DESCRIPTOR[..]);
            assert_eq!(connection.max_packet_size().await?, TEST_REPORT_SIZE);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn test_fidl_error() -> Result<(), Error> {
            let connection = FidlConnection::new(invalid_fake_device_proxy());
            connection.report_descriptor().await.expect_err("Should have failed to get descriptor");
            connection.max_packet_size().await.expect_err("Should have failed to get packet size");
            Ok(())
        }
    }
}

/// A fake implementation of a `Connection` to simplify unit testing.
#[cfg(test)]
pub mod fake {
    use super::Connection;
    use async_trait::async_trait;
    use bytes::Bytes;
    use failure::{format_err, Error};

    /// A fixed packet size used by all devices.
    const REPORT_SIZE: u16 = 64;

    /// A fake connection to a HID device.
    #[derive(Debug)]
    pub enum FakeConnection {
        /// A connection that returns errors on all calls.
        Invalid,
        /// A connection that returns valid data.
        Valid { report_desc: Bytes },
    }

    impl FakeConnection {
        /// Constructs a new `FidlConnection` that will return the supplied report_descriptor.
        pub fn new(report_descriptor: &'static [u8]) -> FakeConnection {
            FakeConnection::Valid { report_desc: Bytes::from(report_descriptor) }
        }

        /// Constructs a new `FidlConnection` that will return errors for all calls.
        pub fn failing() -> FakeConnection {
            FakeConnection::Invalid
        }
    }

    #[async_trait]
    impl Connection for FakeConnection {
        async fn report_descriptor(&self) -> Result<Bytes, Error> {
            match self {
                FakeConnection::Valid { report_desc: rd } => Ok(Bytes::clone(&rd)),
                FakeConnection::Invalid => {
                    Err(format_err!("Method called on always-failing fake connection"))
                }
            }
        }

        async fn max_packet_size(&self) -> Result<u16, Error> {
            match self {
                FakeConnection::Valid { .. } => Ok(REPORT_SIZE),
                FakeConnection::Invalid => {
                    Err(format_err!("Method called on always-failing fake connection"))
                }
            }
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use fuchsia_async as fasync;

        const TEST_REPORT_DESCRIPTOR: [u8; 8] = [0x06, 0xd0, 0xf1, 0x09, 0x01, 0xa1, 0x01, 0x09];

        #[fasync::run_until_stalled(test)]
        async fn test_valid() -> Result<(), Error> {
            let connection = FakeConnection::new(&TEST_REPORT_DESCRIPTOR);
            assert_eq!(connection.report_descriptor().await?, &TEST_REPORT_DESCRIPTOR[..]);
            assert_eq!(connection.max_packet_size().await?, REPORT_SIZE);
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn test_invalid() -> Result<(), Error> {
            let connection = FakeConnection::failing();
            connection.report_descriptor().await.expect_err("Should have failed to get descriptor");
            connection.max_packet_size().await.expect_err("Should have failed to get packet size");
            Ok(())
        }
    }
}
