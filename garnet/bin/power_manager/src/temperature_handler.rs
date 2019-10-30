// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::types::Celsius;
use async_trait::async_trait;
use failure::{format_err, Error};
use fuchsia_zircon as zx;
use std::cell::RefCell;
use std::collections::HashMap;
use std::rc::Rc;

/// Node: TemperatureHandler
///
/// Summary: responds to temperature requests from other nodes by polling the
///          specified driver using the thermal FIDL protocol
///
/// Message Inputs:
///     - ReadTemperature
///
/// Message Outputs: N/A
///
/// FIDL: fidl_fuchsia_hardware_thermal

pub struct TemperatureHandler {
    drivers: RefCell<HashMap<String, zx::Channel>>,
}

impl TemperatureHandler {
    pub fn new() -> Rc<Self> {
        Rc::new(Self { drivers: RefCell::new(HashMap::new()) })
    }

    fn connect_driver(&self, _path: &str) -> Result<(), Error> {
        // TODO(pshickel): connect to driver and insert into hashmap
        Err(format_err!("Failed to connect to driver"))
    }

    fn handle_read_temperature(&self, driver_path: &str) -> Result<MessageReturn, Error> {
        let drivers = self.drivers.borrow();
        if !drivers.contains_key(driver_path) {
            self.connect_driver(driver_path)?;
        }

        let driver = drivers.get(driver_path).unwrap();
        let temperature = self.read_temperature(driver)?;
        Ok(MessageReturn::ReadTemperature(temperature))
    }

    fn read_temperature(&self, _driver: &zx::Channel) -> Result<Celsius, Error> {
        // TODO(pshickel): implement this function once the temperature driver is ready
        Ok(Celsius(0.0))
    }
}

#[async_trait(?Send)]
impl Node for TemperatureHandler {
    fn name(&self) -> &'static str {
        "TemperatureHandler"
    }

    async fn handle_message(&self, msg: &Message<'_>) -> Result<MessageReturn, Error> {
        match msg {
            Message::ReadTemperature(driver) => self.handle_read_temperature(driver),
            _ => Err(format_err!("Unsupported message: {:?}", msg)),
        }
    }
}
