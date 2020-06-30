// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::logo,
    anyhow::{Error, Result},
    clap::{App, Arg, ArgMatches},
    log::error,
    scrutiny::{
        engine::{
            dispatcher::ControllerDispatcher, manager::PluginManager, plugin::Plugin,
            scheduler::CollectorScheduler,
        },
        model::model::DataModel,
        rest::service,
        rest::visualizer::Visualizer,
    },
    simplelog::{Config, LevelFilter, TermLogger, TerminalMode},
    std::env,
    std::io::{self, ErrorKind},
    std::sync::{Arc, Mutex, RwLock},
};

const FUCHSIA_DIR: &str = "FUCHSIA_DIR";

/// Holds a reference to core objects required by the application to run.
pub struct ScrutinyApp {
    manager: PluginManager,
    dispatcher: Arc<RwLock<ControllerDispatcher>>,
    collector: Arc<Mutex<CollectorScheduler>>,
    visualizer: Arc<RwLock<Visualizer>>,
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
            "trace" => LevelFilter::Trace,
            _ => LevelFilter::Off,
        };
        if log_level != LevelFilter::Off {
            logo::print_logo();
        }
        TermLogger::init(log_level, Config::default(), TerminalMode::Mixed).unwrap();
        let model = Arc::new(DataModel::connect(args.value_of("model").unwrap().to_string())?);
        let dispatcher = Arc::new(RwLock::new(ControllerDispatcher::new(Arc::clone(&model))));
        let collector = Arc::new(Mutex::new(CollectorScheduler::new(Arc::clone(&model))));
        let manager = PluginManager::new(Arc::clone(&collector), Arc::clone(&dispatcher));
        let visualizer = Arc::new(RwLock::new(Visualizer::new(
            env::var(FUCHSIA_DIR).expect("Unable to retrieve $FUCHSIA_DIR, has it been set?"),
            args.value_of("visualizer").unwrap().to_string(),
        )));
        Ok(Self { manager, dispatcher, collector, visualizer, args })
    }

    /// Parses all the commond line arguments passed in and returns.
    pub fn args() -> ArgMatches<'static> {
        App::new("scrutiny")
            .version("0.1")
            .author("Fuchsia Authors")
            .about("An extendable security auditing framework")
            .arg(
                Arg::with_name("model")
                    .short("m")
                    .help("The uri of the data model.")
                    .default_value("/tmp/scrutiny/"),
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
        self.manager.register_and_load(Box::new(plugin))
    }

    /// Schedules the DataCollectors to run and starts the REST service.
    pub fn run(&mut self) -> Result<()> {
        self.collector.lock().unwrap().schedule()?;

        if let Ok(port) = self.args.value_of("port").unwrap().parse::<u16>() {
            service::run(self.dispatcher.clone(), self.visualizer.clone(), port);
            Ok(())
        } else {
            error!("Port provided was not a valid port number.");
            Err(Error::new(io::Error::new(ErrorKind::InvalidInput, "Invaild argument.")))
        }
    }
}
