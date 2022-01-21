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

/// FEMU tool.
pub const FEMU_TOOL: &'static str = "aemu_internal";

/// QEMU tool.
pub const QEMU_TOOL: &'static str = "qemu";

/// The SDK tool "fvm" used to modify fvm files.
pub const FVM_HOST_TOOL: &'static str = "fvm";

/// The SDK tool "zbi" used to modify the zbi image.
pub const ZBI_HOST_TOOL: &'static str = "zbi";

/// The root directory for storing instance specific data. Instances
/// should create a subdirectory in this directory to store data.
pub const EMU_INSTANCE_ROOT_DIR: &'static str = "emu.instance_dir";

/// The full path to the script to run initializing any network interfaces
/// before starting the emulator. See the --upscript command line option
/// for details.
pub const EMU_UPSCRIPT_FILE: &'static str = "emu.upscript";

/// The file containing the authorized keys for SSH access.
pub const SSH_PUBLIC_KEY: &'static str = "ssh.pub";

const ALL_KEYS: &'static [&'static str] =
    &[EMU_UPSCRIPT_FILE, EMU_INSTANCE_ROOT_DIR, SSH_PUBLIC_KEY];

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
#[derive(Clone, Default, Debug, PartialEq)]
pub struct FfxConfigWrapper {
    pub overrides: std::collections::HashMap<&'static str, String>,
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
            ffx_config::file(property_name).await
        } else {
            Ok(PathBuf::from(self.from_overrides(property_name)?))
        }
    }

    /// Returns the value that the property_name resolves to. Does not check files for existence.
    pub async fn get(&self, property_name: &str) -> Result<String, ConfigError> {
        // If the overrides map is empty, use ffx_config API directly.
        if self.overrides.is_empty() {
            ffx_config::get(property_name).await
        } else {
            self.from_overrides(property_name)
        }
    }

    /// Checks the overrides map for the requested property, and returns it if it exists.
    fn from_overrides(&self, property_name: &str) -> Result<String, ConfigError> {
        // If the overrides map has entries, this means the wrapper is being
        // used in a test. Take advantage of this fact, and confirm that the
        // property_name being asked for is documented in the ALL_KEYS array.
        // Otherwise fail, which will make the developer add the entry.
        if !ALL_KEYS.contains(&property_name) {
            return Err(ConfigError::from(anyhow!(missing_key_message!(property_name))));
        }
        match self.overrides.get(property_name) {
            Some(val) => Ok(val.to_string()),
            None => Err(ConfigError::from(anyhow!("key not found {}", property_name))),
        }
    }

    /// Returns the PathBuf to the executable with the same name in the SDK.
    /// For testing purposes, add an override value of "test_tool_path.$tool_name".
    pub async fn get_host_tool(&self, tool_name: &str) -> Result<PathBuf> {
        if self.overrides.is_empty() {
            let sdk = ffx_config::get_sdk().await?;
            sdk.get_host_tool(tool_name)
        } else {
            match self.overrides.get(tool_name) {
                Some(val) => Ok(PathBuf::from(val)),
                None => Err(anyhow!("host tool not found {}", tool_name)),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_keys() {
        let fake_path: String = String::from("/path/to/key");
        const RANDOM_PROPERTY_NAME: &str = "random-property-name";

        let mut config = FfxConfigWrapper::new();
        // Add a value for ssh_public key. This causes all config queries to be resolved via
        // overrides.
        config.overrides.insert(SSH_PUBLIC_KEY, fake_path.clone());

        // Get the key
        let ok_result = config.file(SSH_PUBLIC_KEY).await;
        assert_eq!(ok_result.unwrap(), PathBuf::from(fake_path));

        // Try to get a key not defined in ALL_KEYS.
        let err_result = config.file(RANDOM_PROPERTY_NAME).await;
        assert!(err_result.is_err());
        assert_eq!(
            format!("{:?}", err_result.unwrap_err()),
            format!("ConfigError({})", missing_key_message!(RANDOM_PROPERTY_NAME))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_sdk_tools() {
        let fake_fvm_path: String = String::from("/path/to/fvm");
        let mut config = FfxConfigWrapper::new();

        let err_result = config.get_host_tool("fvm").await;
        assert!(err_result.is_err());

        config.overrides.insert("fvm", fake_fvm_path.clone());
        let result = config.get_host_tool("fvm").await;
        assert!(result.is_ok());
        assert_eq!(PathBuf::from(fake_fvm_path), result.unwrap());
    }
}
