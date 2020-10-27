// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The Scrutiny Configuration determines how the framework runtime will be setup
/// when it launches. This includes determining what features the runtime will
/// include and how those features will operate. Since Scrutiny has a variety
/// of usages from shells to static analysis this configuration is intended to
/// be the source of truth for configuring Scrutiny to meet its requirements.
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
}

/// Launch configuration describes events that run after the framework has
/// launched such as scripts, commands etc.
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
pub struct LoggingConfig {
    pub path: String,
    pub verbosity: LoggingVerbosity,
}

impl LoggingConfig {
    pub fn default() -> LoggingConfig {
        LoggingConfig { path: "/tmp/scrutiny.log".to_string(), verbosity: LoggingVerbosity::Info }
    }
    pub fn minimal() -> LoggingConfig {
        LoggingConfig::default()
    }
}

/// The verbosity level at which logged messages will appear.
#[derive(Debug, PartialEq, Eq)]
pub enum LoggingVerbosity {
    Error,
    Warn,
    Info,
    Debug,
    Trace,
    Off,
}

/// The DataModel is a required feature of the Scrutiny runtime. Every
/// configuration must include a model configuration.
pub struct ModelConfig {
    /// Path to the model data.
    pub path: String,
}

impl ModelConfig {
    pub fn default() -> ModelConfig {
        ModelConfig { path: "/tmp/scrutiny/".to_string() }
    }
    pub fn minimal() -> ModelConfig {
        ModelConfig::default()
    }
}

/// The PluginConfig is where the bulk of the runtime configuration is
/// determined. It determines which set of plugins from lib/plugins will be
/// loaded on runtime launch and this directly impacts what Data Collectors
/// and DataControllers are available.
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
