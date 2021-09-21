// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl::endpoints::ProtocolMarker;
use fidl_fuchsia_hardware_ethernet as fethernet;
use fidl_fuchsia_net_filter as ffilter;
use fidl_fuchsia_net_neighbor as fneighbor;
use fidl_fuchsia_net_stack as fstack;
use fidl_fuchsia_netstack as fnetstack;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol_at;
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

// Path to hub-v2 netstack exposed directory.
const NETSTACK_EXPOSED_DIR: &str =
    "/hub-v2/children/core/children/network/children/netstack/exec/expose";

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fstack::StackMarker> for Connector {
    async fn connect(&self) -> Result<<fstack::StackMarker as ProtocolMarker>::Proxy, Error> {
        connect_to_protocol_at::<fstack::StackMarker>(NETSTACK_EXPOSED_DIR)
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fnetstack::NetstackMarker> for Connector {
    async fn connect(&self) -> Result<<fnetstack::NetstackMarker as ProtocolMarker>::Proxy, Error> {
        connect_to_protocol_at::<fnetstack::NetstackMarker>(NETSTACK_EXPOSED_DIR)
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<ffilter::FilterMarker> for Connector {
    async fn connect(&self) -> Result<<ffilter::FilterMarker as ProtocolMarker>::Proxy, Error> {
        connect_to_protocol_at::<ffilter::FilterMarker>(NETSTACK_EXPOSED_DIR)
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fstack::LogMarker> for Connector {
    async fn connect(&self) -> Result<<fstack::LogMarker as ProtocolMarker>::Proxy, Error> {
        connect_to_protocol_at::<fstack::LogMarker>(NETSTACK_EXPOSED_DIR)
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fneighbor::ControllerMarker> for Connector {
    async fn connect(
        &self,
    ) -> Result<<fneighbor::ControllerMarker as ProtocolMarker>::Proxy, Error> {
        connect_to_protocol_at::<fneighbor::ControllerMarker>(NETSTACK_EXPOSED_DIR)
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fneighbor::ViewMarker> for Connector {
    async fn connect(&self) -> Result<<fneighbor::ViewMarker as ProtocolMarker>::Proxy, Error> {
        connect_to_protocol_at::<fneighbor::ViewMarker>(NETSTACK_EXPOSED_DIR)
    }
}

#[async_trait::async_trait]
impl net_cli::NetCliDepsConnector for Connector {
    async fn connect_device(&self, path: &str) -> Result<net_cli::Device, Error> {
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
        let dev = fidl::endpoints::ClientEnd::<fethernet::DeviceMarker>::new(
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
