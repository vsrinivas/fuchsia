// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Result},
    camino::Utf8PathBuf,
    fuchsia_url::AbsolutePackageUrl,
    scrutiny_utils::url::from_package_name,
    sdk_metadata::ProductBundle,
    serde::{Deserialize, Serialize},
    std::path::{Path, PathBuf},
};

pub struct ConfigBuilder {
    model: ModelConfig,
    command: Option<String>,
    script_path: Option<String>,
    plugins: Option<Vec<String>>,
}

impl ConfigBuilder {
    pub fn with_model(model: ModelConfig) -> Self {
        ConfigBuilder { model, command: None, script_path: None, plugins: None }
    }

    pub fn command(&mut self, command: String) -> &mut Self {
        self.command = Some(command);
        self
    }

    pub fn script(&mut self, script_path: String) -> &mut Self {
        self.script_path = Some(script_path);
        self
    }

    pub fn plugins<S: ToString>(&mut self, plugins: Vec<S>) -> &mut Self {
        self.plugins = Some(plugins.iter().map(|s| s.to_string()).collect());
        self
    }

    pub fn build(&self) -> Config {
        let &Self { model, command, script_path, plugins } = &self;
        let plugin = if let Some(plugins) = plugins {
            PluginConfig { plugins: plugins.clone() }
        } else {
            PluginConfig::default()
        };
        Config {
            launch: LaunchConfig { command: command.clone(), script_path: script_path.clone() },
            runtime: RuntimeConfig {
                logging: LoggingConfig::default(),
                model: model.clone(),
                plugin,
            },
        }
    }
}

/// The Scrutiny Configuration determines how the framework runtime will be setup
/// when it launches. This includes determining what features the runtime will
/// include and how those features will operate. Since Scrutiny has a variety
/// of usages from shells to static analysis this configuration is intended to
/// be the source of truth for configuring Scrutiny to meet its requirements.
#[derive(Debug, Deserialize, Serialize, PartialEq, Eq)]
pub struct Config {
    pub launch: LaunchConfig,
    pub runtime: RuntimeConfig,
}

/// Launch configuration describes events that run after the framework has
/// launched such as scripts, commands etc.
#[derive(Debug, Deserialize, Serialize, PartialEq, Eq)]
pub struct LaunchConfig {
    /// Runs a command on launch.
    pub command: Option<String>,
    /// Runs a script at the path on launch.
    pub script_path: Option<String>,
}

/// The Runtime configuration determines which features of the Scrutiny
/// Framework should be available. Changing the runtime configuration could
/// disable certain features for certain usages.
#[derive(Debug, Deserialize, Serialize, PartialEq, Eq)]
pub struct RuntimeConfig {
    /// Configuration about logging such as where logs are stored and its
    /// verbosity level.
    pub logging: LoggingConfig,
    /// Provides configuration options about the model such as where it
    /// is located.
    pub model: ModelConfig,
    /// Provides the set of plugins that should be loaded by the runtime.
    pub plugin: PluginConfig,
}

/// Logging is a required feature of the Scrutiny runtime. Every configuration
/// must include a logging configuration.
#[derive(Debug, Deserialize, Serialize, PartialEq, Eq)]
pub struct LoggingConfig {
    pub path: String,
    pub verbosity: LoggingVerbosity,
    /// Set to true if you don't want stdio output from running commands. This
    /// is useful if you want to interact with the API inside Rust and just
    /// receive the output of the command.
    pub silent_mode: bool,
}

impl LoggingConfig {
    pub fn default() -> LoggingConfig {
        LoggingConfig {
            path: "/tmp/scrutiny.log".to_string(),
            verbosity: LoggingVerbosity::Info,
            silent_mode: false,
        }
    }
}

/// The verbosity level at which logged messages will appear.
#[derive(Debug, Deserialize, Serialize, PartialEq, Eq)]
pub enum LoggingVerbosity {
    Error,
    Warn,
    Info,
    Debug,
    Trace,
    Off,
}

impl From<String> for LoggingVerbosity {
    fn from(item: String) -> Self {
        match item.as_str() {
            "error" => LoggingVerbosity::Error,
            "warn" => LoggingVerbosity::Warn,
            "info" => LoggingVerbosity::Info,
            "debug" => LoggingVerbosity::Debug,
            "trace" => LoggingVerbosity::Trace,
            _ => LoggingVerbosity::Off,
        }
    }
}

/// The DataModel is a required feature of the Scrutiny runtime. Every
/// configuration must include a model configuration. This configuration should
/// include all global configuration in Fuchsia that model collectors should
/// utilize about system state. Instead of collectors hard coding paths these
/// should be tracked here so it is easy to modify all collectors if these
/// paths or urls change in future releases.
#[derive(Debug, Deserialize, Serialize, PartialEq, Eq, Clone)]
// TODO(fxbug.dev/83871): Borrow instead of clone() and allow clients to clone
// only when necessary.
pub struct ModelConfig {
    /// Path to the model data.
    pub uri: String,
    /// Path to the Fuchsia update package.
    pub update_package_path: PathBuf,
    /// Path to a directory of blobs.
    pub blobs_directory: PathBuf,
    /// The URL of the Fuchsia config-data package.
    pub config_data_package_url: AbsolutePackageUrl,
    /// The path to the device manager configuration inside bootfs inside the
    /// ZBI.
    pub devmgr_config_path: PathBuf,
    /// Optional path to a component tree configuration used for customizing
    /// component tree data collection.
    pub component_tree_config_path: Option<PathBuf>,
    /// A path to a directory for temporary files. This path, if defined, should
    /// exist for scrutiny's lifetime.
    pub tmp_dir_path: Option<PathBuf>,
    /// Whether the model is empty, meaning whether the values are stubbed out or actually assigned
    /// real values.
    pub is_empty: bool,
}

impl ModelConfig {
    /// Build an empty model that can be used by scrutiny plugins that do not need
    /// to read any any of the paths in the model, but need a model to run in
    /// the executor.
    pub fn empty() -> Self {
        ModelConfig {
            uri: "{memory}".to_string(),
            update_package_path: "".into(),
            blobs_directory: "".into(),
            config_data_package_url: from_package_name("config-data").unwrap(),
            devmgr_config_path: "config/devmgr".into(),
            component_tree_config_path: None,
            tmp_dir_path: None,
            is_empty: true,
        }
    }

    /// Build a model based on the contents of a product bundle.
    pub fn from_product_bundle(product_bundle_path: impl AsRef<Path>) -> Result<Self> {
        let product_bundle_path = product_bundle_path.as_ref().to_path_buf();
        let product_bundle_path =
            Utf8PathBuf::try_from(product_bundle_path).context("Converting Path to Utf8Path")?;
        let product_bundle = ProductBundle::try_load_from(&product_bundle_path)?;
        let product_bundle = match product_bundle {
            ProductBundle::V1(_) => bail!("Only v2 product bundles are supported"),
            ProductBundle::V2(pb) => pb,
        };

        let repository = product_bundle
            .repositories
            .get(0)
            .ok_or(anyhow!("The product bundle must have at least one repository"))?;
        let blobs_directory = repository.blobs_path.clone().into_std_path_buf();

        let update_package_hash = product_bundle
            .update_package_hash
            .ok_or(anyhow!("An update package must exist inside the product bundle"))?;
        let update_package_path = blobs_directory.join(update_package_hash.to_string());

        Ok(ModelConfig {
            uri: "{memory}".to_string(),
            update_package_path,
            blobs_directory,
            config_data_package_url: from_package_name("config-data").unwrap(),
            devmgr_config_path: "config/devmgr".into(),
            component_tree_config_path: None,
            tmp_dir_path: None,
            is_empty: false,
        })
    }

    /// Model URI used to determine if the model is in memory or on disk.
    pub fn uri(&self) -> String {
        self.uri.clone()
    }
    /// Path to the Fuchsia update package.
    pub fn update_package_path(&self) -> PathBuf {
        assert!(!self.is_empty, "Cannot return an update_package_path for an empty model");
        self.update_package_path.clone()
    }
    /// Paths to blobs directory that contain Fuchsia packages and their
    /// contents.
    pub fn blobs_directory(&self) -> PathBuf {
        assert!(!self.is_empty, "Cannot return a blobs_directory for an empty model");
        self.blobs_directory.clone()
    }
    /// The Fuchsia package url of the config data package.
    pub fn config_data_package_url(&self) -> AbsolutePackageUrl {
        self.config_data_package_url.clone()
    }
    /// The path to the device manager configuration file in bootfs.
    pub fn devmgr_config_path(&self) -> PathBuf {
        self.devmgr_config_path.clone()
    }
    /// A path to a directory for temporary files.
    pub fn tmp_dir_path(&self) -> Option<PathBuf> {
        self.tmp_dir_path.clone()
    }
    /// Whether the model is empty and the values should not be used.
    pub fn is_empty(&self) -> bool {
        self.is_empty
    }
}

/// The PluginConfig is where the bulk of the runtime configuration is
/// determined. It determines which set of plugins from lib/plugins will be
/// loaded on runtime launch and this directly impacts what Data Collectors
/// and DataControllers are available.
#[derive(Debug, Deserialize, Serialize, PartialEq, Eq)]
pub struct PluginConfig {
    pub plugins: Vec<String>,
}

impl PluginConfig {
    pub fn default() -> PluginConfig {
        PluginConfig {
            plugins: vec![
                "DevmgrConfigPlugin".to_string(),
                "StaticPkgsPlugin".to_string(),
                "CorePlugin".to_string(),
                "SearchPlugin".to_string(),
                "EnginePlugin".to_string(),
                "ToolkitPlugin".to_string(),
                "VerifyPlugin".to_string(),
            ],
        }
    }
    // TODO(benwright) - Make this a smaller set once API usages are cleaned up.
    pub fn minimal() -> PluginConfig {
        Self::default()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_logging_verbosity_from_string() {
        assert_eq!(LoggingVerbosity::from("error".to_string()), LoggingVerbosity::Error);
        assert_eq!(LoggingVerbosity::from("warn".to_string()), LoggingVerbosity::Warn);
        assert_eq!(LoggingVerbosity::from("info".to_string()), LoggingVerbosity::Info);
        assert_eq!(LoggingVerbosity::from("debug".to_string()), LoggingVerbosity::Debug);
        assert_eq!(LoggingVerbosity::from("trace".to_string()), LoggingVerbosity::Trace);
        assert_eq!(LoggingVerbosity::from("off".to_string()), LoggingVerbosity::Off);
        assert_eq!(LoggingVerbosity::from("".to_string()), LoggingVerbosity::Off);
    }
}
