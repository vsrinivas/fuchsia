// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::types::Celsius;
use async_trait::async_trait;
use failure::{format_err, Error};
use fidl_fuchsia_hardware_thermal as fthermal;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use std::cell::RefCell;
use std::collections::HashMap;
use std::rc::Rc;

/// Node: TemperatureHandler
///
/// Summary: Responds to temperature requests from other nodes by polling the specified driver
///          using the thermal FIDL protocol
///
/// Handles Messages:
///     - ReadTemperature
///
/// Sends Messages: N/A
///
/// FIDL dependencies:
///     - fidl_fuchsia_hardware_thermal: used by this node to query the thermal driver.

pub struct TemperatureHandler {
    drivers: RefCell<HashMap<String, fthermal::DeviceProxy>>,
}

impl TemperatureHandler {
    pub fn new() -> Rc<Self> {
        Rc::new(Self { drivers: RefCell::new(HashMap::new()) })
    }

    fn connect_driver(&self, path: &str) -> Result<fthermal::DeviceProxy, Error> {
        let (client, server) =
            zx::Channel::create().map_err(|s| format_err!("Failed to create channel: {}", s))?;

        fdio::service_connect(path, server)
            .map_err(|s| format_err!("Failed to connect to service at {}: {}", path, s))?;

        let driver = fthermal::DeviceProxy::new(fasync::Channel::from_channel(client)?);
        Ok(driver)
    }

    async fn handle_read_temperature(&self, driver_path: &str) -> Result<MessageReturn, Error> {
        // TODO(pshickel): What if multiple other nodes want to know about the current
        // temperature from the same driver? If two requests come back to back, does it mean we
        // should blindly query the driver twice, or maybe we want to cache the last value with
        // some staleness tolerance parameter? There isn't a use-case for this yet, but it's
        // something to think about.
        let mut drivers = self.drivers.borrow_mut();
        let mut driver = drivers.get(driver_path);
        if driver.is_none() {
            let driver_connection = self.connect_driver(driver_path)?;
            drivers.insert(String::from(driver_path), driver_connection);
            driver = drivers.get(driver_path);
        }

        let temperature = self.read_temperature(driver.unwrap()).await?;
        Ok(MessageReturn::ReadTemperature(temperature))
    }

    async fn read_temperature(&self, driver: &fthermal::DeviceProxy) -> Result<Celsius, Error> {
        let (status, temperature) = driver
            .get_temperature_celsius()
            .await
            .map_err(|e| format_err!("get_temperature_celsius IPC failed: {}", e))?;
        zx::Status::ok(status)
            .map_err(|e| format_err!("get_temperature_celsius driver returned error: {}", e))?;
        Ok(Celsius(temperature))
    }
}

#[async_trait(?Send)]
impl Node for TemperatureHandler {
    fn name(&self) -> &'static str {
        "TemperatureHandler"
    }

    async fn handle_message(&self, msg: &Message<'_>) -> Result<MessageReturn, Error> {
        match msg {
            Message::ReadTemperature(driver) => self.handle_read_temperature(driver).await,
            _ => Err(format_err!("Unsupported message: {:?}", msg)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::RequestStream;
    use fuchsia_async as fasync;
    use futures::TryStreamExt;

    // Spawns a new task that acts as a fake thermal driver for testing purposes. The driver only
    // handles requests for GetTemperatureCelsius - trying to send any other requests to it is a
    // bug. Responds with a specified temperature that is verified in the test.
    fn setup_fake_driver(mut stream: fthermal::DeviceRequestStream, temperature: f32) {
        fasync::spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fthermal::DeviceRequest::GetTemperatureCelsius { responder }) => {
                        let _ = responder.send(zx::Status::OK.into_raw(), temperature);
                    }
                    _ => assert!(false),
                }
            }
        });
    }

    /// Tests that the node can handle the 'ReadTemperature' message as expected. The test
    /// checks for the expected temperature value which is returned by the fake thermal driver.
    #[fasync::run_singlethreaded(test)]
    async fn test_read_temperature() {
        let test_temperature = 12.34;
        let driver_path = "/dev/class/thermal/fake";
        let node = TemperatureHandler::new();

        // setup fake driver connection
        let (client, server) = zx::Channel::create().unwrap();
        let client = fasync::Channel::from_channel(client).unwrap();
        let server = fasync::Channel::from_channel(server).unwrap();
        let driver = fthermal::DeviceProxy::new(client);
        let stream = fthermal::DeviceRequestStream::from_channel(server);
        setup_fake_driver(stream, test_temperature);
        node.drivers.borrow_mut().insert(String::from(driver_path), driver);

        // send ReadTemperature message and check for expected value
        let message = Message::ReadTemperature(driver_path);
        let result = node.handle_message(&message).await;
        let temperature = result.unwrap();
        if let MessageReturn::ReadTemperature(t) = temperature {
            assert_eq!(t.0, test_temperature);
        } else {
            assert!(false);
        }
    }

    /// Tests that an invalid argument to the 'ReadTemperature' message is handled gracefully
    /// and an error is returned.
    #[fasync::run_singlethreaded(test)]
    async fn test_read_temperature_bad_arg() {
        let thermal_driver = "";
        let node = TemperatureHandler::new();
        let message = Message::ReadTemperature(thermal_driver);
        let result = node.handle_message(&message).await;
        assert!(result.is_err());
    }

    /// Tests that an unsupported message is handled gracefully and an error is returned.
    #[fasync::run_singlethreaded(test)]
    async fn test_unsupported_msg() {
        let node = TemperatureHandler::new();
        let message = Message::GetTotalCpuLoad;
        let result = node.handle_message(&message).await;
        assert!(result.is_err());
    }
}
