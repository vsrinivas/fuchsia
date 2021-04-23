// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_fuchsia_net_filter::FilterMarker;
use fidl_fuchsia_net_neighbor as neighbor;
use fidl_fuchsia_net_stack::{LogMarker, StackMarker};
use fidl_fuchsia_netstack::NetstackMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use log::{Level, Log, Metadata, Record, SetLoggerError};

/// Logger which prints levels at or below info to stdout and levels at or
/// above warn to stderr.
struct Logger;

const LOG_LEVEL: Level = Level::Info;

impl Log for Logger {
    fn enabled(&self, metadata: &Metadata<'_>) -> bool {
        metadata.level() <= LOG_LEVEL
    }

    fn log(&self, record: &Record<'_>) {
        if self.enabled(record.metadata()) {
            match record.metadata().level() {
                Level::Trace | Level::Debug | Level::Info => println!("{}", record.args()),
                Level::Warn | Level::Error => eprintln!("{}", record.args()),
            }
        }
    }

    fn flush(&self) {}
}

static LOGGER: Logger = Logger;

fn logger_init() -> Result<(), SetLoggerError> {
    log::set_logger(&LOGGER).map(|()| log::set_max_level(LOG_LEVEL.to_level_filter()))
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let () = logger_init()?;
    let command: net_cli::Command = argh::from_env();
    let stack = connect_to_service::<StackMarker>().context("failed to connect to netstack")?;
    let netstack =
        connect_to_service::<NetstackMarker>().context("failed to connect to netstack")?;
    let filter = connect_to_service::<FilterMarker>().context("failed to connect to netfilter")?;
    let log = connect_to_service::<LogMarker>().context("failed to connect to netstack log")?;
    let controller = connect_to_service::<neighbor::ControllerMarker>()
        .context("failed to connect to neighbor controller")?;
    let view = connect_to_service::<neighbor::ViewMarker>()
        .context("failed to connect to neighbor view")?;

    net_cli::do_root(command, stack, netstack, filter, log, controller, view).await
}
