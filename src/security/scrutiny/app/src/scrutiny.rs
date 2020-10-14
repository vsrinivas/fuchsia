// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{logo, rest::service::RestService, rest::visualizer::Visualizer, shell::shell::Shell},
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
    simplelog::{Config, LevelFilter, WriteLogger},
    std::env,
    std::fs::File,
    std::io::{self, ErrorKind},
    std::sync::{Arc, Mutex, RwLock},
};

const FUCHSIA_DIR: &str = "FUCHSIA_DIR";

/// Holds a reference to core objects required by the application to run.
pub struct ScrutinyApp {
    manager: Arc<Mutex<PluginManager>>,
    dispatcher: Arc<RwLock<ControllerDispatcher>>,
    scheduler: Arc<Mutex<CollectorScheduler>>,
    visualizer: Arc<RwLock<Visualizer>>,
    shell: Shell,
    args: ArgMatches<'static>,
}

impl ScrutinyApp {
    /// Creates the DataModel, ControllerDispatcher, CollectorScheduler and
    /// PluginManager.
    pub fn new(args: ArgMatches<'static>) -> Result<Self> {
        let log_level = match args.value_of("verbosity").unwrap() {
            "error" => LevelFilter::Error,
            "warn" => LevelFilter::Warn,
            "info" => LevelFilter::Info,
            "debug" => LevelFilter::Debug,
            "trace" => LevelFilter::Trace,
            _ => LevelFilter::Off,
        };
        if log_level != LevelFilter::Off && args.value_of("command").is_none() {
            logo::print_logo();
        }
        WriteLogger::init(
            log_level,
            Config::default(),
            File::create(args.value_of("log").unwrap()).unwrap(),
        )
        .unwrap();
        let model = Arc::new(DataModel::connect(args.value_of("model").unwrap().to_string())?);
        let dispatcher = Arc::new(RwLock::new(ControllerDispatcher::new(Arc::clone(&model))));
        let visualizer = Arc::new(RwLock::new(Visualizer::new(
            env::var(FUCHSIA_DIR).expect("Unable to retrieve $FUCHSIA_DIR, has it been set?"),
            args.value_of("visualizer").unwrap().to_string(),
        )));
        let scheduler = Arc::new(Mutex::new(CollectorScheduler::new(Arc::clone(&model))));
        let manager = Arc::new(Mutex::new(PluginManager::new(
            Arc::clone(&scheduler),
            Arc::clone(&dispatcher),
        )));
        let shell = Shell::new(Arc::clone(&manager), Arc::clone(&dispatcher));
        Ok(Self { manager, dispatcher, scheduler, visualizer, shell, args })
    }

    /// Parses all the commond line arguments passed in and returns.
    pub fn args() -> ArgMatches<'static> {
        App::new("scrutiny")
            .version("0.1")
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
            .get_matches()
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

        if let Some(command) = self.args.value_of("command") {
            // Spin lock on the schedulers to finish.
            while !self.scheduler.lock().unwrap().all_idle() {}
            self.shell.execute(command.to_string());
        } else {
            if let Ok(port) = self.args.value_of("port").unwrap().parse::<u16>() {
                RestService::spawn(self.dispatcher.clone(), self.visualizer.clone(), port);
            } else {
                error!("Port provided was not a valid port number.");
                return Err(Error::new(io::Error::new(
                    ErrorKind::InvalidInput,
                    "Invaild argument.",
                )));
            }
            self.shell.run();
        }
        Ok(())
    }
}
