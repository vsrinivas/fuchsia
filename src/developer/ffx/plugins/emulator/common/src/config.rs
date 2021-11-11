// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The config module encapsulates the access to ffx_config
//! and enables the mocking and faking of configuration to
//! make the use of ffx_config test friendly.
//!
//! This module is specific for ffx_emulator, and could
//! be used as the foundation for a general solution
//! to fxbug.dev/86958.

use {
    anyhow::{anyhow, Result},
    ffx_config::api::ConfigError,
    std::path::PathBuf,
};

// These are constants for the keys accessed by ffx emulator plugin.

/// The root directory for storing instance specific data. Instances
/// should create a subdirectory in this directory to store data.
pub const EMU_INSTANCE_ROOT_DIR: &'static str = "emu.instance_dir";

/// The full path to the script to run initializing any network interfaces
/// before starting the emulator. See the --upscript command line option
/// for details.
pub const EMU_UPSCRIPT_FILE: &'static str = "emu.upscript";

/// The file containing the private key for SSH access to the emulator.
pub const SSH_PRIVATE_KEY: &'static str = "ssh.priv";

/// The file containing the authorized keys for SSH access.
pub const SSH_PUBLIC_KEY: &'static str = "ssh.pub";

/// The directory which contains the product bundle manifest files.
pub const FMS_DATA_DIR: &'static str = "sdk.fms.data.dir";

/// The directory which contains the product bundle manifest files.
pub const SDK_ROOT: &'static str = "sdk.dir";

const ALL_KEYS: &'static [&'static str] =
    &[EMU_UPSCRIPT_FILE, EMU_INSTANCE_ROOT_DIR, FMS_DATA_DIR, SSH_PRIVATE_KEY, SSH_PUBLIC_KEY];

macro_rules! missing_key_message {
    ($key_name:expr) => {
        format!(
            "key not found in config::ALL_KEYS, please add and document {}",
            stringify!(key_name)
        )
    };
}

/// FfxConfigWrapper encapsulates calls to ffx_config::
/// so it is easy to test different values and mock
/// the data.
///
/// For testing, the desired key/value pairs can be added to the overrides property.
/// If any overrides are added, then the wrapper object is in "test mode" and will only
/// return values in the overrides map.
///
/// "Production" mode,  the overrides map is empty and the calls will pass through to
/// ffx_config.
///
/// TODO(fxbug.dev/86958): Support injecting configuration for tests into ffx::config.
pub struct FfxConfigWrapper {
    pub overrides: std::collections::HashMap<&'static str, &'static str>,
}

impl FfxConfigWrapper {
    pub fn new() -> Self {
        Self { overrides: std::collections::HashMap::new() }
    }

    /// Returns the PathBuf to the file that the property_name resolves to.
    /// The file is guaranteed to exist. Note: when overriding in test mode, the test
    /// value is returned regardless of file existence.
    pub async fn file(&self, property_name: &str) -> Result<PathBuf, ConfigError> {
        // If the overrides map is empty, use ffx_config API directly.
        if self.overrides.is_empty() {
            return ffx_config::file(property_name).await;
        }

        // If the overrides map has entries, this means the wrapper is being
        // used in a test. Take advantage of this fact, and confirm that the
        // property_name being asked for is documented in the ALL_KEYS array.
        // Otherwise fail, which will make the developer add the entry.
        if !ALL_KEYS.contains(&property_name) {
            return Err(ConfigError::from(anyhow!(missing_key_message!(property_name))));
        }
        match self.overrides.get(property_name) {
            Some(val) => Ok(PathBuf::from(val)),
            None => Err(ConfigError::from(anyhow!("key not found {}", property_name))),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serial_test::serial;

    #[fuchsia_async::run_singlethreaded(test)]
    #[serial]
    async fn test_keys() {
        const FAKE_PATH: &str = "/path/to/key";
        const RANDOM_PROPERTY_NAME: &str = "random-property-name";

        let mut config = FfxConfigWrapper::new();
        // Add a value for ssh_public key. This causes all config queries to be resolved via
        // overrides.
        config.overrides.insert(SSH_PRIVATE_KEY, FAKE_PATH);

        // Get the key
        let ok_result = config.file(SSH_PRIVATE_KEY).await;
        assert_eq!(ok_result.unwrap(), PathBuf::from(FAKE_PATH));

        // Try to get a key not defined in ALL_KEYS.
        let err_result = config.file(RANDOM_PROPERTY_NAME).await;
        assert!(err_result.is_err());
        assert_eq!(
            format!("{:?}", err_result.unwrap_err()),
            format!("ConfigError({})", missing_key_message!(RANDOM_PROPERTY_NAME))
        );
    }
}
