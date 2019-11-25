// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use log::{error, info};
use structopt::StructOpt;

use fuchsia_async as fasync;
use fuchsia_syslog as syslog;

mod device;
mod nat_tester;
mod sync_manager;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), failure::Error> {
    let device = device::Type::from_args();
    syslog::init().expect("initialising logging");
    let sync = sync_manager::SyncManager::attach(device).await?;
    match nat_tester::NatTester::new(device, sync).run().await {
        Ok(()) => {
            info!("testing completed succesfully");
            Ok(())
        }
        Err(e) => {
            error!("test failed: {:?}", e);
            Err(e)
        }
    }
}
