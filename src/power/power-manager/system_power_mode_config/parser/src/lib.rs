// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, ensure, Context as _, Error};
pub use fidl_fuchsia_power_clientlevel::ClientType;
pub use fidl_fuchsia_power_systemmode::{ClientConfig, ModeMatch, SystemMode};
use serde::Deserialize;
use std::collections::{HashMap, HashSet};
use std::convert::TryInto;
use std::fs::File;
use std::io::Read as _;
use std::path::Path;

/// This library is used to parse a system power mode configuration JSON file into a data structure
/// which also implements some convenience methods for accessing and consuming the data.
///
/// The intended usage is that `SystemPowerModeConfig::read()` is called with the path to a system
/// power mode configuration JSON file. If successful, the function returns a
/// `SystemPowerModeConfig` instance containing the parsed configuration.
///
/// The parser expects a a valid JSON5 serialization of `SystemPowerModeConfig`, such as the
/// following:
/// ```
///   {
///     clients: {
///       wlan: {
///         mode_matches: [
///           {
///             mode: 'battery_saver',
///             level: 0,
///           },
///         ],
///         default_level: 1,
///       },
///     },
///   }
/// ```

/// Represents the top level of a system power mode configuration.
///
/// This struct wraps the contents of a system power mode configuration file. All of the types
/// contained within this top level struct are defined in the `fuchsia.power.clientlevel` and
/// `fuchsia.power.systemmode` libraries.
#[derive(Debug, PartialEq)]
pub struct SystemPowerModeConfig {
    clients: HashMap<ClientType, ClientConfig>,
}

impl SystemPowerModeConfig {
    /// Reads the supplied JSON file path and parses into a `SystemPowerModeConfig` instance.
    ///
    /// Attempts to open, read, and parse the supplied JSON file into a valid
    /// `SystemPowerModeConfig` instance. If parsing was successful, then it is also tested for
    /// validity. If a `SystemPowerModeConfig` instance could not be created, or validation fails,
    /// then an error is returned.
    pub fn read(json_file_path: &Path) -> Result<SystemPowerModeConfig, Error> {
        let mut buffer = String::new();
        File::open(&json_file_path)
            .context(format!("Failed to open file at path {}", json_file_path.display()))?
            .read_to_string(&mut buffer)?;

        let config = Self::from_json_str(&buffer)?;
        config.validate().context("SystemPowerModeConfig validation failed")?;
        Ok(config)
    }

    /// Attempts to create a `SystemPowerModeConfig` instance from a JSON5 string.
    ///
    /// The function eases deserialization by using locally defined structs consisting only of types
    /// that are naturally supported by Serde's deserialization system (these local types end in
    /// "De"). If deserialization of these structs succeeds, then they are converted to the desired
    /// `SystemPowerModeConfig` struct with the help of `TryInto` impls and helper functions.
    fn from_json_str(json: &str) -> Result<SystemPowerModeConfig, Error> {
        #[derive(Deserialize)]
        struct SystemPowerModeConfigDe {
            clients: HashMap<String, ClientConfigDe>,
        }

        #[derive(Deserialize)]
        struct ClientConfigDe {
            mode_matches: Vec<ModeMatchDe>,
            default_level: u64,
        }

        #[derive(Deserialize)]
        struct ModeMatchDe {
            mode: String,
            power_level: u64,
        }

        impl TryInto<SystemPowerModeConfig> for SystemPowerModeConfigDe {
            type Error = anyhow::Error;
            fn try_into(self) -> Result<SystemPowerModeConfig, Self::Error> {
                let mut clients = HashMap::new();
                for (k, v) in self.clients.into_iter() {
                    clients.insert(str_to_client_type(&k)?, v.try_into()?);
                }
                Ok(SystemPowerModeConfig { clients })
            }
        }

        impl TryInto<ClientConfig> for ClientConfigDe {
            type Error = Error;
            fn try_into(self) -> Result<ClientConfig, Self::Error> {
                let mode_matches = self
                    .mode_matches
                    .into_iter()
                    .map(|m| m.try_into())
                    .collect::<Result<Vec<_>, _>>()?;
                Ok(ClientConfig { mode_matches, default_level: self.default_level })
            }
        }

        impl TryInto<ModeMatch> for ModeMatchDe {
            type Error = Error;
            fn try_into(self) -> Result<ModeMatch, Self::Error> {
                Ok(ModeMatch {
                    mode: str_to_system_mode(&self.mode)?,
                    power_level: self.power_level,
                })
            }
        }

        let deserializer: SystemPowerModeConfigDe = serde_json5::from_str(json)?;
        deserializer.try_into()
    }

    /// Validates the configuration.
    pub fn validate(&self) -> Result<(), Error> {
        // Iterate and validate each underlying `ClientConfig` instance
        for (client_name, client_config) in self.clients.iter() {
            client_config
                .validate()
                .context(format!("Validation failed for client {:?}", client_name))?;
        }

        Ok(())
    }

    /// Gets the `ClientConfig` instance for the specified client.
    pub fn get_client_config(&self, client_type: ClientType) -> Option<&ClientConfig> {
        self.clients.get(&client_type)
    }

    pub fn into_iter(self) -> impl Iterator<Item = (ClientType, ClientConfig)> {
        self.clients.into_iter()
    }
}

/// Parses a string into a `ClientType` variant.
///
/// To successfully parse, the string must be a lower snake case representation of the `ClientType`
/// variant.
fn str_to_client_type(s: &str) -> Result<ClientType, Error> {
    Ok(match s {
        "wlan" => ClientType::Wlan,
        _ => bail!("Unsupported client type '{}'", s),
    })
}

/// Parses a string into a `SystemMode` variant.
///
/// To successfully parse, the string must be a lower snake case representation of the `SystemMode`
/// variant.
fn str_to_system_mode(s: &str) -> Result<SystemMode, Error> {
    // Since `SystemMode` doesn't actually contain any variants today, we bail unconditionally here.
    // Once `SystemMode` grows one or more variants then we should string match to return the
    // correct one (same as in `str_to_client_type`).

    bail!("Unsupported system mode '{}'", s)
}

/// A trait that adds useful test-only methods to the `SystemPowerModeConfig` struct.
///
/// This trait and its methods are not marked `cfg(test)` so that they may be used outside of this
/// crate.
pub trait SystemPowerModeConfigTestExt {
    fn new() -> Self;
    fn add_client_config(self, client_type: ClientType, config: ClientConfig) -> Self;
}

impl SystemPowerModeConfigTestExt for SystemPowerModeConfig {
    /// Creates an empty `SystemPowerModeConfig` (no configured clients).
    fn new() -> Self {
        Self { clients: HashMap::new() }
    }

    /// Adds a configuration entry for the specified client.
    fn add_client_config(mut self, client_type: ClientType, config: ClientConfig) -> Self {
        self.clients.insert(client_type, config);
        self
    }
}

/// A trait that adds useful test-only methods to the `ClientConfig` struct.
///
/// This trait and its methods are not marked `cfg(test)` so that they may be used outside of this
/// crate.
pub trait ClientConfigTestExt {
    fn new(default_level: u64) -> Self;
    fn append_mode_match(self, mode: SystemMode, level: u64) -> Self;
}

impl ClientConfigTestExt for ClientConfig {
    /// Creates an empty `ClientConfig` which consists of a default level and no `ModeMatch`
    /// entries.
    fn new(default_level: u64) -> Self {
        Self { mode_matches: Vec::new(), default_level }
    }

    /// Appends a mode match entry to the end of the existing entries.
    fn append_mode_match(mut self, mode: SystemMode, power_level: u64) -> Self {
        self.mode_matches.push(ModeMatch { mode, power_level });
        self
    }
}

pub trait ClientConfigExt {
    fn validate(&self) -> Result<(), Error>;
}

impl ClientConfigExt for ClientConfig {
    /// Validates a `ClientConfig` instance.
    ///
    /// Performs a series of validations to check if the configuration defined by the `ClientConfig`
    /// instance is valid. The instance is valid if:
    ///   1) a given `SystemMode` is not repeating in multiple `ModeMatch` entries
    fn validate(&self) -> Result<(), Error> {
        // Ensure a given `SystemMode` is not repeating in multiple `ModeMatch` entries
        {
            let mut modes_set = HashSet::new();
            for mode in self.mode_matches.iter().map(|mode_match| mode_match.mode) {
                ensure!(
                    modes_set.insert(mode),
                    "A given mode may only be specified once (violated by {:?})",
                    mode
                );
            }
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use crate::*;
    use matches::assert_matches;

    /// Tests that valid `SystemPowerModeConfig` instances pass the validation.
    #[test]
    fn test_system_power_mode_config_validation_success() {
        // Empty config
        let config = SystemPowerModeConfig::new();
        assert_matches!(config.validate(), Ok(()));

        // A single configured client with an arbitrary default power level
        let config =
            SystemPowerModeConfig::new().add_client_config(ClientType::Wlan, ClientConfig::new(0));
        assert_matches!(config.validate(), Ok(()));
    }

    /// Tests that invalid `SystemPowerModeConfig` instances fail the validation.
    #[test]
    fn test_system_power_mode_config_validation_failures() {
        // The only way validation can fail today is by having multiple `ModeMade` entries with a
        // repeated `SystemMode`. We won't be able to test this path until `SystemMode` variants are
        // added (currently there are none).
    }

    /// Tests for proper parsing by the `str_to_client_type` function.
    ///
    /// The test tries to parse each supported `ClientType` variant. Parsing should succeed for the
    /// required lower snake case string representation and fail otherwise.
    ///
    /// Additional test cases should be added for each new `ClientType` variant as it grows.
    #[test]
    fn test_parse_client_types() {
        assert_eq!(str_to_client_type("wlan").unwrap(), ClientType::Wlan);
        assert!(str_to_client_type("Wlan").is_err());
        assert!(str_to_client_type("WLAN").is_err());
    }

    /// Tests for proper parsing by the `str_to_system_mode` function.
    ///
    /// The test tries to parse each supported `SystemMode` variant. Parsing should succeed for the
    /// required lower snake case string representation and fail otherwise.
    ///
    /// Additional test cases should be added for each new `SystemMode` variant as it grows.
    #[test]
    fn test_parse_system_modes() {
        assert!(str_to_system_mode("").is_err());
    }
}
