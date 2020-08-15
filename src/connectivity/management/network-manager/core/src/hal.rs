// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A simple port manager.

use {
    crate::address::{subnet_mask_to_prefix_length, to_ip_addr, LifIpAddr},
    crate::error,
    crate::lifmgr::{self, DhcpAddressPool, LIFProperties},
    crate::oir,
    anyhow::{Context as _, Error},
    fidl_fuchsia_net as fnet,
    fidl_fuchsia_net_dhcp::{self as fnetdhcp, Server_Marker, Server_Proxy},
    fidl_fuchsia_net_name::{
        DnsServerWatcherMarker, DnsServerWatcherProxy, LookupAdminMarker, LookupAdminProxy,
    },
    fidl_fuchsia_net_stack::{
        self as stack, ForwardingDestination, ForwardingEntry, InterfaceInfo, StackMarker,
        StackProxy,
    },
    fidl_fuchsia_net_stack_ext::FidlReturn,
    fidl_fuchsia_netstack::{self as netstack, NetstackMarker, NetstackProxy},
    fidl_fuchsia_router_config as netconfig,
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon as zx,
    futures::{stream, StreamExt, TryStreamExt},
    std::convert::TryFrom,
    std::net::IpAddr,
};

/// The port ID's used by the netstack.
///
/// This is what is passed in the stack FIDL to denote the port or nic id.
#[derive(Eq, PartialEq, Hash, Debug, Copy, Clone)]
pub struct StackPortId(u64);
impl From<u64> for StackPortId {
    fn from(p: u64) -> StackPortId {
        StackPortId(p)
    }
}
impl From<PortId> for StackPortId {
    fn from(p: PortId) -> StackPortId {
        // TODO(dpradilla): This should be based on the mapping between physical location and
        // logical (from management plane point of view) port id.
        StackPortId::from(p.to_u64())
    }
}
impl StackPortId {
    /// Performs the conversion to `u64`, some FIDL interfaces need the ID as a `u64`.
    pub fn to_u64(self) -> u64 {
        self.0
    }
    /// Performs the conversion to `u32`, some FIDL interfaces need the ID as a `u32`.
    pub fn to_u32(self) -> u32 {
        match u32::try_from(self.0) {
            Ok(v) => v,
            e => {
                warn!("overflow converting StackPortId {:?}: {:?}", self.0, e);
                self.0 as u32
            }
        }
    }
}

#[derive(Eq, PartialEq, Hash, Debug, Copy, Clone)]
pub struct PortId(u64);
impl From<u64> for PortId {
    fn from(p: u64) -> PortId {
        PortId(p)
    }
}
impl From<StackPortId> for PortId {
    // TODO(dpradilla): This should be based on the mapping between physical location and
    // logical (from management plane point of view) port id.
    fn from(p: StackPortId) -> PortId {
        PortId(p.to_u64())
    }
}
impl PortId {
    /// Performs the conversion to `u64`, some FIDL interfaces need the ID as a `u64`.
    pub fn to_u64(self) -> u64 {
        self.0
    }

    /// Performs the conversion to `u32`, some FIDL interfaces need the ID as a `u32`.
    pub fn to_u32(self) -> u32 {
        match u32::try_from(self.0) {
            Ok(v) => v,
            e => {
                warn!("overflow converting StackPortId {:?}: {:?}", self.0, e);
                self.0 as u32
            }
        }
    }
}

/// Route table entry.
#[derive(PartialEq, Debug, Clone)]
pub struct Route {
    /// Target network address.
    pub target: LifIpAddr,
    /// Next hop to reach `target`.
    pub gateway: Option<IpAddr>,
    /// Port to reach `gateway`.
    pub port_id: Option<PortId>,
    /// Represents the route priority.
    pub metric: Option<u32>,
}

impl From<&ForwardingEntry> for Route {
    fn from(r: &ForwardingEntry) -> Self {
        let (gateway, port_id) = match r.destination {
            ForwardingDestination::DeviceId(id) => (None, Some(PortId::from(id))),
            ForwardingDestination::NextHop(gateway) => (Some(to_ip_addr(gateway)), None),
        };
        Route {
            target: LifIpAddr { address: to_ip_addr(r.subnet.addr), prefix: r.subnet.prefix_len },
            gateway,
            port_id,
            metric: None,
        }
    }
}

impl From<&netstack::RouteTableEntry2> for Route {
    fn from(r: &netstack::RouteTableEntry2) -> Self {
        Route {
            target: LifIpAddr {
                address: to_ip_addr(r.destination),
                prefix: subnet_mask_to_prefix_length(r.netmask),
            },
            gateway: r.gateway.as_ref().map(|g| to_ip_addr(**g)),
            port_id: Some(StackPortId::from(r.nicid as u64).into()),
            metric: Some(r.metric),
        }
    }
}

pub struct NetCfg {
    stack: StackProxy,
    netstack: NetstackProxy,
    lookup_admin: LookupAdminProxy,
    // NOTE: The DHCP server component does not support applying different configurations to
    // multiple interfaces. To avoid managing the lifetimes of multiple components, limit to
    // running at most one DHCP server at a time. The only use case for DHCP server (OOBE through
    // softAP) only requires one running server.
    dhcp_server: Option<(PortId, Server_Proxy)>,
}

#[derive(Debug)]
pub struct Port {
    pub id: PortId,
    pub path: String,
}

#[derive(Debug, Eq, PartialEq)]
pub enum InterfaceAddress {
    Unknown(LifIpAddr),
    Static(LifIpAddr),
    Dhcp(LifIpAddr),
}

#[derive(Debug, Eq, PartialEq)]
pub enum InterfaceState {
    Unknown,
    Up,
    Down,
}

#[derive(Debug, Eq, PartialEq)]
pub struct Interface {
    pub id: PortId,
    pub topo_path: String,
    pub name: String,
    pub ipv4_addr: Option<InterfaceAddress>,
    pub ipv6_addr: Vec<LifIpAddr>,
    pub enabled: bool,
    pub state: InterfaceState,
    pub dhcp_client_enabled: bool,
}

impl Interface {
    pub fn get_address_v4(&self) -> Option<LifIpAddr> {
        self.ipv4_addr.as_ref().map(|a| match a {
            InterfaceAddress::Unknown(a) => *a,
            InterfaceAddress::Static(a) => *a,
            InterfaceAddress::Dhcp(a) => *a,
        })
    }

    pub fn get_address_v6(&self) -> Vec<LifIpAddr> {
        self.ipv6_addr.clone()
    }
}

fn address_is_valid_unicast(addr: &IpAddr) -> bool {
    // TODO(guzt): This should also check for special-purpose addresses as defined by rfc6890.
    !addr.is_loopback() && !addr.is_unspecified() && !addr.is_multicast()
}

impl From<&InterfaceInfo> for Interface {
    fn from(iface: &InterfaceInfo) -> Self {
        Interface {
            id: iface.id.into(),
            topo_path: iface.properties.topopath.clone(),
            name: iface.properties.name.clone(),
            ipv4_addr: iface
                .properties
                .addresses
                .iter()
                .filter_map(|a| match a.addr {
                    // Only return interfaces with an IPv4 address
                    // TODO(dpradilla) support interfaces with multiple IPs? (is there
                    // a use case given this context?)
                    fnet::IpAddress::Ipv4(_) => {
                        if address_is_valid_unicast(&LifIpAddr::from(&a.addr).address) {
                            Some(InterfaceAddress::Unknown(LifIpAddr::from(a)))
                        } else {
                            None
                        }
                    }
                    _ => None,
                })
                .next(),
            ipv6_addr: iface
                .properties
                .addresses
                .iter()
                .filter_map(|a| match a.addr {
                    // Only return Ipv6 addresses
                    fnet::IpAddress::Ipv6(_) => Some(LifIpAddr::from(a)),
                    _ => None,
                })
                .collect(),

            enabled: match iface.properties.administrative_status {
                stack::AdministrativeStatus::Enabled => true,
                stack::AdministrativeStatus::Disabled => false,
            },
            state: match iface.properties.physical_status {
                stack::PhysicalStatus::Up => InterfaceState::Up,
                stack::PhysicalStatus::Down => InterfaceState::Down,
            },
            dhcp_client_enabled: false,
        }
    }
}

fn valid_unicast_address_or_none(addr: LifIpAddr) -> Option<LifIpAddr> {
    if address_is_valid_unicast(&addr.address) {
        Some(addr)
    } else {
        None
    }
}

impl From<&netstack::NetInterface> for Interface {
    fn from(iface: &netstack::NetInterface) -> Self {
        let dhcp = iface.flags & netstack::NET_INTERFACE_FLAG_DHCP != 0;
        let addr = valid_unicast_address_or_none(LifIpAddr {
            address: to_ip_addr(iface.addr),
            prefix: subnet_mask_to_prefix_length(iface.netmask),
        });
        Interface {
            id: PortId(iface.id.into()),
            topo_path: iface.name.clone(),
            name: iface.name.clone(),
            ipv4_addr: addr.map(|a| {
                if dhcp {
                    InterfaceAddress::Dhcp(a)
                } else {
                    InterfaceAddress::Static(a)
                }
            }),
            ipv6_addr: iface.ipv6addrs.iter().map(|a| LifIpAddr::from(a)).collect(),
            enabled: (iface.flags & netstack::NET_INTERFACE_FLAG_UP) != 0,
            state: InterfaceState::Unknown,
            dhcp_client_enabled: dhcp,
        }
    }
}

impl Into<LIFProperties> for Interface {
    fn into(self) -> LIFProperties {
        LIFProperties {
            dhcp: if self.dhcp_client_enabled { lifmgr::Dhcp::Client } else { lifmgr::Dhcp::None },
            dhcp_config: None,
            address_v4: self.get_address_v4(),
            address_v6: self.get_address_v6(),
            enabled: self.enabled,
        }
    }
}

impl NetCfg {
    pub fn new() -> Result<Self, Error> {
        let stack = connect_to_service::<StackMarker>()
            .context("network_manager failed to connect to netstack")?;
        let netstack = connect_to_service::<NetstackMarker>()
            .context("network_manager failed to connect to netstack")?;
        let lookup_admin = connect_to_service::<LookupAdminMarker>()
            .context("network_manager failed to connect to lookup admin")?;
        Ok(NetCfg { stack, netstack, lookup_admin, dhcp_server: None })
    }

    /// Returns a client for a `fuchsia.net.name.DnsServerWatcher` from the
    /// netstack.
    pub fn get_netstack_dns_server_watcher(&mut self) -> error::Result<DnsServerWatcherProxy> {
        let (dns_server_watcher, dns_server_watcher_req) = fidl::endpoints::create_proxy::<
            DnsServerWatcherMarker,
        >()
        .map_err(|e| error::Service::FidlError {
            msg: format!("error creating dns server watcher: {}", e),
        })?;
        let () = self.stack.get_dns_server_watcher(dns_server_watcher_req).map_err(|e| {
            error::Service::FidlError { msg: format!("error getting dns server watcher: {}", e) }
        })?;
        Ok(dns_server_watcher)
    }

    /// Returns event streams for fuchsia.fnet.stack and fuchsia.netstack.
    pub fn take_event_streams(
        &mut self,
    ) -> (stack::StackEventStream, netstack::NetstackEventStream) {
        (self.stack.take_event_stream(), self.netstack.take_event_stream())
    }

    /// Returns the interface associated with the specified port.
    pub async fn get_interface(&mut self, port: u64) -> Option<Interface> {
        match self.stack.get_interface_info(port).await {
            Ok(Ok(info)) => Some((&info).into()),
            _ => None,
        }
    }

    /// Returns all physical ports in the system.
    pub async fn ports(&self) -> error::Result<Vec<Port>> {
        let ports = self.stack.list_interfaces().await.map_err(|_| error::Hal::OperationFailed)?;
        let p = ports
            .into_iter()
            .filter(|x| x.properties.topopath != "")
            .map(|x| Port { id: StackPortId::from(x.id).into(), path: x.properties.topopath })
            .collect::<Vec<Port>>();
        Ok(p)
    }

    /// Returns all L3 interfaces with valid, non-local IPs in the system.
    pub async fn interfaces(&mut self) -> error::Result<Vec<Interface>> {
        let ifs = self
            .stack
            .list_interfaces()
            .await
            .map_err(|_| error::Hal::OperationFailed)?
            .iter()
            .map(|i| i.into())
            .filter(|i: &Interface| i.ipv4_addr.is_some())
            .collect();
        Ok(ifs)
    }

    /// Creates a new interface, bridging the given ports.
    pub async fn create_bridge(&mut self, ports: Vec<PortId>) -> error::Result<Interface> {
        let (error, bridge_id) = self
            .netstack
            .bridge_interfaces(
                &ports.into_iter().map(|id| StackPortId::from(id).to_u32()).collect::<Vec<_>>(),
            )
            .await
            .map_err(|e| {
                error!("Failed creating bridge {:?}", e);
                error::NetworkManager::Hal(error::Hal::OperationFailed)
            })?;
        if error.status != netstack::Status::Ok {
            error!("Failed creating bridge {:?}", error);
            return Err(error::NetworkManager::Hal(error::Hal::OperationFailed));
        }
        info!("bridge created {:?}", bridge_id);
        if let Some(i) = self.get_interface(bridge_id.into()).await {
            Ok(i)
        } else {
            Err(error::NetworkManager::Hal(error::Hal::BridgeNotFound))
        }
    }

    /// Deletes the given bridge.
    pub async fn delete_bridge(&mut self, id: PortId) -> error::Result<()> {
        // TODO(dpradilla): what is the API for deleting a bridge? Call it
        info!("delete_bridge {:?} - Noop for now", id);
        Ok(())
    }

    /// Configures an IP address.
    pub async fn set_ip_address<'a>(
        &'a mut self,
        pid: PortId,
        addr: &'a LifIpAddr,
    ) -> error::Result<()> {
        let r = self
            .stack
            .add_interface_address(StackPortId::from(pid).to_u64(), &mut addr.into())
            .await;
        info!("set_ip_address {:?}: {:?}: {:?}", pid, addr, r);
        r.squash_result().map_err(|_| error::NetworkManager::Hal(error::Hal::OperationFailed))
    }

    /// Removes an IP address.
    pub async fn unset_ip_address<'a>(
        &'a mut self,
        pid: PortId,
        addr: &'a LifIpAddr,
    ) -> error::Result<()> {
        let a: netconfig::CidrAddress = addr.into();
        // TODO(dpradilla): this needs to be changed to use the stack fidl once
        // this functionality is moved there. the u32 conversion won't be needed.
        let r = self
            .netstack
            .remove_interface_address(
                pid.to_u32(),
                &mut a.address.unwrap(),
                a.prefix_length.unwrap(),
            )
            .await;
        info!("unset_ip_address {:?}: {:?}: {:?}", pid, addr, r);
        r.map_err(|_| error::NetworkManager::Hal(error::Hal::OperationFailed))?;

        Ok(())
    }

    /// Sets the state of an interface.
    ///
    /// `state` controls whether the given `PortId` is enabled or disabled.
    pub async fn set_interface_state(&mut self, pid: PortId, state: bool) -> error::Result<()> {
        let r = if state {
            self.stack.enable_interface(StackPortId::from(pid).to_u64())
        } else {
            self.stack.disable_interface(StackPortId::from(pid).to_u64())
        }
        .await;

        r.squash_result()
            .with_context(|| "failed setting interface state".to_string())
            .map_err(|_| error::NetworkManager::Hal(error::Hal::OperationFailed))
    }

    /// Sets the state of the DHCP client on the specified interface.
    ///
    /// `enable` controls whether the given `PortId` has a DHCP client started or stopped.
    pub async fn set_dhcp_client_state(&mut self, pid: PortId, enable: bool) -> error::Result<()> {
        let (dhcp_client, server_end) = fidl::endpoints::create_proxy::<fnetdhcp::ClientMarker>()
            .context("dhcp client: failed to create fidl endpoints")?;
        if let Err(e) = self.netstack.get_dhcp_client(pid.to_u32(), server_end).await {
            warn!("failed to create fidl endpoint for dhch client: {:?}", e);
            return Err(error::NetworkManager::Hal(error::Hal::OperationFailed));
        }
        let r = if enable {
            dhcp_client.start().await.context("failed to start DHCP client")?
        } else {
            dhcp_client.stop().await.context("failed to stop DHCP client")?
        };
        if let Err(e) = r {
            warn!("failed to start DHCP client: {:?}", e);
            return Err(error::NetworkManager::Hal(error::Hal::OperationFailed));
        }
        info!("DHCP client on nicid: {}, enabled: {}", pid.to_u32(), enable);
        Ok(())
    }

    /// Gets a FIDL connection to the DHCP server running on the interface specified by `pid`.
    ///
    /// This function lazily enables a DHCP server if no servers are currently enabled.
    ///
    /// # Errors
    ///
    /// Returns an error if
    ///   * a server is enabled on another interface
    ///   * failed to enable a new server when no servers are running
    fn get_dhcp_server(&mut self, pid: PortId) -> error::Result<&Server_Proxy> {
        if self.dhcp_server.is_none() {
            let server_proxy = connect_to_service::<Server_Marker>().map_err(|e| {
                warn!("failed to launch DHCP server component: {:?}", e);
                error::NetworkManager::Hal(error::Hal::OperationFailed)
            })?;
            self.dhcp_server = Some((pid, server_proxy));
        }

        match self.dhcp_server.as_ref() {
            Some((current_pid, server_proxy)) => {
                if *current_pid == pid {
                    Ok(server_proxy)
                } else {
                    warn!("at most one DHCP server is allowed: a server is currently enabled on interface {:?}, requesting a new server on interface {:?}", current_pid, pid);
                    Err(error::NetworkManager::Hal(error::Hal::OperationFailed))
                }
            }
            None => unreachable!(),
        }
    }

    /// Starts the DHCP server on the interface specified by `pid`.
    ///
    /// # Errors
    ///
    /// Returns an error if
    ///   * a server is enabled on another interface
    ///   * failed to start the server through FIDL
    async fn start_dhcp_server(&mut self, pid: PortId) -> error::Result<()> {
        self.get_dhcp_server(pid)?
            .start_serving()
            .await
            .map_err(|e| {
                warn!("fidl query failed: {}", e);
                error::NetworkManager::Hal(error::Hal::OperationFailed)
            })?
            .map(|_| ())
            .map_err(|e| {
                warn!(
                    "failed to start DHCP server: {}",
                    fuchsia_zircon::Status::from_raw(e).to_string()
                );
                error::NetworkManager::Hal(error::Hal::OperationFailed)
            })
    }

    /// Stops the DHCP server on the interface specified by `pid`.
    ///
    /// NOTE: This doesn't disable the server, only puts it in a stopped state. This function is
    /// used to soft restart the server after new parameters are set, so they can take effect. In
    /// order to disable a server and start a new one on another interface, use
    /// `disable_dhcp_server`.
    ///
    /// # Errors
    ///
    /// Returns an error if
    ///   * a server is enabled on another interface
    ///   * failed to stop the server through FIDL
    async fn stop_dhcp_server(&mut self, pid: PortId) -> error::Result<()> {
        self.get_dhcp_server(pid)?.stop_serving().await.map_err(|e| {
            warn!("failed to stop DHCP server: {:?}", e);
            error::NetworkManager::Hal(error::Hal::OperationFailed)
        })
    }

    /// Disables the DHCP server on the interface specified by `pid`.
    ///
    /// Different from `stop_dhcp_server`, this function drops the FIDL connection to the current
    /// DHCP server, allowing new servers to be enabled on other interfaces after.
    ///
    /// # Errors
    ///
    /// Returns an error if
    ///   * the input `pid` doesn't match the interface the current server is enabled on
    ///   * failed to stop the server
    async fn disable_dhcp_server(&mut self, pid: PortId) -> error::Result<()> {
        match self.dhcp_server {
            Some((current_pid, _)) => {
                if current_pid == pid {
                    self.stop_dhcp_server(pid).await?;
                    self.dhcp_server = None;
                    Ok(())
                } else {
                    warn!("DHCP server is running on interface {:?}, requesting to disable server on interface {:?}", current_pid, pid);
                    Err(error::NetworkManager::Hal(error::Hal::OperationFailed))
                }
            }
            None => {
                warn!("no running DHCP server to disable");
                Ok(())
            }
        }
    }

    /// Sets the state of the DHCP server on the specified interface.
    ///
    /// `enable` controls whether the interface specified by `pid` has a DHCP server enabled or
    /// not. Re-enabling a running server will cause a restart.
    ///
    /// # Errors
    ///
    /// Returns an error if
    ///   * a server is enabled on another interface
    ///   * failed to start/stop server through FIDL
    pub async fn set_dhcp_server_state(&mut self, pid: PortId, enable: bool) -> error::Result<()> {
        // TODO(jayzhuang): add an optimization to only restart the server if any parameters have
        // changed.
        if enable {
            self.stop_dhcp_server(pid).await?;
            let iface_info = self.get_interface_info(pid).await?;
            let addrs = iface_info
                .properties
                .addresses
                .into_iter()
                .filter_map(|addr| match addr.addr {
                    fnet::IpAddress::Ipv4(addr) => Some(addr),
                    fnet::IpAddress::Ipv6(_) => None,
                })
                .collect::<Vec<_>>();
            self.set_dhcp_server_parameter(pid, &mut fnetdhcp::Parameter::IpAddrs(addrs)).await?;
            self.set_dhcp_server_parameter(
                pid,
                &mut fnetdhcp::Parameter::BoundDeviceNames(vec![String::from(
                    iface_info.properties.name,
                )]),
            )
            .await?;
            self.start_dhcp_server(pid).await?;
        } else {
            self.disable_dhcp_server(pid).await?;
        }
        info!("set_dhcp_server_state pid: {:?} enable: {}", pid, enable);
        Ok(())
    }

    /// Validates and extracts DHCP server options, parameters and the desired enable state from
    /// input configuration.
    ///
    /// # Errors
    ///
    /// Returns an error if the input configuration is invalid.
    async fn dhcp_server_config_to_option_and_param(
        &self,
        pid: PortId,
        config: &lifmgr::DhcpServerConfig,
    ) -> error::Result<(Vec<fnetdhcp::Option_>, Vec<fnetdhcp::Parameter>, Option<bool>)> {
        let mut options = Vec::new();
        let mut params = Vec::new();

        let lifmgr::DhcpServerConfig {
            options:
                lifmgr::DhcpServerOptions { id: _, default_gateway, dns_server, enable, lease_time_sec },
            pool,
            reservations,
        } = config;

        if let Some(default_gateway) = default_gateway {
            options.push(fnetdhcp::Option_::Router(vec![*default_gateway]));
        }

        if let Some(dns_server) = dns_server {
            options.push(fnetdhcp::Option_::DomainNameServer(
                dns_server
                    .servers
                    .iter()
                    .filter_map(|addr| match addr.address {
                        IpAddr::V4(addr) => Some(fnet::Ipv4Address { addr: addr.octets() }),
                        IpAddr::V6(_) => None,
                    })
                    .collect(),
            ));
        }

        if let Some(lease_time_sec) = lease_time_sec {
            params.push(fnetdhcp::Parameter::Lease(fnetdhcp::LeaseLength {
                default: Some(*lease_time_sec),
                max: None,
            }));
        }

        if let Some(DhcpAddressPool { start, end, .. }) = pool {
            let if_info = self.get_interface_info(pid).await?;

            // NOTE: network and mask are not specified in the input configuration, so use the
            // first address from the interface.
            let (addr, prefix_len) = if_info
                .properties
                .addresses
                .into_iter()
                .filter_map(|if_addr| match if_addr.addr {
                    fnet::IpAddress::Ipv4(addr) => Some((addr.addr, if_addr.prefix_len)),
                    fnet::IpAddress::Ipv6(_) => None,
                })
                .next()
                .ok_or({
                    warn!("can't add address pool: no address found on interface {:?}", pid);
                    error::NetworkManager::Hal(error::Hal::OperationFailed)
                })?;

            let subnet = net_types::ip::AddrSubnet::<_, net_types::SpecifiedAddr<_>>::new(
                net_types::ip::Ipv4Addr::new(addr),
                prefix_len,
            )
            .map_err(|e| {
                warn!("can't get subnet from interface address: {:?}", e);
                error::NetworkManager::Hal(error::Hal::OperationFailed)
            })?
            .into_subnet();

            for addr in &[start, end] {
                if !subnet.contains(&net_types::ip::Ipv4Addr::new(addr.octets())) {
                    warn!(
                        "address pool range {}-{}, does not match subnet {} of interface {:?}",
                        start, end, subnet, pid
                    );
                    return Err(error::NetworkManager::Hal(error::Hal::OperationFailed));
                }
            }

            params.push(fnetdhcp::Parameter::AddressPool(fnetdhcp::AddressPool {
                network_id: Some(fnet::Ipv4Address { addr: subnet.network().ipv4_bytes() }),
                broadcast: Some(fnet::Ipv4Address { addr: subnet.broadcast().ipv4_bytes() }),
                mask: Some(fnet::Ipv4Address {
                    addr: (u32::MAX << (32 - subnet.prefix())).to_be_bytes(),
                }),
                pool_range_start: Some(fnet::Ipv4Address { addr: start.octets() }),
                pool_range_stop: Some(fnet::Ipv4Address { addr: end.octets() }),
            }));
        }

        params.push(fnetdhcp::Parameter::StaticallyAssignedAddrs(
            reservations
                .iter()
                .map(|reservation| fnetdhcp::StaticAssignment {
                    host: Some(fnet::MacAddress { octets: reservation.mac.to_array() }),
                    assigned_addr: Some(fnet::Ipv4Address { addr: reservation.address.octets() }),
                })
                .collect(),
        ));

        Ok((options, params, *enable))
    }

    /// Sets the configuration of the DHCP server on the specified interface.
    ///
    /// This function is no-op if input configuration is `None`.
    ///
    /// NOTE: This operation is not atomic. The server will be stopped before applying
    /// configurations, if any. Failure to apply any configuration will leave the server in the
    /// stopped state, so we don't have a DHCP server with undefined configuration serving
    /// addresses.
    ///
    /// # Errors
    ///
    /// Returns an error if
    ///     * a DHCP server is enabled on another interface
    ///     * input configuration is invalid
    ///     * failed to apply any of the configurations through FIDL
    pub async fn set_dhcp_server_config(
        &mut self,
        pid: PortId,
        current: &Option<lifmgr::DhcpServerConfig>,
        desired: &Option<lifmgr::DhcpServerConfig>,
    ) -> error::Result<()> {
        if let Some(config) = desired {
            // Try to validate and convert the input configuration before actually applying any of
            // them.
            let (options, params, enable) =
                self.dhcp_server_config_to_option_and_param(pid, config).await?;

            let enable = match enable {
                Some(enable) => enable,
                None => {
                    // No explicit server state is included in the new config, so try to maintain
                    // the current state from previous configuration.
                    match current {
                        Some(lifmgr::DhcpServerConfig {
                            options: lifmgr::DhcpServerOptions { enable: Some(enable), .. },
                            ..
                        }) => *enable,
                        // If a desired state is never specified (previous state is also None),
                        // leave the server disabled.
                        _ => false,
                    }
                }
            };

            // It's not necessary to go through the configurations because the server will
            // eventually be disabled anyways.
            if !enable {
                self.disable_dhcp_server(pid).await?;
                return Ok(());
            }

            if options.len() + params.len() > 0 {
                self.stop_dhcp_server(pid).await?;
            }

            // Assign `self` to a variable so it can be used in try_fold and returned back.
            let cfg = self;
            let cfg = stream::iter(options)
                .map(Ok)
                .try_fold(cfg, |cfg, mut option| async move {
                    cfg.set_dhcp_server_option(pid, &mut option).await.map(|_| cfg)
                })
                .await?;
            let cfg = stream::iter(params)
                .map(Ok)
                .try_fold(cfg, |cfg, mut param| async move {
                    cfg.set_dhcp_server_parameter(pid, &mut param).await.map(|_| cfg)
                })
                .await?;

            cfg.start_dhcp_server(pid).await?;
        }

        info!("set_dhcp_server_config pid: {:?} config: {:?}", pid, desired);
        Ok(())
    }

    /// Gets interface info from netstack.
    ///
    /// # Errors
    ///
    /// Returns an error if the FIDL call failed.
    async fn get_interface_info(&self, pid: PortId) -> error::Result<stack::InterfaceInfo> {
        self.stack
            .get_interface_info(pid.to_u64())
            .await
            .map_err(|e| {
                warn!("fidl query failed: {}", e);
                error::NetworkManager::Hal(error::Hal::OperationFailed)
            })?
            .map_err(|e| {
                warn!("get_interface_info({}) failed: {:?}", pid.to_u64(), e);
                error::NetworkManager::Hal(error::Hal::OperationFailed)
            })
    }

    /// Sets an option on the DHCP server on the specified interface.
    ///
    /// # Errors
    ///
    /// Returns an error if
    ///   * a server is enabled on another interface
    ///   * failed to set server option though FIDL
    async fn set_dhcp_server_option(
        &mut self,
        pid: PortId,
        option: &mut fnetdhcp::Option_,
    ) -> error::Result<()> {
        self.get_dhcp_server(pid)?
            .set_option(option)
            .await
            .map_err(|e| {
                warn!("fidl query failed: {}", e);
                error::NetworkManager::Hal(error::Hal::OperationFailed)
            })?
            .map_err(|e| {
                warn!(
                    "failed to set DHCP server option: {}",
                    fuchsia_zircon::Status::from_raw(e).to_string()
                );
                error::NetworkManager::Hal(error::Hal::OperationFailed)
            })
    }

    /// Sets a parameter on the DHCP server on the specified interface.
    ///
    /// # Errors
    ///
    /// Returns an error if
    ///   * a server is enabled on another interface
    ///   * failed to set server parameter though FIDL
    async fn set_dhcp_server_parameter(
        &mut self,
        pid: PortId,
        param: &mut fnetdhcp::Parameter,
    ) -> error::Result<()> {
        self.get_dhcp_server(pid)?
            .set_parameter(param)
            .await
            .map_err(|e| {
                warn!("fidl query failed: {}", e);
                error::NetworkManager::Hal(error::Hal::OperationFailed)
            })?
            .map_err(|e| {
                warn!(
                    "failed to set DHCP server parameter: {}",
                    fuchsia_zircon::Status::from_raw(e).to_string()
                );
                error::NetworkManager::Hal(error::Hal::OperationFailed)
            })
    }

    /// Sets the state of IP forwarding.
    ///
    /// `enabled` controls whether IP forwarding is enabled or disabled.
    pub async fn set_ip_forwarding(&self, enabled: bool) -> error::Result<()> {
        let r = if enabled {
            self.stack.enable_ip_forwarding()
        } else {
            self.stack.disable_ip_forwarding()
        }
        .await;
        r.map_err(|_| error::NetworkManager::Hal(error::Hal::OperationFailed))
    }

    /// Updates a configured IP address.
    async fn apply_manual_ip<'a>(
        &'a mut self,
        pid: PortId,
        current: &'a Option<LifIpAddr>,
        desired: &'a Option<LifIpAddr>,
    ) -> error::Result<()> {
        match (current, desired) {
            (Some(current_ip), Some(desired_ip)) => {
                if current_ip != desired_ip {
                    // There has been a change.
                    // Remove the old one and add the new one.
                    self.unset_ip_address(pid, &current_ip).await?;
                    self.set_ip_address(pid, &desired_ip).await?;
                }
            }
            (None, Some(desired_ip)) => {
                self.set_ip_address(pid, &desired_ip).await?;
            }
            (Some(current_ip), None) => {
                self.unset_ip_address(pid, &current_ip).await?;
            }
            // Nothing to do.
            (None, None) => {}
        };
        Ok(())
    }

    /// Applies the given LIF properties.
    pub async fn apply_properties<'a>(
        &'a mut self,
        pid: PortId,
        old: &'a lifmgr::LIFProperties,
        properties: &'a lifmgr::LIFProperties,
    ) -> error::Result<()> {
        match (&old.dhcp, &properties.dhcp) {
            // DHCP is still disabled, check for manual IP address changes.
            (lifmgr::Dhcp::None, lifmgr::Dhcp::None) => {
                self.apply_manual_ip(pid, &old.address_v4, &properties.address_v4).await?;
            }
            // No changes to DHCP configuration, it is still enabled, nothing to do.
            (lifmgr::Dhcp::Client, lifmgr::Dhcp::Client) => {}
            (lifmgr::Dhcp::Server, lifmgr::Dhcp::Server) => {
                self.apply_manual_ip(pid, &old.address_v4, &properties.address_v4).await?;
                self.set_dhcp_server_config(pid, &old.dhcp_config, &properties.dhcp_config).await?;
            }

            (lifmgr::Dhcp::Server, lifmgr::Dhcp::None) => {
                self.apply_manual_ip(pid, &old.address_v4, &properties.address_v4).await?;
                self.set_dhcp_server_state(pid, false).await?;
            }
            (lifmgr::Dhcp::None, lifmgr::Dhcp::Server) => {
                self.apply_manual_ip(pid, &old.address_v4, &properties.address_v4).await?;
                self.set_dhcp_server_config(pid, &old.dhcp_config, &properties.dhcp_config).await?;
                self.set_dhcp_server_state(pid, true).await?;
            }

            // DHCP configuration transitions from client enabled
            (lifmgr::Dhcp::Client, lifmgr::Dhcp::None) => {
                // Disable DHCP and apply manual address configuration.
                self.set_dhcp_client_state(pid, false).await?;
                self.apply_manual_ip(pid, &old.address_v4, &properties.address_v4).await?;
            }
            (lifmgr::Dhcp::Client, lifmgr::Dhcp::Server) => {
                // Disable DHCP and apply manual address configuration.
                self.set_dhcp_client_state(pid, false).await?;
                self.apply_manual_ip(pid, &old.address_v4, &properties.address_v4).await?;
                self.set_dhcp_server_config(pid, &old.dhcp_config, &properties.dhcp_config).await?;
                self.set_dhcp_server_state(pid, true).await?;
            }
            // DHCP configuration transitions from disabled to enabled.
            (_, lifmgr::Dhcp::Client) => {
                // Remove any manual IP address and enable DHCP client.
                self.apply_manual_ip(pid, &old.address_v4, &None).await?;
                self.set_dhcp_client_state(pid, properties.dhcp == lifmgr::Dhcp::Client).await?;
                self.set_dhcp_server_state(pid, false).await?;
            }
        }

        // TODO(dpradilla) apply ipv6 address

        if old.enabled != properties.enabled {
            info!("id {:?} updating state {:?}", pid, properties.enabled);
            self.set_interface_state(pid, properties.enabled).await?;
        }
        Ok(())
    }

    /// Returns the running routing table (as seen by the network stack).
    pub async fn routes(&mut self) -> Option<Vec<Route>> {
        let table = self.netstack.get_route_table2().await;
        match table {
            Ok(entries) => Some(
                entries
                    .iter()
                    .map(Route::from)
                    .filter(|r| !r.target.address.is_loopback())
                    .collect(),
            ),
            _ => {
                info!("no entries present in forwarding table.");
                None
            }
        }
    }

    /// Sets the DNS resolvers.
    pub async fn set_dns_resolvers<'a, I>(&mut self, mut servers: I) -> error::Result<()>
    where
        I: ExactSizeIterator<Item = &'a mut fnet::SocketAddress>,
    {
        self.lookup_admin
            .set_dns_servers(&mut servers)
            .await
            .with_context(|| "failed setting interface state".to_string())
            .and_then(|r| {
                r.map_err(|status| {
                    anyhow::anyhow!("set_dns_servers returned {:?}", zx::Status::from_raw(status))
                })
            })
            .map_err(|e| {
                error!("set_dns_resolvers error {:?}", e);
                error::NetworkManager::Hal(error::Hal::OperationFailed)
            })
    }

    /// Adds an ethernet device to netstack.
    pub async fn add_ethernet_device(
        &self,
        channel: fuchsia_zircon::Channel,
        port: oir::PortDevice,
    ) -> error::Result<PortId> {
        info!("Adding port: {:#?}", port);
        let nic_id = self
            .netstack
            .add_ethernet_device(
                &port.topological_path,
                &mut netstack::InterfaceConfig {
                    name: port.name,
                    metric: port.metric,
                    filepath: port.file_path,
                },
                fidl::endpoints::ClientEnd::<fidl_fuchsia_hardware_ethernet::DeviceMarker>::new(
                    channel,
                ),
            )
            .await
            .map_err(|e| {
                error!("add_ethernet_device FIDL error {:?}", e);
                error::NetworkManager::Hal(error::Hal::OperationFailed)
            })?
            .map_err(|e| {
                error!("add_ethernet_device error {:?}", zx::Status::from_raw(e));
                error::NetworkManager::Hal(error::Hal::OperationFailed)
            })?;
        info!("added port with id {:?}", nic_id);

        Ok(StackPortId::from(u64::from(nic_id)).into())
    }

    /// Disables packet filters on `port`.
    pub async fn disable_filters(&self, port: PortId) -> error::Result<()> {
        info!("Disabling filters for port {:?}", port);
        self.stack
            .disable_packet_filter(port.to_u64())
            .await
            .context("disable_packet_filter FIDL error")
            .and_then(|r| {
                r.map_err(|error: fidl_fuchsia_net_stack::Error| {
                    anyhow::anyhow!("disable_packet_filter returned {:?}", error)
                })
            })
            .map_err(|e| {
                error!("disable_packet_filter error {:?}", e);
                error::NetworkManager::Hal(error::Hal::OperationFailed)
            })
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{address::LifIpAddr, lifmgr::DnsSearch, ElementId},
        futures::join,
        std::net::Ipv4Addr,
    };

    fn interface_info_with_addrs(addrs: Vec<fnet::Subnet>) -> InterfaceInfo {
        InterfaceInfo {
            id: 42,
            properties: stack::InterfaceProperties {
                topopath: "test/interface/info".to_string(),
                addresses: addrs,
                administrative_status: stack::AdministrativeStatus::Enabled,
                name: "ethtest".to_string(),
                filepath: "/some/file".to_string(),
                mac: None,
                mtu: 0,
                features: fidl_fuchsia_hardware_ethernet::Features::empty(),
                physical_status: stack::PhysicalStatus::Up,
            },
        }
    }

    fn interface_with_addr(addr: Option<LifIpAddr>) -> Interface {
        Interface {
            id: 42.into(),
            topo_path: "test/interface/info".to_string(),
            name: "ethtest".to_string(),
            ipv4_addr: addr.map(|a| InterfaceAddress::Unknown(a)),
            ipv6_addr: Vec::new(),
            enabled: true,
            state: InterfaceState::Up,
            dhcp_client_enabled: false,
        }
    }

    fn sample_addresses() -> Vec<fnet::Subnet> {
        vec![
            // Unspecified addresses are skipped.
            fnet::Subnet {
                addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [0, 0, 0, 0] }),
                prefix_len: 24,
            },
            // Multicast addresses are skipped.
            fnet::Subnet {
                addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [224, 0, 0, 5] }),
                prefix_len: 24,
            },
            // Loopback addresses are skipped.
            fnet::Subnet {
                addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [127, 0, 0, 1] }),
                prefix_len: 24,
            },
            // IPv6 addresses are skipped.
            fnet::Subnet {
                addr: fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                    addr: [16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1],
                }),
                prefix_len: 8,
            },
            // First valid address, should be picked.
            fnet::Subnet {
                addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [4, 3, 2, 1] }),
                prefix_len: 24,
            },
            // A valid address is already available, so this address should be skipped.
            fnet::Subnet {
                addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 4] }),
                prefix_len: 24,
            },
        ]
    }

    fn create_test_netcfg() -> NetCfg {
        let stack: StackProxy = fidl::endpoints::spawn_stream_handler(handle_with_panic).unwrap();
        let netstack: NetstackProxy =
            fidl::endpoints::spawn_stream_handler(handle_with_panic).unwrap();
        let lookup_admin: LookupAdminProxy =
            fidl::endpoints::spawn_stream_handler(handle_with_panic).unwrap();
        NetCfg { stack, netstack, lookup_admin, dhcp_server: None }
    }

    #[test]
    fn test_net_interface_info_into_hal_interface() {
        let info = interface_info_with_addrs(sample_addresses());
        let iface: Interface = (&info).into();
        assert_eq!(iface.topo_path, "test/interface/info");
        assert_eq!(iface.enabled, true);
        assert_eq!(
            iface.get_address_v4(),
            Some(LifIpAddr { address: IpAddr::from([4, 3, 2, 1]), prefix: 24 })
        );
        assert_eq!(iface.id.to_u64(), 42);
    }

    async fn handle_list_interfaces(request: stack::StackRequest) {
        match request {
            stack::StackRequest::ListInterfaces { responder } => {
                responder
                    .send(
                        &mut sample_addresses()
                            .into_iter()
                            .map(|addr| interface_info_with_addrs(vec![addr]))
                            .collect::<Vec<InterfaceInfo>>()
                            .iter_mut(),
                    )
                    .unwrap();
            }
            _ => {
                panic!("unexpected stack request: {:?}", request);
            }
        }
    }

    async fn handle_with_panic<Request: std::fmt::Debug>(request: Request) {
        panic!("unexpected request: {:?}", request);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_ignore_interface_without_ip() {
        let mut netcfg = create_test_netcfg();
        netcfg.stack = fidl::endpoints::spawn_stream_handler(handle_list_interfaces).unwrap();

        assert_eq!(
            netcfg.interfaces().await.unwrap(),
            // Should return only interfaces with a valid address.
            vec![
                interface_with_addr(Some(LifIpAddr {
                    address: IpAddr::from([4, 3, 2, 1]),
                    prefix: 24
                })),
                interface_with_addr(Some(LifIpAddr {
                    address: IpAddr::from([1, 2, 3, 4]),
                    prefix: 24
                })),
            ]
        );
    }

    #[test]
    fn test_valid_address() {
        let f = |addr| {
            valid_unicast_address_or_none(LifIpAddr { address: IpAddr::from(addr), prefix: 24 })
        };
        assert!(f([0, 0, 0, 0]).is_none());
        assert!(f([127, 0, 0, 1]).is_none());
        assert!(f([224, 0, 0, 5]).is_none());
        assert_eq!(
            f([1, 2, 3, 4]),
            Some(LifIpAddr { address: IpAddr::from([1, 2, 3, 4]), prefix: 24 })
        );
    }

    #[test]
    fn test_hal_interface_from_netstack_net_interface() {
        for (test, net_interface, want) in [
            (
                "ipv4 /24",
                netstack::NetInterface {
                    id: 5,
                    flags: netstack::NET_INTERFACE_FLAG_UP | netstack::NET_INTERFACE_FLAG_DHCP,
                    features: fidl_fuchsia_hardware_ethernet::Features::empty(),
                    configuration: 0,
                    name: "test_if".to_string(),
                    addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 4] }),
                    netmask: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [255, 255, 255, 0] }),
                    broadaddr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 255] }),
                    ipv6addrs: vec![],
                    hwaddr: vec![1, 2, 3, 4, 5, 6],
                },
                Interface {
                    id: PortId(5),
                    topo_path: "test_if".to_string(),
                    name: "test_if".to_string(),
                    ipv4_addr: Some(InterfaceAddress::Dhcp(LifIpAddr {
                        address: IpAddr::from([1, 2, 3, 4]),
                        prefix: 24,
                    })),
                    ipv6_addr: Vec::new(),
                    enabled: true,
                    state: InterfaceState::Unknown,
                    dhcp_client_enabled: true,
                },
            ),
            (
                "ipv4 /25",
                netstack::NetInterface {
                    id: 5,
                    flags: netstack::NET_INTERFACE_FLAG_UP | netstack::NET_INTERFACE_FLAG_DHCP,
                    features: fidl_fuchsia_hardware_ethernet::Features::empty(),
                    configuration: 0,
                    name: "test_if".to_string(),
                    addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 4] }),
                    netmask: fnet::IpAddress::Ipv4(fnet::Ipv4Address {
                        addr: [255, 255, 255, 128],
                    }),
                    broadaddr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 127] }),
                    ipv6addrs: vec![],
                    hwaddr: vec![1, 2, 3, 4, 5, 6],
                },
                Interface {
                    id: PortId(5),
                    topo_path: "test_if".to_string(),
                    name: "test_if".to_string(),
                    ipv4_addr: Some(InterfaceAddress::Dhcp(LifIpAddr {
                        address: IpAddr::from([1, 2, 3, 4]),
                        prefix: 25,
                    })),
                    ipv6_addr: Vec::new(),
                    enabled: true,
                    state: InterfaceState::Unknown,
                    dhcp_client_enabled: true,
                },
            ),
            (
                "ipv6 /64",
                netstack::NetInterface {
                    id: 5,
                    flags: netstack::NET_INTERFACE_FLAG_UP | netstack::NET_INTERFACE_FLAG_DHCP,
                    features: fidl_fuchsia_hardware_ethernet::Features::empty(),
                    configuration: 0,
                    name: "test_if".to_string(),
                    addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 4] }),
                    netmask: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [255, 255, 255, 0] }),
                    broadaddr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 255] }),
                    ipv6addrs: vec![fnet::Subnet {
                        addr: fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                            addr: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
                        }),
                        prefix_len: 64,
                    }],
                    hwaddr: vec![1, 2, 3, 4, 5, 6],
                },
                Interface {
                    id: PortId(5),
                    topo_path: "test_if".to_string(),
                    name: "test_if".to_string(),
                    ipv4_addr: Some(InterfaceAddress::Dhcp(LifIpAddr {
                        address: IpAddr::from([1, 2, 3, 4]),
                        prefix: 24,
                    })),
                    ipv6_addr: vec![LifIpAddr {
                        address: IpAddr::from([
                            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                        ]),
                        prefix: 64,
                    }],
                    enabled: true,
                    state: InterfaceState::Unknown,
                    dhcp_client_enabled: true,
                },
            ),
            (
                "ipv6 /72",
                netstack::NetInterface {
                    id: 5,
                    flags: netstack::NET_INTERFACE_FLAG_UP | netstack::NET_INTERFACE_FLAG_DHCP,
                    features: fidl_fuchsia_hardware_ethernet::Features::empty(),
                    configuration: 0,
                    name: "test_if".to_string(),
                    addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 4] }),
                    netmask: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [255, 255, 255, 0] }),
                    broadaddr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 255] }),
                    ipv6addrs: vec![fnet::Subnet {
                        addr: fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                            addr: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
                        }),
                        prefix_len: 72,
                    }],
                    hwaddr: vec![1, 2, 3, 4, 5, 6],
                },
                Interface {
                    id: PortId(5),
                    topo_path: "test_if".to_string(),
                    name: "test_if".to_string(),
                    ipv4_addr: Some(InterfaceAddress::Dhcp(LifIpAddr {
                        address: IpAddr::from([1, 2, 3, 4]),
                        prefix: 24,
                    })),
                    ipv6_addr: vec![LifIpAddr {
                        address: IpAddr::from([
                            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                        ]),
                        prefix: 72,
                    }],
                    enabled: true,
                    state: InterfaceState::Unknown,
                    dhcp_client_enabled: true,
                },
            ),
            (
                "2 ipv6 /64",
                netstack::NetInterface {
                    id: 5,
                    flags: netstack::NET_INTERFACE_FLAG_UP | netstack::NET_INTERFACE_FLAG_DHCP,
                    features: fidl_fuchsia_hardware_ethernet::Features::empty(),
                    configuration: 0,
                    name: "test_if".to_string(),
                    addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 4] }),
                    netmask: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [255, 255, 255, 0] }),
                    broadaddr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 255] }),
                    ipv6addrs: vec![
                        fnet::Subnet {
                            addr: fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                                addr: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
                            }),
                            prefix_len: 64,
                        },
                        fnet::Subnet {
                            addr: fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                                addr: [2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
                            }),
                            prefix_len: 48,
                        },
                    ],
                    hwaddr: vec![1, 2, 3, 4, 5, 6],
                },
                Interface {
                    id: PortId(5),
                    topo_path: "test_if".to_string(),
                    name: "test_if".to_string(),
                    ipv4_addr: Some(InterfaceAddress::Dhcp(LifIpAddr {
                        address: IpAddr::from([1, 2, 3, 4]),
                        prefix: 24,
                    })),
                    ipv6_addr: vec![
                        LifIpAddr {
                            address: IpAddr::from([
                                1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                            ]),
                            prefix: 64,
                        },
                        LifIpAddr {
                            address: IpAddr::from([
                                2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                            ]),
                            prefix: 48,
                        },
                    ],
                    enabled: true,
                    state: InterfaceState::Unknown,
                    dhcp_client_enabled: true,
                },
            ),
        ]
        .iter()
        {
            let got = Interface::from(net_interface);
            assert_eq!(got, *want, "{} Got {:?}, want {:?}", test, got, want)
        }
    }

    #[test]
    fn test_hal_interface_from_interfaceinfo() {
        for (test, net_interface, want) in [(
            "multiple v4 and v6 addresses",
            InterfaceInfo {
                id: 5,
                properties: stack::InterfaceProperties {
                    topopath: "test/interface/info".to_string(),
                    addresses: vec![
                        fnet::Subnet {
                            addr: fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                                addr: [16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1],
                            }),
                            prefix_len: 8,
                        },
                        fnet::Subnet {
                            addr: fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                                addr: [1, 1, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1],
                            }),
                            prefix_len: 64,
                        },
                        fnet::Subnet {
                            addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [4, 3, 2, 1] }),
                            prefix_len: 23,
                        },
                        // A valid address is already available, so this address should be skipped.
                        fnet::Subnet {
                            addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 4] }),
                            prefix_len: 27,
                        },
                    ],
                    administrative_status: stack::AdministrativeStatus::Enabled,
                    name: "test_if".to_string(),
                    filepath: "/some/file".to_string(),
                    mac: None,
                    mtu: 1234,
                    features: fidl_fuchsia_hardware_ethernet::Features::empty(),
                    physical_status: stack::PhysicalStatus::Up,
                },
            },
            Interface {
                id: PortId(5),
                topo_path: "test/interface/info".to_string(),
                name: "test_if".to_string(),
                ipv4_addr: Some(InterfaceAddress::Unknown(LifIpAddr {
                    address: IpAddr::from([4, 3, 2, 1]),
                    prefix: 23,
                })),
                ipv6_addr: vec![
                    LifIpAddr {
                        address: IpAddr::from([
                            16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,
                        ]),
                        prefix: 8,
                    },
                    LifIpAddr {
                        address: IpAddr::from([
                            1, 1, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,
                        ]),
                        prefix: 64,
                    },
                ],
                enabled: true,
                state: InterfaceState::Up,
                dhcp_client_enabled: false,
            },
        )]
        .iter()
        {
            let got = Interface::from(net_interface);
            assert_eq!(got, *want, "{} Got {:?}, want {:?}", test, got, want)
        }
    }
    #[test]
    fn test_route_from_forwarding_entry() {
        assert_eq!(
            Route::from(&ForwardingEntry {
                subnet: fnet::Subnet {
                    addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 0] }),
                    prefix_len: 23,
                },
                destination: stack::ForwardingDestination::NextHop(fnet::IpAddress::Ipv4(
                    fnet::Ipv4Address { addr: [1, 2, 3, 4] },
                )),
            }),
            Route {
                target: LifIpAddr { address: "1.2.3.0".parse().unwrap(), prefix: 23 },
                gateway: Some("1.2.3.4".parse().unwrap()),
                metric: None,
                port_id: None,
            },
            "valid IPv4 entry, with gateway"
        );

        assert_eq!(
            Route::from(&ForwardingEntry {
                subnet: fnet::Subnet {
                    addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 0] }),
                    prefix_len: 23,
                },
                destination: stack::ForwardingDestination::DeviceId(3)
            }),
            Route {
                target: LifIpAddr { address: "1.2.3.0".parse().unwrap(), prefix: 23 },
                gateway: None,
                metric: None,
                port_id: Some(PortId(3))
            },
            "valid IPv4 entry, no gateway"
        );

        assert_eq!(
            Route::from(&ForwardingEntry {
                subnet: fnet::Subnet {
                    addr: fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                        addr: [0x26, 0x20, 0, 0, 0x10, 0, 0x50, 0, 0, 0, 0, 0, 0, 0, 0, 0]
                    }),
                    prefix_len: 64,
                },
                destination: stack::ForwardingDestination::NextHop(fnet::IpAddress::Ipv6(
                    fnet::Ipv6Address {
                        addr: [
                            0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0x02, 0x0, 0x5e, 0xff, 0xfe, 0x0, 0x02,
                            0x65
                        ],
                    }
                )),
            }),
            Route {
                target: LifIpAddr { address: "2620:0:1000:5000::".parse().unwrap(), prefix: 64 },
                gateway: Some("fe80::200:5eff:fe00:265".parse().unwrap()),
                metric: None,
                port_id: None,
            },
            "valid IPv6 entry, with gateway"
        );

        assert_eq!(
            Route::from(&ForwardingEntry {
                subnet: fnet::Subnet {
                    addr: fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                        addr: [0x26, 0x20, 0, 0, 0x10, 0, 0x50, 0, 0, 0, 0, 0, 0, 0, 0, 0]
                    }),
                    prefix_len: 58,
                },
                destination: stack::ForwardingDestination::DeviceId(3)
            }),
            Route {
                target: LifIpAddr { address: "2620:0:1000:5000::".parse().unwrap(), prefix: 58 },
                gateway: None,
                metric: None,
                port_id: Some(PortId(3))
            },
            "valid IPv6 entry, no gateway"
        );
    }

    #[test]
    fn test_route_from_routetableentry2() {
        assert_eq!(
            Route::from(&netstack::RouteTableEntry2 {
                destination: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 0] }),
                netmask: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [255, 255, 254, 0] }),
                gateway: Some(Box::new(fnet::IpAddress::Ipv4(fnet::Ipv4Address {
                    addr: [1, 2, 3, 1]
                }))),
                nicid: 1,
                metric: 100,
            }),
            Route {
                target: LifIpAddr { address: "1.2.3.0".parse().unwrap(), prefix: 23 },
                gateway: Some("1.2.3.1".parse().unwrap()),
                metric: Some(100),
                port_id: Some(PortId(1)),
            },
            "valid IPv4 entry, with gateway"
        );

        assert_eq!(
            Route::from(&netstack::RouteTableEntry2 {
                destination: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 0] }),
                netmask: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [255, 255, 254, 0] }),
                gateway: None,
                nicid: 3,
                metric: 50,
            }),
            Route {
                target: LifIpAddr { address: "1.2.3.0".parse().unwrap(), prefix: 23 },
                gateway: None,
                metric: Some(50),
                port_id: Some(PortId(3))
            },
            "valid IPv4 entry, no gateway"
        );

        assert_eq!(
            Route::from(&netstack::RouteTableEntry2 {
                destination: fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                    addr: [0x26, 0x20, 0, 0, 0x10, 0, 0x50, 0, 0, 0, 0, 0, 0, 0, 0, 0]
                }),
                netmask: fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                    addr: [255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0],
                }),
                gateway: Some(Box::new(fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                    addr: [
                        0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0x02, 0x0, 0x5e, 0xff, 0xfe, 0x0, 0x02, 0x65
                    ],
                }))),
                nicid: 9,
                metric: 500,
            }),
            Route {
                target: LifIpAddr { address: "2620:0:1000:5000::".parse().unwrap(), prefix: 64 },
                gateway: Some("fe80::200:5eff:fe00:265".parse().unwrap()),
                metric: Some(500),
                port_id: Some(PortId(9)),
            },
            "valid IPv6 entry, with gateway"
        );

        assert_eq!(
            Route::from(&netstack::RouteTableEntry2 {
                destination: fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                    addr: [0x26, 0x20, 0, 0, 0x10, 0, 0x50, 0, 0, 0, 0, 0, 0, 0, 0, 0]
                }),
                netmask: fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                    addr: [255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0],
                }),
                gateway: None,
                nicid: 3,
                metric: 50,
            }),
            Route {
                target: LifIpAddr { address: "2620:0:1000:5000::".parse().unwrap(), prefix: 56 },
                gateway: None,
                metric: Some(50),
                port_id: Some(PortId(3))
            },
            "valid IPv6 entry, no gateway"
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_dhcp_servers() {
        let mut netcfg = create_test_netcfg();

        netcfg.get_dhcp_server(PortId(1)).expect("failed to get DHCP server in test");
        assert!(netcfg.dhcp_server.is_some());

        netcfg.get_dhcp_server(PortId(1)).expect("failed to get DHCP server in test");
        assert_eq!(
            netcfg.get_dhcp_server(PortId(2)).expect_err("get DHCP server with wrong id succeeded"),
            error::NetworkManager::Hal(error::Hal::OperationFailed)
        );

        netcfg.dhcp_server = None; // Manually disable the server so no FIDL calls are made.
        netcfg.get_dhcp_server(PortId(2)).expect("failed to get DHCP server in test");
        assert!(netcfg.dhcp_server.is_some());
        assert_eq!(
            netcfg.get_dhcp_server(PortId(1)).expect_err("get DHCP server with wrong id succeeded"),
            error::NetworkManager::Hal(error::Hal::OperationFailed)
        );
    }

    async fn handle_get_interface(request: stack::StackRequest) {
        match request {
            stack::StackRequest::GetInterfaceInfo { responder, .. } => {
                responder
                    .send(&mut Ok(stack::InterfaceInfo {
                        id: 42,
                        properties: stack::InterfaceProperties {
                            name: "forty-two".to_string(),
                            topopath: "".to_string(),
                            filepath: "".to_string(),
                            mac: None,
                            mtu: 0,
                            features: fidl_fuchsia_hardware_ethernet::Features::empty(),
                            administrative_status: stack::AdministrativeStatus::Enabled,
                            physical_status: stack::PhysicalStatus::Up,
                            addresses: vec![fnet::Subnet {
                                addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address {
                                    addr: [192, 168, 42, 1],
                                }),
                                prefix_len: 24,
                            }],
                        },
                    }))
                    .unwrap();
            }
            _ => {
                panic!("unexpected stack request: {:?}", request);
            }
        }
    }

    #[derive(Debug, PartialEq)]
    enum MockDhcpServerRequest {
        StartServing,
        StopServing,
        SetOption(fnetdhcp::Option_),
        SetParameter(fnetdhcp::Parameter),
    }

    struct MockDhcpServer {
        requests: Vec<MockDhcpServerRequest>,
        should_err: bool,
    }

    impl MockDhcpServer {
        fn new() -> Self {
            Self { requests: Vec::new(), should_err: false }
        }

        fn get_response(&self) -> Result<(), i32> {
            if self.should_err {
                Err(0)
            } else {
                Ok(())
            }
        }

        async fn handle_request_stream(&mut self, stream: fnetdhcp::Server_RequestStream) {
            self.requests = stream
                .map(|request| match request.expect("dhcp server request failed") {
                    fnetdhcp::Server_Request::StartServing { responder } => {
                        responder
                            .send(&mut self.get_response())
                            .expect("failed to send test response");
                        MockDhcpServerRequest::StartServing
                    }
                    fnetdhcp::Server_Request::StopServing { responder } => {
                        responder.send().expect("failed to send test response");
                        MockDhcpServerRequest::StopServing
                    }
                    fnetdhcp::Server_Request::SetOption { value, responder } => {
                        responder
                            .send(&mut self.get_response())
                            .expect("failed to send test response");
                        MockDhcpServerRequest::SetOption(value)
                    }
                    fnetdhcp::Server_Request::SetParameter { value, responder } => {
                        responder
                            .send(&mut self.get_response())
                            .expect("failed to send test response");
                        MockDhcpServerRequest::SetParameter(value)
                    }
                    _ => panic!("unexpected DHCP server request"),
                })
                .collect::<Vec<_>>()
                .await;
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_enable_disable_dhcp_server() {
        let mut netcfg = create_test_netcfg();
        netcfg.stack = fidl::endpoints::spawn_stream_handler(handle_get_interface)
            .expect("failed to spawn test handler");

        // Trying to disable DHCP server when no servers are running is no-op.
        netcfg
            .set_dhcp_server_state(PortId(42), false)
            .await
            .expect("failed to disable DHCP server in test");
        assert!(netcfg.dhcp_server.is_none());

        let (dhcpd_proxy, dhcpd_stream) =
            fidl::endpoints::create_proxy_and_stream::<Server_Marker>()
                .expect("failed to create test proxy and stream");
        netcfg.dhcp_server = Some((PortId(42), dhcpd_proxy));

        let enable_then_disable_server = async {
            netcfg
                .set_dhcp_server_state(PortId(42), true)
                .await
                .expect("failed to enable test DHCP server");
            assert!(netcfg.dhcp_server.is_some());

            netcfg
                .set_dhcp_server_state(PortId(1), false)
                .await
                .expect_err("attempt to disable a server on a different interface should fail");

            netcfg
                .set_dhcp_server_state(PortId(42), false)
                .await
                .expect("failed to disable test DHCP server");
            // Server is dropped after disable.
            assert!(netcfg.dhcp_server.is_none());

            // Drop the channel to stop mock server.
            drop(netcfg);
        };

        let mut mock_server = MockDhcpServer::new();
        join!(enable_then_disable_server, mock_server.handle_request_stream(dhcpd_stream));

        assert_eq!(
            mock_server.requests,
            vec![
                // Enables server.
                MockDhcpServerRequest::StopServing,
                MockDhcpServerRequest::SetParameter(fnetdhcp::Parameter::IpAddrs(vec![
                    fnet::Ipv4Address { addr: [192, 168, 42, 1] }
                ])),
                MockDhcpServerRequest::SetParameter(fnetdhcp::Parameter::BoundDeviceNames(vec![
                    "forty-two".to_string()
                ])),
                MockDhcpServerRequest::StartServing,
                // Disables server.
                MockDhcpServerRequest::StopServing,
            ],
        );
    }

    struct DhcpServerConfigTestCase<'a> {
        current_config: Option<lifmgr::DhcpServerConfig>,
        new_config: Option<lifmgr::DhcpServerConfig>,
        want_num_requests: usize,
        want_enable: bool,
        want_requests: &'a [MockDhcpServerRequest],
    }

    async fn run_dhcp_server_config_test(tc: DhcpServerConfigTestCase<'_>) {
        let mut netcfg = create_test_netcfg();
        netcfg.stack = fidl::endpoints::spawn_stream_handler(handle_get_interface)
            .expect("failed to spawn test handler");

        let (dhcpd_proxy, dhcpd_stream) =
            fidl::endpoints::create_proxy_and_stream::<Server_Marker>()
                .expect("failed to create test proxy and stream");
        netcfg.dhcp_server = Some((PortId(42), dhcpd_proxy));

        let set_server_config = async {
            netcfg
                .set_dhcp_server_config(PortId(42), &tc.current_config, &tc.new_config)
                .await
                .expect("failed to set test DHCP server config");
            if !tc.want_enable {
                assert!(netcfg.dhcp_server.is_none());
            }
            // Drop the channel to stop mock server.
            drop(netcfg);
        };

        let mut mock_server = MockDhcpServer::new();
        join!(set_server_config, mock_server.handle_request_stream(dhcpd_stream));

        assert_eq!(mock_server.requests.len(), tc.want_num_requests);

        if tc.want_num_requests > 0 {
            assert_eq!(mock_server.requests[0], MockDhcpServerRequest::StopServing);

            if tc.want_enable {
                assert_eq!(
                    mock_server.requests[tc.want_num_requests - 1],
                    MockDhcpServerRequest::StartServing
                );
            } else {
                assert_eq!(
                    mock_server.requests[tc.want_num_requests - 1],
                    MockDhcpServerRequest::StopServing
                );
            }

            // Order of the requests doesn't matter, so only check existence.
            tc.want_requests.iter().for_each(|request| {
                assert!(mock_server.requests.contains(request));
            });
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_configure_dhcp_server() {
        run_dhcp_server_config_test(DhcpServerConfigTestCase {
            current_config: None,
            new_config: Some(lifmgr::DhcpServerConfig {
                options: lifmgr::DhcpServerOptions {
                    id: Some(ElementId::new(1)),
                    lease_time_sec: Some(3600),
                    default_gateway: Some(fnet::Ipv4Address { addr: [192, 168, 42, 42] }),
                    dns_server: Some(DnsSearch {
                        servers: vec![LifIpAddr {
                            address: IpAddr::V4(Ipv4Addr::new(192, 168, 42, 242)),
                            prefix: 24,
                        }],
                        domain_name: None,
                    }),
                    enable: Some(true),
                },
                pool: Some(lifmgr::DhcpAddressPool {
                    id: ElementId::new(1),
                    start: Ipv4Addr::new(192, 168, 42, 100),
                    end: Ipv4Addr::new(192, 168, 42, 200),
                }),
                reservations: vec![lifmgr::DhcpReservation {
                    id: ElementId::new(1),
                    name: None,
                    address: Ipv4Addr::new(192, 168, 42, 201),
                    mac: eui48::MacAddress::new([1, 2, 3, 4, 5, 6]),
                }],
            }),
            want_num_requests: 7,
            want_enable: true,
            want_requests: &[
                MockDhcpServerRequest::SetOption(fnetdhcp::Option_::Router(vec![
                    fnet::Ipv4Address { addr: [192, 168, 42, 42] },
                ])),
                MockDhcpServerRequest::SetOption(fnetdhcp::Option_::Router(vec![
                    fnet::Ipv4Address { addr: [192, 168, 42, 42] },
                ])),
                MockDhcpServerRequest::SetOption(fnetdhcp::Option_::DomainNameServer(vec![
                    fnet::Ipv4Address { addr: [192, 168, 42, 242] },
                ])),
                MockDhcpServerRequest::SetParameter(fnetdhcp::Parameter::Lease(
                    fnetdhcp::LeaseLength { default: Some(3600), max: None },
                )),
                MockDhcpServerRequest::SetParameter(fnetdhcp::Parameter::AddressPool(
                    fnetdhcp::AddressPool {
                        network_id: Some(fnet::Ipv4Address { addr: [192, 168, 42, 0] }),
                        broadcast: Some(fnet::Ipv4Address { addr: [192, 168, 42, 255] }),
                        mask: Some(fnet::Ipv4Address { addr: [255, 255, 255, 0] }),
                        pool_range_start: Some(fnet::Ipv4Address { addr: [192, 168, 42, 100] }),
                        pool_range_stop: Some(fnet::Ipv4Address { addr: [192, 168, 42, 200] }),
                    },
                )),
                MockDhcpServerRequest::SetParameter(fnetdhcp::Parameter::StaticallyAssignedAddrs(
                    vec![fnetdhcp::StaticAssignment {
                        host: Some(fnet::MacAddress { octets: [1, 2, 3, 4, 5, 6] }),
                        assigned_addr: Some(fnet::Ipv4Address { addr: [192, 168, 42, 201] }),
                    }],
                )),
            ],
        })
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_configure_dhcp_server_should_skip_unspecified_configs() {
        run_dhcp_server_config_test(DhcpServerConfigTestCase {
            current_config: None,
            new_config: Some(lifmgr::DhcpServerConfig {
                options: lifmgr::DhcpServerOptions {
                    lease_time_sec: Some(3600),
                    enable: Some(true),
                    ..Default::default()
                },
                ..Default::default()
            }),
            want_num_requests: 4,
            want_enable: true,
            want_requests: &[
                MockDhcpServerRequest::SetParameter(fnetdhcp::Parameter::Lease(
                    fnetdhcp::LeaseLength { default: Some(3600), max: None },
                )),
                MockDhcpServerRequest::SetParameter(fnetdhcp::Parameter::StaticallyAssignedAddrs(
                    Vec::new(),
                )),
            ],
        })
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_configure_dhcp_server_should_set_new_server_state_if_explicitly_requested() {
        stream::iter(&[
            (Some(true), 3), // 3 requests sent to server: stop, set reserved address, then restart.
            (Some(false), 1), // 1 request sent to server: stop only.
        ])
        .for_each(|(new_status, num_requests)| async move {
            run_dhcp_server_config_test(DhcpServerConfigTestCase {
                current_config: None,
                new_config: Some(lifmgr::DhcpServerConfig {
                    options: lifmgr::DhcpServerOptions {
                        enable: *new_status,
                        ..Default::default()
                    },
                    ..Default::default()
                }),
                want_num_requests: *num_requests,
                want_enable: new_status.expect("unexpected none status in test input"),
                want_requests: &[],
            })
            .await;
        })
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_configure_dhcp_server_should_maintain_previous_state_if_no_new_state_requested() {
        stream::iter(&[
            (Some(true), 3), // 3 requests sent to server: stop, set reserved address, then restart.
            (Some(false), 1), // 1 request sent to server: stop only.
            (None, 1),       // 1 request sent to server: stop only.
        ])
        .for_each(|(old_status, num_requests)| {
            async move {
                run_dhcp_server_config_test(DhcpServerConfigTestCase {
                    current_config: Some(lifmgr::DhcpServerConfig {
                        options: lifmgr::DhcpServerOptions {
                            enable: *old_status,
                            ..Default::default()
                        },
                        ..Default::default()
                    }),
                    new_config: Some(lifmgr::DhcpServerConfig {
                        options: lifmgr::DhcpServerOptions { enable: None, ..Default::default() },
                        ..Default::default()
                    }),
                    want_num_requests: *num_requests,
                    // If a desired state is never specified (previous state is also None),
                    // leave the server disabled.
                    want_enable: old_status.unwrap_or(false),
                    want_requests: &[],
                })
                .await;
            }
        })
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_configure_dhcp_server_with_none_config_is_noop() {
        // No stub server is running on interface with ID 42, the test would fail if
        // `set_dhcp_server_config` tries to do anything.
        create_test_netcfg()
            .set_dhcp_server_config(PortId(42), &None, &None)
            .await
            .expect("failed to set test DHCP server config");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_configure_dhcp_server_address_pool_not_in_subnet() {
        let mut netcfg = create_test_netcfg();
        netcfg.stack = fidl::endpoints::spawn_stream_handler(handle_get_interface)
            .expect("failed to spawn test handler");

        let (dhcpd_proxy, dhcpd_stream) =
            fidl::endpoints::create_proxy_and_stream::<Server_Marker>()
                .expect("failed to create test proxy and stream");
        netcfg.dhcp_server = Some((PortId(42), dhcpd_proxy));

        let set_server_config = async {
            let res = netcfg
                .set_dhcp_server_config(
                    PortId(42),
                    &None,
                    &Some(lifmgr::DhcpServerConfig {
                        options: lifmgr::DhcpServerOptions {
                            enable: Some(true),
                            ..Default::default()
                        },
                        pool: Some(lifmgr::DhcpAddressPool {
                            id: ElementId::new(1),
                            // Doesn't match subnet of the interface.
                            start: Ipv4Addr::new(192, 168, 92, 100),
                            end: Ipv4Addr::new(192, 168, 92, 200),
                        }),
                        ..Default::default()
                    }),
                )
                .await;
            // Drop the channel to stop mock server.
            drop(netcfg);

            assert_eq!(res, Err(error::NetworkManager::Hal(error::Hal::OperationFailed)));
        };

        let mut mock_server = MockDhcpServer::new();
        join!(set_server_config, mock_server.handle_request_stream(dhcpd_stream));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_configure_dhcp_server_should_propagate_fidl_error() {
        let mut netcfg = create_test_netcfg();
        netcfg.stack = fidl::endpoints::spawn_stream_handler(handle_get_interface)
            .expect("failed to spawn test handler");

        let (dhcpd_proxy, dhcpd_stream) =
            fidl::endpoints::create_proxy_and_stream::<Server_Marker>()
                .expect("failed to create test proxy and stream");
        netcfg.dhcp_server = Some((PortId(42), dhcpd_proxy));

        let set_server_config = async {
            let res = netcfg
                .set_dhcp_server_config(
                    PortId(42),
                    &None,
                    &Some(lifmgr::DhcpServerConfig {
                        options: lifmgr::DhcpServerOptions {
                            enable: Some(true),
                            ..Default::default()
                        },
                        ..Default::default()
                    }),
                )
                .await;
            assert_eq!(res, Err(error::NetworkManager::Hal(error::Hal::OperationFailed)));
            // Drop the channel to stop mock server.
            drop(netcfg);
        };

        let mut mock_server = MockDhcpServer::new();
        mock_server.should_err = true;
        join!(set_server_config, mock_server.handle_request_stream(dhcpd_stream));
    }
}
