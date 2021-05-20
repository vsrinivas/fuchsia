// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_hardware_ethernet as zx_eth;
use fidl_fuchsia_net_filter::FilterMarker;
use fidl_fuchsia_net_neighbor as neighbor;
use fidl_fuchsia_net_stack::{LogMarker, StackMarker};
use fidl_fuchsia_netstack::NetstackMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_zircon as zx;
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

struct Connector;

impl net_cli::ServiceConnector<StackMarker> for Connector {
    fn connect(&self) -> Result<<StackMarker as ServiceMarker>::Proxy, Error> {
        connect_to_protocol::<StackMarker>()
    }
}

impl net_cli::ServiceConnector<NetstackMarker> for Connector {
    fn connect(&self) -> Result<<NetstackMarker as ServiceMarker>::Proxy, Error> {
        connect_to_protocol::<NetstackMarker>()
    }
}

impl net_cli::ServiceConnector<FilterMarker> for Connector {
    fn connect(&self) -> Result<<FilterMarker as ServiceMarker>::Proxy, Error> {
        connect_to_protocol::<FilterMarker>()
    }
}

impl net_cli::ServiceConnector<LogMarker> for Connector {
    fn connect(&self) -> Result<<LogMarker as ServiceMarker>::Proxy, Error> {
        connect_to_protocol::<LogMarker>()
    }
}

impl net_cli::ServiceConnector<neighbor::ControllerMarker> for Connector {
    fn connect(&self) -> Result<<neighbor::ControllerMarker as ServiceMarker>::Proxy, Error> {
        connect_to_protocol::<neighbor::ControllerMarker>()
    }
}

impl net_cli::ServiceConnector<neighbor::ViewMarker> for Connector {
    fn connect(&self) -> Result<<neighbor::ViewMarker as ServiceMarker>::Proxy, Error> {
        connect_to_protocol::<neighbor::ViewMarker>()
    }
}

impl net_cli::NetCliDepsConnector for Connector {
    fn connect_device(&self, path: &str) -> Result<net_cli::Device, Error> {
        let dev = std::fs::File::open(path)
            .with_context(|| format!("failed to open device at {}", path))?;
        let topological_path =
            fdio::device_get_topo_path(&dev).context("failed to get topological path")?;
        let fd = std::os::unix::io::IntoRawFd::into_raw_fd(dev);
        let mut client = 0;
        // Safety: the fd supplied to fdio_get_service_handle() must be to a FIDL protocol. In
        // this case, we've safely extracted the fd from a channel to a fuchsia.io/Node for the
        // ethernet device. fdio_get_service_handle() will then close this fd in both the success
        // and error case.
        zx::Status::ok(unsafe { fdio::fdio_sys::fdio_get_service_handle(fd, &mut client) })
            .context("failed to get fdio service handle")?;
        let dev = fidl::endpoints::ClientEnd::<zx_eth::DeviceMarker>::new(
            // Safe because we checked the return status above.
            zx::Channel::from(unsafe { zx::Handle::from_raw(client) }),
        );
        Ok(net_cli::Device { topological_path, dev })
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let () = logger_init()?;
    let command: net_cli::Command = argh::from_env();
    net_cli::do_root(command, &Connector).await
}
