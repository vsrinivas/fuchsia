// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Error, Result},
    clap::{App, Arg, ArgMatches},
    log::error,
    scrutiny::{
        engine::{dispatcher::ControllerDispatcher, manager::PluginManager, pool::CollectorPool},
        model::model::DataModel,
        plugins::components::graph::ComponentGraphPlugin,
        plugins::health::HealthPlugin,
        rest::service,
    },
    simplelog::{Config, LevelFilter, TermLogger, TerminalMode},
    std::io::{self, ErrorKind},
    std::sync::{Arc, Mutex, RwLock},
    termion::color,
};

/// Holds a reference to core objects required by the application to run.
struct Application {
    pub manager: PluginManager,
    dispatcher: Arc<RwLock<ControllerDispatcher>>,
    collector: Arc<Mutex<CollectorPool>>,
    args: ArgMatches<'static>,
}

impl Application {
    /// Creates the DataModel, ControllerDispatcher, CollectorPool and
    /// PluginManager.
    pub fn new(args: ArgMatches<'static>) -> Result<Self> {
        let model = Arc::new(DataModel::connect()?);
        let dispatcher = Arc::new(RwLock::new(ControllerDispatcher::new(Arc::clone(&model))));
        let collector = Arc::new(Mutex::new(CollectorPool::new(Arc::clone(&model))));
        let manager = PluginManager::new(Arc::clone(&collector), Arc::clone(&dispatcher));
        Ok(Self { manager, dispatcher, collector, args })
    }

    /// Parses all the commond line arguments passed in and returns.
    pub fn args() -> ArgMatches<'static> {
        App::new("scrutiny")
            .version("0.1")
            .author("Fuchsia Authors")
            .about("An extendable security auditing framework")
            .arg(
                Arg::with_name("port")
                    .short("p")
                    .help("The port to run the scrutiny service on.")
                    .default_value("8080"),
            )
            .get_matches()
    }

    /// Schedules the DataCollectors to run and starts the REST service.
    pub fn run(&mut self) -> Result<()> {
        self.collector.lock().unwrap().schedule()?;

        if let Ok(port) = self.args.value_of("port").unwrap().parse::<u16>() {
            service::run(self.dispatcher.clone(), port);
            Ok(())
        } else {
            error!("Port provided was not a valid port number.");
            Err(Error::new(io::Error::new(ErrorKind::InvalidInput, "Invaild argument.")))
        }
    }

    /// Prints the ASCII logo for scrutiny.
    pub fn print_logo() {
        println!(
            "{}
 _____                 _   _
/  ___|               | | (_)
\\ `--.  ___ _ __ _   _| |_ _ _ __  _   _
 `--. \\/ __| '__| | | | __| | '_ \\| | | |
/\\__/ / (__| |  | |_| | |_| | | | | |_| |
\\____/ \\___|_|   \\__,_|\\__|_|_| |_|\\__, |
                                    __/ |
                                   |___/
============================================
{}",
            color::Fg(color::Yellow),
            color::Fg(color::Reset)
        )
    }
}

/// Scrutiny is still a WIP this is just a placeholder for setting up the
/// build targets.
fn main() -> Result<()> {
    let args = Application::args();
    Application::print_logo();
    TermLogger::init(LevelFilter::Info, Config::default(), TerminalMode::Mixed).unwrap();

    let mut app = Application::new(args)?;
    app.manager.register_and_load(Box::new(HealthPlugin::new()))?;
    app.manager.register_and_load(Box::new(ComponentGraphPlugin::new()))?;
    app.run()
}
