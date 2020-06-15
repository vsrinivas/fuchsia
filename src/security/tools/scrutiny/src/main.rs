// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    scrutiny::{
        engine::{dispatcher::ControllerDispatcher, manager::PluginManager, pool::CollectorPool},
        model::model::DataModel,
        plugins::components::graph::ComponentGraphPlugin,
        plugins::health::HealthPlugin,
        rest::service,
    },
    simplelog::{Config, LevelFilter, TermLogger, TerminalMode},
    std::sync::{Arc, Mutex, RwLock},
    termion::color,
};

pub struct App {
    manager: PluginManager,
    dispatcher: Arc<RwLock<ControllerDispatcher>>,
    collector: Arc<Mutex<CollectorPool>>,
}

impl App {
    pub fn new() -> Result<Self> {
        TermLogger::init(LevelFilter::Info, Config::default(), TerminalMode::Mixed).unwrap();
        let model = Arc::new(DataModel::connect()?);
        let dispatcher = Arc::new(RwLock::new(ControllerDispatcher::new(Arc::clone(&model))));
        let collector = Arc::new(Mutex::new(CollectorPool::new(Arc::clone(&model))));
        let manager = PluginManager::new(Arc::clone(&collector), Arc::clone(&dispatcher));
        Ok(Self { manager, dispatcher, collector })
    }

    fn setup(&mut self) -> Result<()> {
        self.manager.register_and_load(Box::new(HealthPlugin::new()))?;
        self.manager.register_and_load(Box::new(ComponentGraphPlugin::new()))?;
        self.collector.lock().unwrap().schedule()?;
        Ok(())
    }

    pub fn run(&mut self) -> Result<()> {
        service::run(self.dispatcher.clone());
        Ok(())
    }
}

fn print_logo() {
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

/// Scrutiny is still a WIP this is just a placeholder for setting up the
/// build targets.
fn main() -> Result<()> {
    print_logo();
    let mut app = App::new()?;
    app.setup()?;
    app.run()
}
