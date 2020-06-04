// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    fuchsia_syslog::{fx_log_err, fx_log_info},
};

mod args;
pub(crate) mod update;

#[fuchsia_async::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init_with_tags(&["system-updater"]).expect("can't init logger");
    fx_log_info!("starting system updater");

    let args: crate::args::Args = argh::from_env();
    let config = update::Config::from_args(args);

    let env = match update::Environment::connect_in_namespace() {
        Ok(env) => env,
        Err(e) => {
            fx_log_err!("Error connecting to services: {:#}", anyhow!(e));
            std::process::exit(1);
        }
    };

    let res = update::update(config, env).await;

    if let Err(()) = res {
        std::process::exit(1);
    }
}
