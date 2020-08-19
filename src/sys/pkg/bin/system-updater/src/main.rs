// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        args::Args,
        fidl::{FidlServer, UpdateStateNotifier},
        install_manager::start_install_manager,
        update::{
            EnvironmentConnector, NamespaceEnvironmentConnector, RealUpdater, UpdateHistory,
            Updater,
        },
    },
    anyhow::anyhow,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    futures::prelude::*,
    parking_lot::Mutex,
    std::sync::Arc,
};

mod args;
mod fidl;
mod install_manager;
pub(crate) mod update;

#[fasync::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init_with_tags(&["system-updater"]).expect("can't init logger");
    fx_log_info!("starting system updater");

    let args: Args = argh::from_env();
    let inspector = fuchsia_inspect::Inspector::new();
    let history_node = inspector.root().create_child("history");

    let history = Arc::new(Mutex::new(UpdateHistory::load(history_node).await));

    if args.oneshot {
        // We don't need inspect in oneshot mode, and it won't be served anyway.
        drop(inspector);

        oneshot_update(args, Arc::clone(&history)).await;
    } else {
        serve_fidl(history, inspector).await;
    }
}

/// In the oneshot code path, we do NOT serve FIDL.
/// Instead, we perform a single update based on the provided CLI args, then exit.
async fn oneshot_update(args: Args, history: Arc<Mutex<UpdateHistory>>) {
    let config = update::Config::from_args(args);

    let env = match NamespaceEnvironmentConnector::connect() {
        Ok(env) => env,
        Err(e) => {
            fx_log_err!("Error connecting to services: {:#}", anyhow!(e));
            std::process::exit(1);
        }
    };

    let mut done = false;
    let mut failed = false;
    let (_, attempt) = RealUpdater::new(history).update(config, env, None).await;
    futures::pin_mut!(attempt);
    while let Some(state) = attempt.next().await {
        assert!(!done, "update stream continued after a terminal state");

        if state.is_terminal() {
            done = true;
        }
        if state.is_failure() {
            failed = true;
            // Intentionally don't short circuit on failure.  Dropping attempt before the stream
            // terminates will drop any pending cleanup work the update attempt hasn't had a chance
            // to do yet.
        }
    }
    assert!(done, "update stream did not include a terminal state");

    if failed {
        std::process::exit(1);
    }
}

/// In the non-oneshot path, we serve FIDL, ignore any other provided CLI args, and do NOT
/// perform an update on start. Instead, we wait for a FIDL request to start an update.
async fn serve_fidl(history: Arc<Mutex<UpdateHistory>>, inspector: fuchsia_inspect::Inspector) {
    let mut fs = ServiceFs::new_local();
    if let Err(e) = fs.take_and_serve_directory_handle() {
        fx_log_err!("error encountered serving directory handle: {:#}", anyhow!(e));
        std::process::exit(1);
    }

    if let Err(e) = inspector.serve(&mut fs) {
        // Almost nothing should be fatal to the system-updater if we can help it.
        fx_log_warn!("Couldn't serve inspect: {:#}", anyhow!(e));
    }

    // The install manager task will run the update attempt task,
    // listen for FIDL events, and notify monitors of update attempt progress.
    let updater = RealUpdater::new(Arc::clone(&history));
    let (install_manager_ch, install_manager_fut) =
        start_install_manager::<UpdateStateNotifier, RealUpdater, NamespaceEnvironmentConnector>(
            updater,
        )
        .await;

    // The FIDL server will forward requests to the install manager task via the control handle.
    let server_fut = FidlServer::new(history, install_manager_ch).run(fs);

    // Start the tasks.
    futures::join!(fasync::Task::local(install_manager_fut), fasync::Task::local(server_fut));
}
