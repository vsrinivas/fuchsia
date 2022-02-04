// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{ensure, Context as _, Error};
use serde_derive::Deserialize;
use std::collections::{HashMap, HashSet};
use std::fs::File;
use std::io::Read as _;
use std::path::Path;

/// This library is used to parse a thermal configuration JSON file into a data structure which also
/// implements some convenience methods for accessing and consuming the data.
///
/// The intended usage is that `ThermalConfig::read()` is called with a thermal configuration JSON
/// file path. If successful, the function returns a ThermalConfig instance containing the parsed
/// config data.
///
/// The parser expects a JSON5 file of the following format:
///     {
///         clients: {
///             audio: [
///                 {
///                     state: 1,
///                     trip_points: [
///                         {
///                             sensor: '/dev/sys/platform/05:03:a/thermal',
///                             activate_at: 75,
///                             deactivate_below: 71,
///                         },
///                     ],
///                 },
///                 {
///                     state: 2,
///                     trip_points: [
///                         {
///                             sensor: '/dev/sys/platform/05:03:a/thermal',
///                             activate_at: 86,
///                             deactivate_below: 82,
///                         },
///                     ],
///                 },
///             ],
///         },
///     }

/// Represents the top level of a thermal configuration structure.
#[derive(Deserialize, Debug)]
pub struct ThermalConfig {
    /// Maps the name of a client (e.g., "audio") to its corresponding configuration.
    clients: HashMap<String, ClientConfig>,
}

/// Defines the configuration for a single client (made up of a vector of `StateConfig` instances).
#[derive(Deserialize, Debug)]
pub struct ClientConfig(Vec<StateConfig>);

/// Defines the configuration for a single thermal state. Together with other `StateConfig`
/// instances, this makes up a client's `ClientConfig`.
#[derive(Deserialize, Debug)]
pub struct StateConfig {
    /// Thermal state number.
    pub state: u32,

    /// Vector of trip points that will activate this state.
    pub trip_points: Vec<TripPoint>,
}

/// Defines a trip point with hysteresis for a specific temperature sensor.
#[derive(Deserialize, Debug, Clone)]
pub struct TripPoint {
    /// Topological path to the temperature sensor.
    pub sensor: String,

    /// Temperature at which this trip point becomes active.
    pub activate_at: u32,

    /// Temperature below which this trip point becomes inactive.
    pub deactivate_below: u32,
}

impl TripPoint {
    /// Creates a new trip point.
    ///
    /// Note: this is only intended for use in tests. However, it isn't marked as cfg(test) so that
    /// code outside of the library can use it in their tests as well.
    pub fn new(sensor: &str, deactivate_below: u32, activate_at: u32) -> Self {
        Self { sensor: sensor.into(), deactivate_below, activate_at }
    }
}

impl ThermalConfig {
    /// Creates a new, empty ThermalConfig instance.
    ///
    /// An empty ThermalConfig instance corresponds to the case where no client has specified a
    /// thermal trip point configuration.
    ///
    /// Note: this is only intended for use in tests. However, it isn't marked as cfg(test) so that
    /// code outside of the library can use it in their tests as well.
    pub fn new() -> Self {
        Self { clients: HashMap::new() }
    }

    /// Read the supplied JSON file path and parse into a ThermalConfig instance.
    ///
    /// Attempts to open, read, and parse the supplied JSON file into a valid ThermalConfig
    /// instance. If a ThermalConfig instance could be created with the JSON configuration, then it
    /// is also tested for validity. If a ThermalConfig instance could not be created, or validation
    /// fails, then an error is returned.
    pub fn read(json_file_path: &Path) -> Result<ThermalConfig, Error> {
        let mut buffer = String::new();
        File::open(&json_file_path)?.read_to_string(&mut buffer)?;

        let config = serde_json5::from_str::<ThermalConfig>(&buffer)?;
        config.validate()?;
        Ok(config)
    }

    /// Validates the thermal configuration.
    pub fn validate(&self) -> Result<(), Error> {
        // Iterate and validate each underlying ClientConfig instance
        for (client_name, client_config) in self.clients.iter() {
            client_config
                .validate()
                .context(format!("Validation failed for client {}", client_name))?;
        }

        Ok(())
    }

    /// Adds a configuration entry for the specified client.
    ///
    /// Note: this is only intended for use in tests. However, it isn't marked as cfg(test) so that
    /// code outside of the library can use it in their tests as well.
    pub fn add_client_config(mut self, client: &str, config: ClientConfig) -> Self {
        self.clients.insert(client.to_string(), config);
        self
    }

    /// Gets the ClientConfig instance for the specified client.
    pub fn get_client_config(&self, client: &String) -> Option<&ClientConfig> {
        self.clients.get(client)
    }

    pub fn into_iter(self) -> impl Iterator<Item = (String, ClientConfig)> {
        self.clients.into_iter()
    }
}

impl ClientConfig {
    /// Creates a new empty ClientConfig.
    ///
    /// Note: this is only intended for use in tests. However, it isn't marked as cfg(test) so that
    /// code outside of the library can use it in their tests as well.
    pub fn new() -> Self {
        Self(vec![])
    }

    /// Adds a new thermal state (defined by the supplied trip points) to the client config.
    ///
    /// This will create a new thermal state using the supplied trip points, assigning it a valid
    /// thermal state number (which is equal to the number of existing thermal states plus one).
    ///
    /// Note: this is only intended for use in tests. However, it isn't marked as cfg(test) so that
    /// code outside of the library can use it in their tests as well.
    pub fn add_thermal_state(mut self, trip_points: Vec<TripPoint>) -> Self {
        self.0.push(StateConfig { state: self.0.len() as u32 + 1, trip_points });
        self
    }

    /// Validates the client config.
    ///
    /// Performs a series of validations to check if the configuration defined by this
    /// `ClientConfig` instance is valid. The instance is valid if:
    ///     1) thermal state numbers are monotonically increasing starting at 1
    ///     2) trip points are well-formed (`deactivate_below` <= `activate_at`)
    ///     3) for a given sensor, the [`deactivate_below`..=`activate_at`] range is strictly
    ///        increasing and non-overlapping for successive trip points
    ///     4) the same sensor is not referenced by multiple trip points in the same thermal state
    fn validate(&self) -> Result<(), Error> {
        // Ensure state numbers are monotonically increasing starting at 1
        {
            let state_numbers: Vec<_> = self.0.iter().map(|s| s.state).collect();
            let expected_state_numbers: Vec<_> = (1..self.0.len() as u32 + 1).collect();
            ensure!(
                state_numbers == expected_state_numbers,
                "State numbers must increase monotonically starting at 1 \
                (got invalid state numbers: {:?})",
                state_numbers
            );
        }

        // Ensure:
        //  1) trip points are well-formed (`deactivate_below` <= `activate_at`)
        //  2) for a given sensor, the [`deactivate_below`..=`activate_at`] range is strictly
        //     increasing and non-overlapping for successive trip points
        //  3) the same sensor is not referenced by multiple trip points in the same thermal state
        {
            // This map will be used to keep track of the highest encountered `activate_at` value
            // for each sensor as we iterate through the trip points. This will let us detect if any
            // trip points are overlapping or not increasing.
            let mut highest_activate_value: HashMap<String, u32> = HashMap::new();

            for state_config in self.0.iter() {
                // This set will be used to determine which sensors have already had trip points
                // added for a given thermal state. This will let us detect if a sensor is
                // referenced by multiple trip points in the same thermal state.
                let mut sensors_configured_for_state = HashSet::new();

                for tp in state_config.trip_points.iter() {
                    ensure!(
                        sensors_configured_for_state.insert(&tp.sensor) == true,
                        "A sensor cannot be referenced by multiple trip points in the same thermal \
                        state (violated by sensor {} in state {})",
                        tp.sensor,
                        state_config.state
                    );

                    ensure!(
                        tp.activate_at >= tp.deactivate_below,
                        "activate_at must be greater or equal to deactivate_below \
                        (invalid for state {}: activate_at={}; deactivate_below={}",
                        state_config.state,
                        tp.activate_at,
                        tp.deactivate_below
                    );

                    // If we've already encountered a trip point for this sensor, make sure the new
                    // trip point has a `deactivate_below` value that is greater than the previously
                    // observed highest activate_at value. Otherwise, the new trip point will be
                    // overlapping (or otherwise non-increasing) with a previously observed trip
                    // point.
                    if let Some(activate_value) = highest_activate_value.get_mut(&tp.sensor) {
                        ensure!(
                            tp.deactivate_below > *activate_value,
                            "Trip point ranges must not overlap (range for state {} ({} - {}) \
                            overlaps with previously specified range for sensor {})",
                            state_config.state,
                            tp.deactivate_below,
                            tp.activate_at,
                            tp.sensor
                        );
                        *activate_value = tp.activate_at;
                    } else {
                        highest_activate_value.insert(tp.sensor.clone(), tp.activate_at);
                    }
                }
            }
        }

        Ok(())
    }

    /// Gets the thermal states that make up this client configuration.
    pub fn into_thermal_states(self) -> Vec<StateConfig> {
        self.0
    }
}

#[cfg(test)]
mod tests {
    use crate::*;
    use assert_matches::assert_matches;

    /// Tests that valid ThermalConfig instances pass the validation.
    #[test]
    fn test_thermal_config_validation_success() {
        // Basic, empty thermal config
        let thermal_config = ThermalConfig::new();
        assert_matches!(thermal_config.validate(), Ok(()));

        // Multiple clients, each with multiple thermal states consisting of multiple trip points
        let thermal_config = ThermalConfig::new()
            .add_client_config(
                "client1",
                ClientConfig::new()
                    .add_thermal_state(vec![
                        TripPoint::new("sensor1", 1, 1),
                        TripPoint::new("sensor2", 1, 1),
                    ])
                    .add_thermal_state(vec![
                        TripPoint::new("sensor1", 2, 2),
                        TripPoint::new("sensor2", 2, 2),
                    ]),
            )
            .add_client_config(
                "client2",
                ClientConfig::new()
                    .add_thermal_state(vec![
                        TripPoint::new("sensor1", 1, 1),
                        TripPoint::new("sensor2", 1, 1),
                    ])
                    .add_thermal_state(vec![
                        TripPoint::new("sensor1", 2, 2),
                        TripPoint::new("sensor2", 2, 2),
                    ]),
            );
        assert_matches!(thermal_config.validate(), Ok(()));
    }

    /// Tests that invalid ClientConfig instances fail the validation.
    #[test]
    fn test_thermal_config_validation_failures() {
        // Decreasing thermal state numbers
        let thermal_config = ThermalConfig::new().add_client_config(
            "client1",
            ClientConfig(vec![
                StateConfig { state: 2, trip_points: vec![] },
                StateConfig { state: 1, trip_points: vec![] },
            ]),
        );
        assert_matches!(thermal_config.validate(), Err(_));

        // Repeated thermal state numbers
        let thermal_config = ThermalConfig::new().add_client_config(
            "client1",
            ClientConfig(vec![
                StateConfig { state: 1, trip_points: vec![] },
                StateConfig { state: 1, trip_points: vec![] },
            ]),
        );
        assert_matches!(thermal_config.validate(), Err(_));

        // Thermal state numbers below 1
        let thermal_config = ThermalConfig::new().add_client_config(
            "client1",
            ClientConfig(vec![
                StateConfig { state: 0, trip_points: vec![] },
                StateConfig { state: 1, trip_points: vec![] },
            ]),
        );
        assert_matches!(thermal_config.validate(), Err(_));

        // Trip point with deactivate_below > activate_at
        let thermal_config = ThermalConfig::new().add_client_config(
            "client1",
            ClientConfig::new().add_thermal_state(vec![TripPoint::new("sensor1", 10, 8)]),
        );
        assert_matches!(thermal_config.validate(), Err(_));

        // Repeated sensor for a given thermal state
        let thermal_config = ThermalConfig::new().add_client_config(
            "client1",
            ClientConfig::new().add_thermal_state(vec![
                TripPoint::new("sensor1", 5, 6),
                TripPoint::new("sensor1", 4, 5),
            ]),
        );
        assert_matches!(thermal_config.validate(), Err(_));

        // Thermal states with overlapping trip points
        let thermal_config = ThermalConfig::new().add_client_config(
            "client1",
            ClientConfig::new()
                .add_thermal_state(vec![TripPoint::new("sensor1", 1, 2)])
                .add_thermal_state(vec![TripPoint::new("sensor1", 2, 3)]),
        );
        assert_matches!(thermal_config.validate(), Err(_));
    }
}
