// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use failure::{Error, ResultExt};
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;
use http_request::FuchsiaHyperHttpRequest;
use log::info;
use omaha_client::state_machine::StateMachine;
use std::cell::RefCell;
use std::rc::Rc;

mod configuration;
mod fidl;
mod http_request;
mod install_plan;
mod installer;
mod metrics;
mod policy;
mod storage;
mod temp_installer;
mod timer;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init().expect("Can't init logger");
    info!("Starting omaha client...");

    let mut executor = fuchsia_async::Executor::new().context("Error creating executor")?;

    executor.run_singlethreaded(async {
        let apps = configuration::get_apps()?;
        info!("Omaha apps: {:?}", apps);
        let config = configuration::get_config();
        info!("Update config: {:?}", config);

        let (_metrics_reporter, cobalt_fut) = metrics::CobaltMetricsReporter::new();

        let http = FuchsiaHyperHttpRequest::new();
        let installer = temp_installer::FuchsiaInstaller::new()?;
        let mut state_machine = StateMachine::new(
            policy::FuchsiaPolicyEngine,
            http,
            installer,
            &config,
            timer::FuchsiaTimer,
        );
        state_machine.add_apps(apps);
        let state_machine_ref = Rc::new(RefCell::new(state_machine));
        let fidl = fidl::FidlServer::new(state_machine_ref.clone());
        let mut fs = ServiceFs::new_local();
        fs.take_and_serve_directory_handle()?;
        await!(future::join3(StateMachine::start(state_machine_ref), fidl.start(fs), cobalt_fut));
        Ok(())
    })
}
