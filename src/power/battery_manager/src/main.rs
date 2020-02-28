// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod battery_manager;
mod power;

use {
    crate::battery_manager::BatteryManager,
    anyhow::Error,
    fidl_fuchsia_power::BatteryManagerRequestStream,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as syslog, fx_log_err, fx_log_info},
    futures::prelude::*,
    std::sync::Arc,
};

static LOG_TAG: &str = "battery_manager";
static LOG_VERBOSITY: i32 = 1;

enum IncomingService {
    BatteryManager(BatteryManagerRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&[LOG_TAG]).expect("Can't init logger");
    fx_log_info!("starting up");

    let mut fs = ServiceFs::new();

    let battery_manager = Arc::new(BatteryManager::new());
    let battery_manager_clone = battery_manager.clone();

    let f = power::watch_power_device(battery_manager_clone);

    fasync::spawn(f.unwrap_or_else(|e| {
        fx_log_err!("watch_power_device failed {:?}", e);
    }));

    fs.dir("svc").add_fidl_service(IncomingService::BatteryManager);

    fs.take_and_serve_directory_handle()?;

    fs.for_each_concurrent(None, |request| {
        let battery_manager = battery_manager.clone();

        async move {
            match request {
                IncomingService::BatteryManager(stream) => {
                    let res = battery_manager.serve(stream).await;
                    if let Err(e) = res {
                        fx_log_err!("BatteryManager failed {}", e);
                    }
                }
            }
        }
    })
    .await;

    fx_log_info!("stopping battery_manager");
    Ok(())
}
