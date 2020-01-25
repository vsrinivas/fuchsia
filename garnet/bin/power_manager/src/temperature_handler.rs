// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::log_if_err;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::types::Celsius;
use crate::utils::connect_proxy;
use anyhow::{format_err, Error};
use async_trait::async_trait;
use fidl_fuchsia_hardware_thermal as fthermal;
use fuchsia_syslog::fx_log_err;
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
///     - fuchsia.hardware.thermal: the node uses this protocol to query the thermal driver
///       specified by `driver_path` in the TemperatureHandler constructor

/// A builder for constructing the TemperatureHandler node
pub struct TemperatureHandlerBuilder {
    driver_path: String,
    driver_proxy: Option<fthermal::DeviceProxy>,
}

impl TemperatureHandlerBuilder {
    pub fn new_with_driver_path(driver_path: String) -> Self {
        Self { driver_path, driver_proxy: None }
    }

    #[cfg(test)]
    pub fn new_with_proxy(driver_path: String, proxy: fthermal::DeviceProxy) -> Self {
        Self { driver_path, driver_proxy: Some(proxy) }
    }

    pub fn build(self) -> Result<Rc<TemperatureHandler>, Error> {
        // Get default proxy if necessary
        let proxy = if self.driver_proxy.is_none() {
            connect_proxy::<fthermal::DeviceMarker>(&self.driver_path)?
        } else {
            self.driver_proxy.unwrap()
        };

        Ok(Rc::new(TemperatureHandler { driver_path: self.driver_path, driver_proxy: proxy }))
    }
}

pub struct TemperatureHandler {
    driver_path: String,
    driver_proxy: fthermal::DeviceProxy,
}

impl TemperatureHandler {
    async fn handle_read_temperature(&self) -> Result<MessageReturn, Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "TemperatureHandler::handle_read_temperature",
            "driver" => self.driver_path.as_str()
        );
        // TODO(pshickel): What if multiple other nodes want to know about the current
        // temperature from the same driver? If two requests come back to back, does it mean we
        // should blindly query the driver twice, or maybe we want to cache the last value with
        // some staleness tolerance parameter? There isn't a use-case for this yet, but it's
        // something to think about.
        let result = self.read_temperature().await;
        log_if_err!(
            result,
            format!("Failed to read temperature from {}", self.driver_path).as_str()
        );
        fuchsia_trace::instant!(
            "power_manager",
            "TemperatureHandler::read_temperature_result",
            fuchsia_trace::Scope::Thread,
            "driver" => self.driver_path.as_str(),
            "result" => format!("{:?}", result).as_str()
        );
        Ok(MessageReturn::ReadTemperature(result?))
    }

    async fn read_temperature(&self) -> Result<Celsius, Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "TemperatureHandler::read_temperature",
            "driver" => self.driver_path.as_str()
        );
        let (status, temperature) =
            self.driver_proxy.get_temperature_celsius().await.map_err(|e| {
                format_err!(
                    "{} ({}): get_temperature_celsius IPC failed: {}",
                    self.name(),
                    self.driver_path,
                    e
                )
            })?;
        zx::Status::ok(status).map_err(|e| {
            format_err!(
                "{} ({}): get_temperature_celsius driver returned error: {}",
                self.name(),
                self.driver_path,
                e
            )
        })?;
        Ok(Celsius(temperature.into()))
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

    /// Spawns a new task that acts as a fake thermal driver for testing purposes. The driver only
    /// handles requests for GetTemperatureCelsius - trying to send any other requests to it is a
    /// bug. Each GetTemperatureCelsius responds with a value provided by the supplied
    /// `get_temperature` closure.
    fn setup_fake_driver(
        mut get_temperature: impl FnMut() -> Celsius + 'static,
    ) -> fthermal::DeviceProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fthermal::DeviceMarker>().unwrap();
        fasync::spawn_local(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fthermal::DeviceRequest::GetTemperatureCelsius { responder }) => {
                        let _ =
                            responder.send(zx::Status::OK.into_raw(), get_temperature().0 as f32);
                    }
                    _ => assert!(false),
                }
            }
        });

        proxy
    }

    /// Sets up a test TemperatureHandler node that receives temperature readings from the
    /// provided closure.
    pub fn setup_test_node(
        get_temperature: impl FnMut() -> Celsius + 'static,
    ) -> Rc<TemperatureHandler> {
        TemperatureHandlerBuilder::new_with_proxy(
            "Fake".to_string(),
            setup_fake_driver(get_temperature),
        )
        .build()
        .unwrap()
    }

    /// Tests that the node can handle the 'ReadTemperature' message as expected. The test
    /// checks for the expected temperature value which is returned by the fake thermal driver.
    #[fasync::run_singlethreaded(test)]
    async fn test_read_temperature() {
        // Readings for the fake temperature driver.
        let temperature_readings = vec![1.2, 3.4, 5.6, 7.8, 9.0];

        // Readings piped through the fake driver will be cast to f32 and back to f64.
        let expected_readings: Vec<f64> =
            temperature_readings.iter().map(|x| *x as f32 as f64).collect();

        // Each ReadTemperature request will respond with the next element from
        // `temperature_readings`, wrapping back around when the end of the vector is reached.
        let mut index = 0;
        let get_temperature = move || {
            let value = temperature_readings[index];
            index = (index + 1) % temperature_readings.len();
            Celsius(value)
        };
        let node = setup_test_node(get_temperature);

        // Send ReadTemperature message and check for expected value.
        for expected_reading in expected_readings {
            let result = node.handle_message(&Message::ReadTemperature).await;
            let temperature = result.unwrap();
            if let MessageReturn::ReadTemperature(t) = temperature {
                assert_eq!(t.0, expected_reading);
            } else {
                assert!(false);
            }
        }
    }

    /// Tests that an unsupported message is handled gracefully and an error is returned.
    #[fasync::run_singlethreaded(test)]
    async fn test_unsupported_msg() {
        let node = setup_test_node(|| Celsius(0.0));
        let message = Message::GetTotalCpuLoad;
        let result = node.handle_message(&message).await;
        assert!(result.is_err());
    }
}
