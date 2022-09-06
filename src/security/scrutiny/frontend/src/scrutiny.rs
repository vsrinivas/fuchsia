// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::shell::Shell,
    anyhow::Result,
    scrutiny::{
        engine::{
            dispatcher::ControllerDispatcher, manager::PluginManager, plugin::Plugin,
            scheduler::CollectorScheduler,
        },
        model::model::DataModel,
    },
    scrutiny_config::{Config, LoggingVerbosity},
    simplelog::{Config as SimpleLogConfig, LevelFilter, WriteLogger},
    std::{
        fs::File,
        io::{BufRead, BufReader},
        sync::{Arc, Mutex, RwLock},
    },
};

/// Holds a reference to core objects required by the application to run.
pub struct Scrutiny {
    manager: Arc<Mutex<PluginManager>>,
    dispatcher: Arc<RwLock<ControllerDispatcher>>,
    scheduler: Arc<Mutex<CollectorScheduler>>,
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

        if let Ok(log_file) = File::create(config.runtime.logging.path.clone()) {
            let _ = WriteLogger::init(log_level, SimpleLogConfig::default(), log_file);
        }

        let model = Arc::new(DataModel::new(config.runtime.model.clone())?);
        let dispatcher = Arc::new(RwLock::new(ControllerDispatcher::new(Arc::clone(&model))));
        let scheduler = Arc::new(Mutex::new(CollectorScheduler::new(Arc::clone(&model))));
        let manager = Arc::new(Mutex::new(PluginManager::new(
            Arc::clone(&scheduler),
            Arc::clone(&dispatcher),
        )));
        let shell = Shell::new(
            Arc::clone(&manager),
            Arc::clone(&dispatcher),
            config.runtime.logging.silent_mode,
        );
        Ok(Self { manager, dispatcher, scheduler, shell, config })
    }

    /// Utility function to register a plugin.
    pub fn plugin(&mut self, plugin: impl Plugin + 'static) -> Result<()> {
        let desc = plugin.descriptor().clone();
        self.manager.lock().unwrap().register(Box::new(plugin))?;
        // Only load plugins that are part of the loaded plugins set.
        if self.config.runtime.plugin.plugins.contains(&desc.name()) {
            self.manager.lock().unwrap().load(&desc)?;
        }
        Ok(())
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
    pub fn run(&mut self) -> Result<String> {
        self.scheduler.lock().unwrap().schedule()?;

        if let Some(command) = &self.config.launch.command {
            return self.shell.execute(command.to_string());
        } else if let Some(script) = &self.config.launch.script_path {
            let script_file = BufReader::new(File::open(script)?);
            let mut script_output = String::new();
            for line in script_file.lines() {
                script_output.push_str(&self.shell.execute(line?)?);
            }
            return Ok(script_output);
        } else {
            self.shell.run();
        }
        Ok(String::new())
    }
}
