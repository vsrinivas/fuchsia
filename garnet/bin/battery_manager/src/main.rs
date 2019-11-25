// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod battery_manager;
mod power;

use crate::battery_manager::BatteryManager;
use failure::{Error, ResultExt};
use fidl_fuchsia_power as fpower;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::{self as syslog, fx_log_err, fx_log_info};
use futures::prelude::*;
use parking_lot::Mutex;
use std::convert::Into;
use std::sync::Arc;

static LOG_TAG: &str = "battery_manager";
static LOG_VERBOSITY: i32 = 1;

struct BatteryManagerServer {
    manager: Arc<Mutex<BatteryManager>>,
}

fn spawn_battery_manager(
    bms: BatteryManagerServer,
    mut stream: fpower::BatteryManagerRequestStream,
) {
    fasync::spawn(
        async move {
            while let Some(req) = (stream.try_next()).await? {
                match req {
                    fpower::BatteryManagerRequest::GetBatteryInfo { responder, .. } => {
                        let battery_manager = bms.manager.lock();
                        fx_log_info!("BatteryManagerServer handle GetBatteryInfo request");
                        if let Err(e) =
                            responder.send(battery_manager.get_battery_info_copy().into())
                        {
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
                                fx_log_info!(
                                    "BatterManagerServer handle Watch request for watcher {:?}",
                                    &w
                                );

                                battery_manager.lock().add_watcher(w.clone());

                                let info = { battery_manager.lock().get_battery_info_copy() };
                                match (w.on_change_battery_info(info.clone().into())).await {
                                    Ok(_) => {}
                                    Err(e) => fx_log_err!("failed to add watcher: {:?}", e),
                                };
                            }
                        }
                    }
                }
            }
            Ok(())
        }
        .unwrap_or_else(|e: failure::Error| fx_log_err!("{:?}", e)),
    );
}

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&[LOG_TAG]).expect("Can't init logger");
    fx_log_info!("starting up");

    let mut fs = ServiceFs::new();

    let mut executor = fasync::Executor::new().context("unable to create executor")?;

    let battery_manager = Arc::new(Mutex::new(BatteryManager::new()));
    let battery_manager_clone = battery_manager.clone();

    let f = power::watch_power_device(battery_manager_clone);

    fasync::spawn(f.unwrap_or_else(|e| {
        fx_log_err!("watch_power_device failed {:?}", e);
    }));

    fs.dir("svc").add_fidl_service(move |stream| {
        let bms = BatteryManagerServer { manager: battery_manager.clone() };
        spawn_battery_manager(bms, stream);
    });
    fs.take_and_serve_directory_handle()?;

    let () = executor.run(fs.collect(), 2); // 2 threads
    fx_log_info!("stopping battery_manager");
    Ok(())
}
