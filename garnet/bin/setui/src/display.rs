// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use self::display_fidl_handler::fidl_io;
pub use self::light_sensor_controller::LIGHT_SENSOR_SERVICE_NAME;

pub mod display_configuration;
pub mod display_controller;
mod display_fidl_handler;
mod light_sensor;
mod light_sensor_config;
pub mod light_sensor_controller;
pub mod types;

pub use light_sensor_config::LightSensorConfig;

#[cfg(test)]
pub(crate) use light_sensor::testing as light_sensor_testing;
