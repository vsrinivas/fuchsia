// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    scrutiny_utils::env::fuchsia_build_dir,
    serde::{Deserialize, Serialize},
    std::path::{Path, PathBuf},
};

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

impl Config {
    /// The default runtime configuration includes everything you will see when
    /// running the scrutiny binary with no options set. This is the expected
    /// runtime for auditors doing discovery tasks or using the toolset in its
    /// normal mode of operation.
    pub fn default() -> Config {
        Config { launch: LaunchConfig::default(), runtime: RuntimeConfig::default() }
    }

    /// The minimal runtime configuration is intended for most integration use
    /// cases where the REST server is not required for instance. This can be
    /// used to launch the runtime for static analysis verification or for
    /// basic core functionality. The toolkit and search plugin and other
    /// features are not loaded.
    pub fn minimal() -> Config {
        Config { launch: LaunchConfig::minimal(), runtime: RuntimeConfig::minimal() }
    }

    /// Configures Scrutiny to run a single command in a minimal runtime
    /// environment. This is a helper utility configuration to simplify
    /// common configurations.
    pub fn run_command(command: String) -> Config {
        Config {
            launch: LaunchConfig { command: Some(command), script_path: None },
            runtime: RuntimeConfig::minimal(),
        }
    }

    /// Configures Scrutiny to run with a single script in a minimal runtime
    /// environment. This is a helper utility configuration to simplify
    /// common configurations.
    pub fn run_script(script_path: String) -> Config {
        Config {
            launch: LaunchConfig { command: None, script_path: Some(script_path) },
            runtime: RuntimeConfig::minimal(),
        }
    }
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

impl LaunchConfig {
    pub fn default() -> LaunchConfig {
        LaunchConfig { command: None, script_path: None }
    }
    pub fn minimal() -> LaunchConfig {
        LaunchConfig::default()
    }
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
    /// Configuration about the optional Scrutiny server. If None the server
    /// will not be launched.
    pub server: Option<ServerConfig>,
}

impl RuntimeConfig {
    pub fn default() -> RuntimeConfig {
        RuntimeConfig {
            logging: LoggingConfig::default(),
            model: ModelConfig::default(),
            plugin: PluginConfig::default(),
            server: Some(ServerConfig::default()),
        }
    }
    pub fn minimal() -> RuntimeConfig {
        RuntimeConfig {
            logging: LoggingConfig::minimal(),
            model: ModelConfig::minimal(),
            plugin: PluginConfig::minimal(),
            // The server feature is disabled runtime configuration.
            server: None,
        }
    }
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
    pub fn minimal() -> LoggingConfig {
        LoggingConfig::default()
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
pub struct ModelConfig {
    /// Path to the model data.
    pub uri: String,
    /// Path to the Fuchsia build directory.
    pub build_path: PathBuf,
    /// Path to the local Fuchsia package repository.
    pub repository_path: PathBuf,
    /// Path to the local Fuchsia package repository blobs.
    pub repository_blobs_path: PathBuf,
    /// The URL of the Fuchsia update package.
    pub update_package_url: String,
    /// The URL of the Fuchsia config-data package.
    pub config_data_package_url: String,
}

impl ModelConfig {
    /// By default the ModelConfig will attempt to retrieve the build dir
    /// from the environment. If this fails it simply sets the environment
    /// to an empty environment.
    pub fn default() -> ModelConfig {
        let build_path = fuchsia_build_dir().unwrap_or_else(|_| Path::new("").to_path_buf());
        let repository_path = build_path.join("amber-files/repository");
        let repository_blobs_path = build_path.join("amber-files/blobs");
        ModelConfig {
            uri: "{memory}".to_string(),
            build_path,
            repository_path,
            repository_blobs_path,
            update_package_url: "fuchsia-pkg://fuchsia.com/update".to_string(),
            config_data_package_url: "fuchsia-pkg://fuchsia.com/config-data".to_string(),
        }
    }
    pub fn minimal() -> ModelConfig {
        ModelConfig::default()
    }
    /// Model URI used to determine if the model is in memory or on disk.
    pub fn uri(&self) -> String {
        self.uri.clone()
    }
    /// The root Fuchsia build path.
    pub fn build_path(&self) -> PathBuf {
        self.build_path.clone()
    }
    /// The Fuchsia repository path.
    pub fn repository_path(&self) -> PathBuf {
        self.repository_path.clone()
    }
    /// The Fuchsia repository blobs path.
    pub fn repository_blobs_path(&self) -> PathBuf {
        self.repository_blobs_path.clone()
    }
    /// The Fuchsia package url of the update package.
    pub fn update_package_url(&self) -> String {
        self.update_package_url.clone()
    }
    /// The Fuchsia package url of the config data package.
    pub fn config_data_package_url(&self) -> String {
        self.config_data_package_url.clone()
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
                "CorePlugin".to_string(),
                "SearchPlugin".to_string(),
                "EnginePlugin".to_string(),
                "ToolkitPlugin".to_string(),
                "VerifyPlugin".to_string(),
            ],
        }
    }
    /// The minimal plugin configuration only contains the core plugin.
    pub fn minimal() -> PluginConfig {
        PluginConfig { plugins: vec!["CorePlugin".to_string()] }
    }
}

/// The Scrutiny Server is an optional runtime feature that launches a server
/// to display the Scrutiny visualizers.
#[derive(Debug, Deserialize, Serialize, PartialEq, Eq)]
pub struct ServerConfig {
    /// The port to run the server from.
    pub port: u16,
    /// A visualizer path to server Data Visualizers from, this is always
    /// relative to $FUCHSIA_DIR.
    pub visualizer_path: String,
}

impl ServerConfig {
    pub fn default() -> ServerConfig {
        ServerConfig { port: 8080, visualizer_path: "/scripts/scrutiny".to_string() }
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
