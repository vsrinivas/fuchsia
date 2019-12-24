// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod battery_manager;
mod power;

use crate::battery_manager::BatteryManager;
use anyhow::{Context as _, Error};
use fidl_fuchsia_power as fpower;
use fidl_fuchsia_power_ext::CloneExt;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::{self as syslog, fx_log_err, fx_log_info, fx_vlog};
use futures::prelude::*;
use std::sync::Arc;

static LOG_TAG: &str = "battery_manager";
static LOG_VERBOSITY: i32 = 1;

struct BatteryManagerServer {
    manager: Arc<BatteryManager>,
}

fn spawn_battery_manager_async(
    bms: BatteryManagerServer,
    mut stream: fpower::BatteryManagerRequestStream,
) {
    fasync::spawn(
        async move {
            while let Some(req) = (stream.try_next()).await? {
                match req {
                    fpower::BatteryManagerRequest::GetBatteryInfo { responder, .. } => {
                        let info = bms.manager.get_battery_info_copy();
                        fx_vlog!(
                            LOG_VERBOSITY,
                            "::bms:: handle GetBatteryInfo request with info: {:?}",
                            &info
                        );
                        if let Err(e) = responder.send(info.clone()) {
                            fx_log_err!("failed to respond with battery info {:?}", e);
                        }
                    }
                    fpower::BatteryManagerRequest::Watch { watcher, .. } => {
                        match watcher.into_proxy() {
                            Err(e) => {
                                fx_log_err!("failed to get watcher proxy {:?}", e);
                            }
                            Ok(w) => {
                                let battery_manager = bms.manager.clone();
                                fx_vlog!(LOG_VERBOSITY, "::bms:: handle Watch request");

                                battery_manager.add_watcher(w.clone()).await;

                                // make sure watcher has current battery info
                                let info = battery_manager.get_battery_info_copy();

                                fx_vlog!(
                                    LOG_VERBOSITY,
                                    "::bms:: callback on new watcher with info {:?}",
                                    &info
                                );
                                match (w.on_change_battery_info(info)).await {
                                    Ok(_) => {
                                        fx_vlog!(LOG_VERBOSITY, "::bms:: notified new watcher");
                                    }
                                    Err(e) => fx_log_err!("failed to notify new watcher: {:?}", e),
                                };
                            }
                        }
                    }
                }
            }
            Ok(())
        }
        .unwrap_or_else(|e: anyhow::Error| fx_log_err!("{:?}", e)),
    );
}

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&[LOG_TAG]).expect("Can't init logger");
    fx_log_info!("starting up");

    let mut fs = ServiceFs::new();

    let mut executor = fasync::Executor::new().context("unable to create executor")?;

    let battery_manager = Arc::new(BatteryManager::new());
    let battery_manager_clone = battery_manager.clone();

    let f = power::watch_power_device(battery_manager_clone);

    fasync::spawn(f.unwrap_or_else(|e| {
        fx_log_err!("watch_power_device failed {:?}", e);
    }));

    fs.dir("svc").add_fidl_service(move |stream| {
        let bms = BatteryManagerServer { manager: battery_manager.clone() };
        spawn_battery_manager_async(bms, stream);
    });
    fs.take_and_serve_directory_handle()?;

    let () = executor.run(fs.collect(), 2); // 2 threads
    fx_log_info!("stopping battery_manager");
    Ok(())
}
