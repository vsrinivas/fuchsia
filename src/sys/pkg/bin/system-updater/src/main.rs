// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]
#![allow(clippy::let_unit_value)]

use {
    crate::{
        fidl::{FidlServer, UpdateStateNotifier},
        install_manager::start_install_manager,
        update::{NamespaceEnvironmentConnector, RealUpdater, UpdateHistory},
    },
    anyhow::anyhow,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    parking_lot::Mutex,
    std::sync::Arc,
};

mod fidl;
mod install_manager;
pub(crate) mod update;

#[fasync::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init_with_tags(&["system-updater"]).expect("can't init logger");
    fx_log_info!("starting system updater");

    let inspector = fuchsia_inspect::Inspector::new();
    let history_node = inspector.root().create_child("history");

    let history = Arc::new(Mutex::new(UpdateHistory::load(history_node).await));

    let mut fs = ServiceFs::new_local();
    if let Err(e) = fs.take_and_serve_directory_handle() {
        fx_log_err!("error encountered serving directory handle: {:#}", anyhow!(e));
        std::process::exit(1);
    }

    if let Err(e) = inspect_runtime::serve(&inspector, &mut fs) {
        // Almost nothing should be fatal to the system-updater if we can help it.
        fx_log_warn!("Couldn't serve inspect: {:#}", anyhow!(e));
    }

    // The install manager task will run the update attempt task,
    // listen for FIDL events, and notify monitors of update attempt progress.
    let updater = RealUpdater::new(Arc::clone(&history));
    let attempt_node = inspector.root().create_child("current_attempt");
    let (install_manager_ch, install_manager_fut) = start_install_manager::<
        UpdateStateNotifier,
        RealUpdater,
        NamespaceEnvironmentConnector,
    >(updater, attempt_node)
    .await;

    // The FIDL server will forward requests to the install manager task via the control handle.
    let server_fut = FidlServer::new(history, install_manager_ch).run(fs);

    // Start the tasks.
    futures::join!(fasync::Task::local(install_manager_fut), fasync::Task::local(server_fut));
}
