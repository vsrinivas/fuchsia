// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate failure;
extern crate fuchsia_vfs_watcher;
extern crate futures;
#[macro_use]
extern crate log;
extern crate tokio_core;

mod logger;

use failure::{Error, ResultExt};
use fuchsia_vfs_watcher::*;
use futures::stream::Stream;
use std::fs::File;
use tokio_core::reactor;

const MAX_LOG_LEVEL: log::LogLevelFilter = log::LogLevelFilter::Info;
const DEV_PATH: &str = "/dev/class/ethernet";

fn main() {
    if let Err(e) = main_res() {
        println!("wlanstack2: Error: {:?}", e);
    }
}

fn main_res() -> Result<(), Error> {
    log::set_logger(|max_level| {
        max_level.set(MAX_LOG_LEVEL);
        Box::new(logger::Logger)
    })?;
    info!("Starting");

    let mut core = reactor::Core::new().context("error creating event loop")?;
    let handle = core.handle();

    let dev_dir = File::open(DEV_PATH)?;
    let w = Watcher::new(&dev_dir, &handle).context("error creating watcher")?.for_each(|msg| {
        match msg.event {
            WatchEvent::EXISTING => info!("{}/{} existing", DEV_PATH, msg.filename.to_string_lossy()),
            WatchEvent::ADD_FILE => info!("{}/{} added", DEV_PATH, msg.filename.to_string_lossy()),
            WatchEvent::REMOVE_FILE => info!("{}/{} removed", DEV_PATH, msg.filename.to_string_lossy()),
            WatchEvent::IDLE => info!("device watcher idle"),
            e => info!("unknown watch event: {:?}", e),
        }
        Ok(())
    });

    core.run(w).map_err(|e| e.into())
}
