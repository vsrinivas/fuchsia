// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{args::Args, fidl::FidlServer, update::UpdateHistory},
    anyhow::anyhow,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    parking_lot::Mutex,
    std::sync::Arc,
};

mod args;
mod fidl;
pub(crate) mod update;

#[fuchsia_async::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init_with_tags(&["system-updater"]).expect("can't init logger");
    fx_log_info!("starting system updater");

    let args: Args = argh::from_env();
    let history = Arc::new(Mutex::new(UpdateHistory::load().await));

    if args.oneshot {
        oneshot_update(args, Arc::clone(&history)).await;
    } else {
        serve_fidl(history).await;
    }
}

/// In the oneshot code path, we do NOT serve FIDL.
/// Instead, we perform a single update based on the provided CLI args, then exit.
async fn oneshot_update(args: Args, history: Arc<Mutex<UpdateHistory>>) {
    let config = update::Config::from_args(args);

    let env = match update::Environment::connect_in_namespace() {
        Ok(env) => env,
        Err(e) => {
            fx_log_err!("Error connecting to services: {:#}", anyhow!(e));
            std::process::exit(1);
        }
    };

    let res = update::update(config, env, history).await;

    if let Err(()) = res {
        std::process::exit(1);
    }
}

/// In the non-oneshot path, we serve FIDL, ignore any other provided CLI args, and do NOT
/// perform an update on start. Instead, we wait for a FIDL request to start an update.
async fn serve_fidl(history: Arc<Mutex<UpdateHistory>>) {
    let mut fs = ServiceFs::new_local();
    if let Err(e) = fs.take_and_serve_directory_handle() {
        fx_log_err!("error encountered serving directory handle: {:#}", anyhow!(e));
        std::process::exit(1);
    }
    let server = FidlServer::new(history);
    server.run(fs).await;
}
