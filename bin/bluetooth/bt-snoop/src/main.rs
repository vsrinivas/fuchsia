// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(
    async_await,
    await_macro,
    futures_api,
    pin,
    arbitrary_self_types
)]
#![deny(warnings)]

use {
    failure::{Error},
    fuchsia_async::{self as fasync},
    fuchsia_syslog::{self as syslog, fx_log_err, fx_log_info, fx_vlog},
    std::{
        fs::File,
    },
    structopt::StructOpt,
};

mod snooper;

/// Root directory of all HCI devices
static HCI_DEVICE_CLASS_PATH: &str = "/dev/class/bt-hci";

/// Setup the main loop of execution in a Task and run it on an `Executor`.
fn start(_log_size: usize, _hci_dir: File) -> Result<(), Error> {
    let mut exec = fasync::Executor::new().expect("Could not create executor");

    let main_loop = async {
        fx_vlog!(1, "Capturing snoop packets...");
        Ok(())
    };

    exec.run_singlethreaded(main_loop)
}

/// Parse program arguments, call the main loop, and log any unrecoverable errors.
fn main() {
    #[derive(StructOpt)]
    #[structopt(
        version = "0.1.0",
        author = "Fuchsia Bluetooth Team",
        about = "Log bluetooth snoop packets and provide them to clients."
    )]
    struct Opt {
        #[structopt(
            long = "log-size",
            default_value = "256",
            help = "Size in KiB of the buffer to store packets in."
        )]
        log_size_kib: usize,
    }
    let Opt { log_size_kib } = Opt::from_args();
    // convert from KiB to bytes.
    let log_size_bytes = log_size_kib * 1024;

    syslog::init_with_tags(&["bt-snoop"]).expect("Can't init logger");
    fx_log_info!("Starting bt-snoop.");

    let hci_dir = File::open(HCI_DEVICE_CLASS_PATH).expect("Failed to open hci dev directory");

    match start(log_size_bytes, hci_dir) {
        Err(err) => fx_log_err!("Failed with critical error: {:?}", err),
        _ => {}
    };
}
