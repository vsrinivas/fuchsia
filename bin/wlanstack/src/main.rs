// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate failure;
extern crate fuchsia_vfs_watcher;
extern crate futures;
extern crate tokio_core;

use failure::{Error, ResultExt};
use fuchsia_vfs_watcher::*;
use futures::stream::Stream;
use std::fs::File;
use tokio_core::reactor;

const ETH_PATH: &str = "/dev/class/ethernet";

fn main() {
    if let Err(e) = main_res() {
        println!("Error: {:?}", e);
    }
}

fn main_res() -> Result<(), Error> {
    println!("Starting wlanstack2");

    let mut core = reactor::Core::new().context("error creating event loop")?;
    let handle = core.handle();

    let dev_dir = File::open(ETH_PATH)?;
    let w = Watcher::new(&dev_dir, &handle).context("error creating watcher")?.for_each(|msg| {
        match msg.event {
            WatchEvent::EXISTING => println!("{:?} existing", msg.filename),
            WatchEvent::ADD_FILE => println!("{:?} added", msg.filename),
            WatchEvent::REMOVE_FILE => println!("{:?} removed", msg.filename),
            WatchEvent::IDLE => println!("idle"),
            e => println!("unknown watch event: {:?}", e),
        }
        Ok(())
    });

    core.run(w).map_err(|e| e.into())
}
