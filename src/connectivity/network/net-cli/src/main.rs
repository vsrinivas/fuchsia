// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl::endpoints::ProtocolMarker;
use fidl_fuchsia_hardware_ethernet as fethernet;
use fidl_fuchsia_net_debug as fdebug;
use fidl_fuchsia_net_dhcp as fdhcp;
use fidl_fuchsia_net_filter as ffilter;
use fidl_fuchsia_net_interfaces as finterfaces;
use fidl_fuchsia_net_name as fname;
use fidl_fuchsia_net_neighbor as fneighbor;
use fidl_fuchsia_net_stack as fstack;
use fidl_fuchsia_netstack as fnetstack;
use fidl_fuchsia_sys2 as fsys;
use fuchsia_async as fasync;
use fuchsia_component::client::{connect_to_protocol_at_dir_root, connect_to_protocol_at_path};
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

struct Connector {
    realm_query: fsys::RealmQueryProxy,
}

impl Connector {
    pub fn new() -> Result<Self, Error> {
        let realm_query = connect_to_protocol_at_path::<fsys::RealmQueryMarker>(REALM_QUERY_PATH)?;
        Ok(Self { realm_query })
    }

    async fn connect_to_exposed_protocol<P: fidl::endpoints::DiscoverableProtocolMarker>(
        &self,
        moniker: &str,
    ) -> Result<P::Proxy, Error> {
        let resolved_dirs = self
            .realm_query
            .get_instance_directories(moniker)
            .await?
            .map_err(|e| format_err!("RealmQuery error: {:?}", e))?
            .ok_or(format_err!("{} is not resolved", moniker))?;
        let exposed_dir = resolved_dirs.exposed_dir.into_proxy()?;
        connect_to_protocol_at_dir_root::<P>(&exposed_dir)
    }
}

const REALM_QUERY_PATH: &str = "/svc/fuchsia.sys2.RealmQuery.root";
const NETSTACK_MONIKER: &str = "./core/network/netstack";
const DHCPD_MONIKER: &str = "./core/network/dhcpd";
const DNS_RESOLVER_MONIKER: &str = "./core/network/dns-resolver";

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fdebug::InterfacesMarker> for Connector {
    async fn connect(&self) -> Result<<fdebug::InterfacesMarker as ProtocolMarker>::Proxy, Error> {
        self.connect_to_exposed_protocol::<fdebug::InterfacesMarker>(NETSTACK_MONIKER).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fdhcp::Server_Marker> for Connector {
    async fn connect(&self) -> Result<<fdhcp::Server_Marker as ProtocolMarker>::Proxy, Error> {
        self.connect_to_exposed_protocol::<fdhcp::Server_Marker>(DHCPD_MONIKER).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<ffilter::FilterMarker> for Connector {
    async fn connect(&self) -> Result<<ffilter::FilterMarker as ProtocolMarker>::Proxy, Error> {
        self.connect_to_exposed_protocol::<ffilter::FilterMarker>(NETSTACK_MONIKER).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<finterfaces::StateMarker> for Connector {
    async fn connect(&self) -> Result<<finterfaces::StateMarker as ProtocolMarker>::Proxy, Error> {
        self.connect_to_exposed_protocol::<finterfaces::StateMarker>(NETSTACK_MONIKER).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fneighbor::ControllerMarker> for Connector {
    async fn connect(
        &self,
    ) -> Result<<fneighbor::ControllerMarker as ProtocolMarker>::Proxy, Error> {
        self.connect_to_exposed_protocol::<fneighbor::ControllerMarker>(NETSTACK_MONIKER).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fneighbor::ViewMarker> for Connector {
    async fn connect(&self) -> Result<<fneighbor::ViewMarker as ProtocolMarker>::Proxy, Error> {
        self.connect_to_exposed_protocol::<fneighbor::ViewMarker>(NETSTACK_MONIKER).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fstack::LogMarker> for Connector {
    async fn connect(&self) -> Result<<fstack::LogMarker as ProtocolMarker>::Proxy, Error> {
        self.connect_to_exposed_protocol::<fstack::LogMarker>(NETSTACK_MONIKER).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fstack::StackMarker> for Connector {
    async fn connect(&self) -> Result<<fstack::StackMarker as ProtocolMarker>::Proxy, Error> {
        self.connect_to_exposed_protocol::<fstack::StackMarker>(NETSTACK_MONIKER).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fnetstack::NetstackMarker> for Connector {
    async fn connect(&self) -> Result<<fnetstack::NetstackMarker as ProtocolMarker>::Proxy, Error> {
        self.connect_to_exposed_protocol::<fnetstack::NetstackMarker>(NETSTACK_MONIKER).await
    }
}

#[async_trait::async_trait]
impl net_cli::ServiceConnector<fname::LookupMarker> for Connector {
    async fn connect(&self) -> Result<<fname::LookupMarker as ProtocolMarker>::Proxy, Error> {
        self.connect_to_exposed_protocol::<fname::LookupMarker>(DNS_RESOLVER_MONIKER).await
    }
}

#[async_trait::async_trait]
impl net_cli::NetCliDepsConnector for Connector {
    async fn connect_device(&self, path: &str) -> Result<net_cli::Device, Error> {
        let dev = std::fs::File::open(path)
            .with_context(|| format!("failed to open device at {}", path))?;
        let topological_path =
            fdio::device_get_topo_path(&dev).context("failed to get topological path")?;
        let client = fdio::get_service_handle(dev)?;
        let dev = fidl::endpoints::ClientEnd::<fethernet::DeviceMarker>::new(client);
        Ok(net_cli::Device { topological_path, dev })
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let () = logger_init()?;
    let command: net_cli::Command = argh::from_env();
    let connector = Connector::new()?;
    net_cli::do_root(ffx_writer::Writer::new(None), command, &connector).await
}
