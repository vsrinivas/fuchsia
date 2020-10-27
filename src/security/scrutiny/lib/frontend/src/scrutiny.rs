// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        config::{Config, LoggingVerbosity},
        logo,
        rest::service::RestService,
        rest::visualizer::Visualizer,
        shell::Shell,
    },
    anyhow::{Error, Result},
    clap::{App, Arg, ArgMatches},
    log::error,
    scrutiny::{
        engine::{
            dispatcher::ControllerDispatcher, manager::PluginManager, plugin::Plugin,
            scheduler::CollectorScheduler,
        },
        model::model::DataModel,
    },
    simplelog::{Config as SimpleLogConfig, LevelFilter, WriteLogger},
    std::env,
    std::fs::File,
    std::io::{self, BufRead, BufReader, ErrorKind},
    std::sync::{Arc, Mutex, RwLock},
};

const FUCHSIA_DIR: &str = "FUCHSIA_DIR";

/// Holds a reference to core objects required by the application to run.
pub struct Scrutiny {
    manager: Arc<Mutex<PluginManager>>,
    dispatcher: Arc<RwLock<ControllerDispatcher>>,
    scheduler: Arc<Mutex<CollectorScheduler>>,
    visualizer: Option<Arc<RwLock<Visualizer>>>,
    shell: Shell,
    config: Config,
}

impl Scrutiny {
    /// Creates the DataModel, ControllerDispatcher, CollectorScheduler and
    /// PluginManager.
    pub fn new(config: Config) -> Result<Self> {
        let log_level = match config.runtime.logging.verbosity {
            LoggingVerbosity::Error => LevelFilter::Error,
            LoggingVerbosity::Warn => LevelFilter::Warn,
            LoggingVerbosity::Info => LevelFilter::Info,
            LoggingVerbosity::Debug => LevelFilter::Debug,
            LoggingVerbosity::Trace => LevelFilter::Trace,
            LoggingVerbosity::Off => LevelFilter::Off,
        };
        if log_level != LevelFilter::Off
            && config.launch.command.is_none()
            && config.launch.script_path.is_none()
        {
            logo::print_logo();
        }
        WriteLogger::init(
            log_level,
            SimpleLogConfig::default(),
            File::create(config.runtime.logging.path.clone()).unwrap(),
        )
        .unwrap();
        let model = Arc::new(DataModel::connect(config.runtime.model.path.clone())?);
        let dispatcher = Arc::new(RwLock::new(ControllerDispatcher::new(Arc::clone(&model))));
        let visualizer = if let Some(server_config) = &config.runtime.server {
            Some(Arc::new(RwLock::new(Visualizer::new(
                env::var(FUCHSIA_DIR).expect("Unable to retrieve $FUCHSIA_DIR, has it been set?"),
                server_config.visualizer_path.clone(),
            ))))
        } else {
            None
        };
        let scheduler = Arc::new(Mutex::new(CollectorScheduler::new(Arc::clone(&model))));
        let manager = Arc::new(Mutex::new(PluginManager::new(
            Arc::clone(&scheduler),
            Arc::clone(&dispatcher),
        )));
        let shell = Shell::new(Arc::clone(&manager), Arc::clone(&dispatcher));
        Ok(Self { manager, dispatcher, scheduler, visualizer, shell, config })
    }

    /// Declares the Scrutiny command line interface.
    fn cmdline() -> App<'static, 'static> {
        App::new("scrutiny")
            .version("1.0")
            .author("Fuchsia Authors")
            .about("An extendable security auditing framework")
            .arg(
                Arg::with_name("command")
                    .short("c")
                    .help("Run a single command")
                    .value_name("command")
                    .takes_value(true),
            )
            .arg(
                Arg::with_name("model")
                    .short("m")
                    .help("The uri of the data model.")
                    .default_value("/tmp/scrutiny/"),
            )
            .arg(
                Arg::with_name("log")
                    .short("l")
                    .help("Path to output scrutiny.log")
                    .default_value("/tmp/scrutiny.log"),
            )
            .arg(
                Arg::with_name("port")
                    .short("p")
                    .help("The port to run the scrutiny service on.")
                    .default_value("8080"),
            )
            .arg(
                Arg::with_name("script")
                    .short("s")
                    .help("Run a file as a scrutiny script")
                    .value_name("script")
                    .takes_value(true),
            )
            .arg(
                Arg::with_name("verbosity")
                    .short("v")
                    .help("The verbosity level of logging")
                    .possible_values(&["off", "error", "warn", "info", "debug", "trace"])
                    .default_value("info"),
            )
            .arg(
                Arg::with_name("visualizer")
                    .short("i")
                    .help(
                        "The root path (relative to $FUCHSIA_DIR) for the visualizer interface. \
                    .html, .css, and .json files relative to this root path will \
                    be served relative to the scrutiny service root.",
                    )
                    .default_value("/scripts/scrutiny"),
            )
    }

    /// Parses all the command line arguments passed in and returns a
    /// ScrutinyConfig.
    fn cmdline_to_config(args: ArgMatches<'static>) -> Result<Config> {
        let mut config = Config::default();
        if let Some(command) = args.value_of("command") {
            config.launch.command = Some(command.to_string());
        }
        if let Some(script_path) = args.value_of("script") {
            config.launch.script_path = Some(script_path.to_string());
        }
        config.runtime.logging.path = args.value_of("log").unwrap().to_string();
        config.runtime.logging.verbosity = match args.value_of("verbosity").unwrap() {
            "error" => LoggingVerbosity::Error,
            "warn" => LoggingVerbosity::Warn,
            "info" => LoggingVerbosity::Info,
            "debug" => LoggingVerbosity::Debug,
            "trace" => LoggingVerbosity::Trace,
            _ => LoggingVerbosity::Off,
        };
        config.runtime.model.path = args.value_of("model").unwrap().to_string();
        if let Some(server_config) = &mut config.runtime.server {
            if let Ok(port) = args.value_of("port").unwrap().parse::<u16>() {
                server_config.port = port;
            } else {
                error!("Port provided was not a valid port number.");
                return Err(Error::new(io::Error::new(
                    ErrorKind::InvalidInput,
                    "Invaild argument.",
                )));
            }
            server_config.visualizer_path = args.value_of("visualizer").unwrap().to_string();
        }
        Ok(config)
    }

    /// Parses all the command line arguments passed in from the environment
    /// and returns a ScrutinyConfig.
    pub fn args_from_env() -> Result<Config> {
        let app = Scrutiny::cmdline();
        Scrutiny::cmdline_to_config(app.get_matches())
    }

    /// Parses arguments directly from the vector instead of the environmetn
    /// and returns a ScrutinyConfig.
    pub fn args_inline(inline_arguments: Vec<String>) -> Result<Config> {
        let app = Scrutiny::cmdline();
        Scrutiny::cmdline_to_config(app.get_matches_from(inline_arguments))
    }

    /// Utility function to register a plugin.
    pub fn plugin(&mut self, plugin: impl Plugin + 'static) -> Result<()> {
        self.manager.lock().unwrap().register_and_load(Box::new(plugin))
    }

    /// Returns an arc to the dispatcher controller that can be exposed to
    /// plugins that may wish to use it for managemnet.
    pub fn dispatcher(&self) -> Arc<RwLock<ControllerDispatcher>> {
        Arc::clone(&self.dispatcher)
    }

    /// Returns an arc to the collector scheduler that can be exposed to plugins
    /// that may wish to use it for management.
    pub fn scheduler(&self) -> Arc<Mutex<CollectorScheduler>> {
        Arc::clone(&self.scheduler)
    }

    /// Returns an arc to the plugin manager that can be exposed to plugins that
    /// may wish to use it for management.
    pub fn plugin_manager(&self) -> Arc<Mutex<PluginManager>> {
        Arc::clone(&self.manager)
    }

    /// Schedules the DataCollectors to run and starts the REST service.
    pub fn run(&mut self) -> Result<()> {
        self.scheduler.lock().unwrap().schedule()?;

        if let Some(command) = &self.config.launch.command {
            // Spin lock on the schedulers to finish.
            while !self.scheduler.lock().unwrap().all_idle() {}
            self.shell.execute(command.to_string());
        } else if let Some(script) = &self.config.launch.script_path {
            // Spin lock on the schedulers to finish.
            while !self.scheduler.lock().unwrap().all_idle() {}
            let script_file = BufReader::new(File::open(script)?);
            for line in script_file.lines() {
                self.shell.execute(line?);
            }
        } else {
            if let Some(server_config) = &self.config.runtime.server {
                RestService::spawn(
                    self.dispatcher.clone(),
                    self.visualizer.clone().unwrap(),
                    server_config.port,
                )?;
            }
            self.shell.run();
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn scrutiny_script_args() {
        let result = Scrutiny::args_inline(
            vec!["scrutiny", "-s", "foo"].into_iter().map(String::from).collect(),
        )
        .unwrap();
        assert_eq!(result.launch.script_path.unwrap(), "foo".to_string());
    }

    #[test]
    fn scrutiny_command_args() {
        let result = Scrutiny::args_inline(
            vec!["scrutiny", "-c", "bar"].into_iter().map(String::from).collect(),
        )
        .unwrap();
        assert_eq!(result.launch.command.unwrap(), "bar".to_string());
    }

    #[test]
    fn scrutiny_model_path_args() {
        let result = Scrutiny::args_inline(
            vec!["scrutiny", "-m", "baz"].into_iter().map(String::from).collect(),
        )
        .unwrap();
        assert_eq!(result.runtime.model.path, "baz".to_string());
    }

    #[test]
    fn scrutiny_logging_path_args() {
        let result = Scrutiny::args_inline(
            vec!["scrutiny", "-l", "test_log"].into_iter().map(String::from).collect(),
        )
        .unwrap();
        assert_eq!(result.runtime.logging.path, "test_log".to_string());
    }

    #[test]
    fn scrutiny_logging_verbosity_args() {
        let result = Scrutiny::args_inline(
            vec!["scrutiny", "-v", "warn"].into_iter().map(String::from).collect(),
        )
        .unwrap();
        assert_eq!(result.runtime.logging.verbosity, LoggingVerbosity::Warn);
    }

    #[test]
    fn scrutiny_port_args() {
        let result = Scrutiny::args_inline(
            vec!["scrutiny", "-p", "1234"].into_iter().map(String::from).collect(),
        )
        .unwrap();
        assert_eq!(result.runtime.server.unwrap().port, 1234);
    }

    #[test]
    fn scrutiny_visualizer_args() {
        let result = Scrutiny::args_inline(
            vec!["scrutiny", "-i", "test_viz"].into_iter().map(String::from).collect(),
        )
        .unwrap();
        assert_eq!(result.runtime.server.unwrap().visualizer_path, "test_viz".to_string());
    }
}
