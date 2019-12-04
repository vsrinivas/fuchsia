// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::types::Celsius;
use async_trait::async_trait;
use failure::{format_err, Error};
use fidl_fuchsia_hardware_thermal as fthermal;
use fuchsia_zircon as zx;
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
///     - fidl_fuchsia_hardware_thermal: used by this node to query the thermal driver specified by
///       "driver_path" in the TemperatureHandler constructor

pub struct TemperatureHandler {
    driver_path: String,
    driver_proxy: fthermal::DeviceProxy,
}

impl TemperatureHandler {
    pub fn new(driver_path: String) -> Result<Rc<Self>, Error> {
        let proxy = Self::connect_driver(&driver_path)?;
        Ok(Self::new_with_proxy(driver_path, proxy))
    }

    fn new_with_proxy(driver_path: String, driver_proxy: fthermal::DeviceProxy) -> Rc<Self> {
        Rc::new(Self { driver_path, driver_proxy })
    }

    fn connect_driver(path: &String) -> Result<fthermal::DeviceProxy, Error> {
        let (proxy, server) = fidl::endpoints::create_proxy::<fthermal::DeviceMarker>()
            .map_err(|e| format_err!("Failed to create thermal proxy: {}", e))?;

        fdio::service_connect(path, server.into_channel())
            .map_err(|s| format_err!("Failed to connect to service at {}: {}", path, s))?;
        Ok(proxy)
    }

    async fn handle_read_temperature(&self) -> Result<MessageReturn, Error> {
        // TODO(pshickel): What if multiple other nodes want to know about the current
        // temperature from the same driver? If two requests come back to back, does it mean we
        // should blindly query the driver twice, or maybe we want to cache the last value with
        // some staleness tolerance parameter? There isn't a use-case for this yet, but it's
        // something to think about.
        let (status, temperature) =
            self.driver_proxy.get_temperature_celsius().await.map_err(|e| {
                format_err!("{}: get_temperature_celsius IPC failed: {}", self.driver_path, e)
            })?;
        zx::Status::ok(status).map_err(|e| {
            format_err!(
                "{}: get_temperature_celsius driver returned error: {}",
                self.driver_path,
                e
            )
        })?;
        Ok(MessageReturn::ReadTemperature(Celsius(temperature as f64)))
    }
}

#[async_trait(?Send)]
impl Node for TemperatureHandler {
    fn name(&self) -> &'static str {
        "TemperatureHandler"
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, Error> {
        match msg {
            Message::ReadTemperature => self.handle_read_temperature().await,
            _ => Err(format_err!("Unsupported message: {:?}", msg)),
        }
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use futures::TryStreamExt;

    // Spawns a new task that acts as a fake thermal driver for testing purposes. The driver only
    // handles requests for GetTemperatureCelsius - trying to send any other requests to it is a
    // bug. Each GetTemperatureCelsius request responds with the next element from
    // `temperature_readings`, which wraps back around at the end of the vector.
    fn setup_fake_driver(temperature_readings: Vec<f32>) -> fthermal::DeviceProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fthermal::DeviceMarker>().unwrap();
        let mut temperaure_index = 0;

        fasync::spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fthermal::DeviceRequest::GetTemperatureCelsius { responder }) => {
                        let _ = responder.send(
                            zx::Status::OK.into_raw(),
                            temperature_readings[temperaure_index],
                        );
                        temperaure_index = (temperaure_index + 1) % temperature_readings.len();
                    }
                    _ => assert!(false),
                }
            }
        });

        proxy
    }

    pub fn setup_test_node(temperature_readings: Vec<f32>) -> Rc<TemperatureHandler> {
        TemperatureHandler::new_with_proxy(
            "Fake".to_string(),
            setup_fake_driver(temperature_readings),
        )
    }

    /// Tests that the node can handle the 'ReadTemperature' message as expected. The test
    /// checks for the expected temperature value which is returned by the fake thermal driver.
    #[fasync::run_singlethreaded(test)]
    async fn test_read_temperature() {
        let temperature_readings = vec![1.2, 3.4, 5.6, 7.8, 9.0];
        let node = setup_test_node(temperature_readings.to_vec());

        // send ReadTemperature message and check for expected value
        for temperature_reading in temperature_readings {
            let result = node.handle_message(&Message::ReadTemperature).await;
            let temperature = result.unwrap();
            if let MessageReturn::ReadTemperature(t) = temperature {
                assert_eq!(t.0, temperature_reading as f64);
            } else {
                assert!(false);
            }
        }
    }

    /// Tests that an unsupported message is handled gracefully and an error is returned.
    #[fasync::run_singlethreaded(test)]
    async fn test_unsupported_msg() {
        let node = setup_test_node(vec![]);
        let message = Message::GetTotalCpuLoad;
        let result = node.handle_message(&message).await;
        assert!(result.is_err());
    }
}
