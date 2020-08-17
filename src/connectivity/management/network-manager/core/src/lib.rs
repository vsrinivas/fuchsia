// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The logic for the Network Manager.
//!
//! This crate implements the logic for the Network Manager, not including
//! the details of execution (serving FIDL, futures execution, etc).

extern crate network_manager_core_interface as interface;

#[macro_use]
extern crate log;

pub mod address;
pub mod config;
pub mod error;
pub mod hal;
pub mod lifmgr;
pub mod oir;
pub mod packet_filter;
pub mod portmgr;
mod servicemgr;

use {
    crate::config::InterfaceType,
    crate::lifmgr::{LIFProperties, LIFType},
    crate::portmgr::PortId,
    fidl_fuchsia_net as fnet, fidl_fuchsia_net_name as fname, fidl_fuchsia_net_stack as stack,
    fidl_fuchsia_netstack as netstack, fidl_fuchsia_router_config as netconfig,
    std::sync::atomic::{AtomicUsize, Ordering},
};

// TODO(cgibson): Remove this when we have the ability to add one port at a time the bridge.
const NUMBER_OF_PORTS_IN_LAN: usize = 3;

/// `DeviceState` holds the device state.
pub struct DeviceState {
    version: Version, // state version.
    port_manager: portmgr::PortManager,
    lif_manager: lifmgr::LIFManager,
    service_manager: servicemgr::Manager,
    packet_filter: packet_filter::PacketFilter,
    dns_config: DnsConfig,
    hal: hal::NetCfg,
    config: config::Config,
    lans: Vec<PortId>,
}

#[derive(Debug, Clone, Copy)]
pub enum DnsPolicy {
    /// Can not be replaced by dynamically learned DNS configuration,
    /// will overwrite existing configuration.
    Static,
    /// Can be replaced by dynamically learned DNS configuration,
    /// will not overwrite existing configuration.
    Replaceable,
    /// Will merge with existing configuration.
    Merge,
}

impl From<DnsPolicy> for netconfig::DnsPolicy {
    fn from(p: DnsPolicy) -> Self {
        match p {
            DnsPolicy::Static => netconfig::DnsPolicy::Static,
            DnsPolicy::Replaceable => netconfig::DnsPolicy::Replaceable,
            DnsPolicy::Merge => netconfig::DnsPolicy::Merge,
        }
    }
}

impl Default for DnsPolicy {
    fn default() -> Self {
        DnsPolicy::Static
    }
}

impl From<netconfig::DnsPolicy> for DnsPolicy {
    fn from(p: netconfig::DnsPolicy) -> Self {
        match p {
            netconfig::DnsPolicy::Static => DnsPolicy::Static,
            netconfig::DnsPolicy::Replaceable => DnsPolicy::Replaceable,
            netconfig::DnsPolicy::Merge => DnsPolicy::Merge,
            _ => DnsPolicy::default(),
        }
    }
}

#[derive(Debug)]
struct DnsConfig {
    id: ElementId,
    servers: Vec<fnet::SocketAddress>,
    domain: Option<String>,
    policy: DnsPolicy,
}

impl From<&DnsConfig> for netconfig::DnsResolverConfig {
    fn from(c: &DnsConfig) -> Self {
        netconfig::DnsResolverConfig {
            element: c.id.into(),
            policy: c.policy.into(),
            search: netconfig::DnsSearch {
                domain_name: c.domain.clone(),
                servers: c
                    .servers
                    .iter()
                    .map(|s| match s {
                        fidl_fuchsia_net::SocketAddress::Ipv4(addr) => {
                            fidl_fuchsia_net::IpAddress::Ipv4(addr.address)
                        }
                        fidl_fuchsia_net::SocketAddress::Ipv6(addr) => {
                            fidl_fuchsia_net::IpAddress::Ipv6(addr.address)
                        }
                    })
                    .collect::<Vec<_>>(),
            },
        }
    }
}

impl DeviceState {
    /// Create an empty DeviceState.
    pub fn new(hal: hal::NetCfg, packet_filter: packet_filter::PacketFilter) -> Self {
        let v = 0;
        DeviceState {
            version: v,
            port_manager: portmgr::PortManager::new(),
            lif_manager: lifmgr::LIFManager::new(),
            service_manager: servicemgr::Manager::new(),
            packet_filter,
            hal,
            lans: Vec::new(),
            dns_config: DnsConfig {
                id: ElementId::new(v),
                servers: Default::default(),
                domain: Default::default(),
                policy: Default::default(),
            },
            config: config::Config::new(
                "/data/user_config.json",
                "/config/data/factory_config.json",
                "/pkg/data/device_schema.json",
            ),
        }
    }

    /// Loads the device configuration.
    pub async fn load_config(&mut self) -> error::Result<()> {
        if let Err(e) = self.config.load_config().await {
            error!("Failed to validate config from both user and factory files: {}", e);
            Err(e)
        } else {
            info!("Successfully loaded configuration!");
            Ok(())
        }
    }

    /// Returns a client for a `fuchsia.net.name.DnsServerWatcher` from the netstack.
    pub fn get_netstack_dns_server_watcher(
        &mut self,
    ) -> error::Result<fname::DnsServerWatcherProxy> {
        self.hal.get_netstack_dns_server_watcher()
    }

    /// Returns the underlying event streams associated with the open channels to fuchsia.net.stack
    /// and fuchsia.netstack.
    pub fn take_event_streams(
        &mut self,
    ) -> (stack::StackEventStream, netstack::NetstackEventStream) {
        self.hal.take_event_streams()
    }

    /// Informs network manager of an external event from fuchsia.net.stack containing updates to
    /// properties associated with an interface. OnInterfaceStatusChange event is raised when an
    /// interface is enabled/disabled, connected/disconnected, or added/removed.
    pub async fn update_state_for_stack_event(
        &mut self,
        event: stack::StackEvent,
    ) -> error::Result<()> {
        match event {
            stack::StackEvent::OnInterfaceStatusChange { info } => {
                match self.hal.get_interface(info.id).await {
                    Some(iface) => self.notify_lif_added_or_updated(iface),
                    None => Ok(()),
                }
            }
        }
    }

    /// Informs network manager of an external event from fuchsia.netstack containing updates to
    /// properties associated with an interface.
    pub async fn update_state_for_netstack_event(
        &mut self,
        event: netstack::NetstackEvent,
    ) -> error::Result<()> {
        match event {
            netstack::NetstackEvent::OnInterfacesChanged { interfaces } => {
                interfaces.into_iter().try_for_each(|iface| {
                    let i = &iface;
                    self.notify_lif_added_or_updated(i.into())
                })
            }
        }
    }

    /// Restores the `topo_path`'s configured packet filters.
    ///
    /// This method uses [`portmgr::PortId`]'s to install the packet filter rule on the correct
    /// interface. This means that the port must be registered with port manager before this method
    /// is called otherwise the `nicid` lookup will fail.
    pub async fn apply_packet_filters(&self, topo_path: &str) -> error::Result<()> {
        let acls = self.config.get_acl_entries(topo_path).and_then(|it| {
            let mut peek = it.peekable();
            if peek.peek().is_some() {
                Some(peek)
            } else {
                None
            }
        });
        let pid = self
            .port_manager
            .port_id(topo_path)
            .ok_or_else(|| error::NetworkManager::Port(error::Port::NotFound))?;

        if let Some(acls) = acls {
            for entry in acls {
                for rule in self.packet_filter.parse_aclentry(entry).await?.iter() {
                    info!("Applying filter rule {:?} to port {:?}", rule, pid);
                    self.set_filter_on_interface(rule, pid.to_u32()).await?;
                }
            }
            Ok(())
        } else {
            // No filter rules, disable filters.
            // TODO(fxbug.dev/52968): Revisit whether we need to disable filters
            // once we drill down on why it's causing such a slow down.
            self.hal.disable_filters(*pid).await
        }
    }

    /// Restores state of global system options and services from stored configuration.
    pub async fn setup_services(&mut self) -> error::Result<()> {
        info!("Restoring system services state...");
        self.packet_filter.clear_filters().await?;
        self.hal.set_ip_forwarding(self.config.get_ip_forwarding_state()).await?;
        if self.config.get_nat_state() {
            self.service_manager.enable_nat();
            match self.packet_filter.update_nat_config(self.service_manager.get_nat_config()).await
            {
                // If the result of this call was `UpdateNatPendingConfig` then NAT needs further
                // configuration before it's ready to be enabled. Everything else is an error.
                // Further configuration is done each time there is a change to the LIF in the
                // `update_lif_properties()` method.
                Ok(())
                | Err(error::NetworkManager::Service(error::Service::UpdateNatPendingConfig)) => {}
                Err(e) => {
                    error!("Failed to install NAT rules: {:?}", e);
                    return Err(e);
                }
            }
        } else {
            self.service_manager.disable_nat();
            self.packet_filter.clear_nat_rules().await?;
        }

        // TODO(cgibson): Configure DHCP server.
        // TODO(cgibson): Apply global packet filtering rules (i.e. rules that apply to all
        // interfaces) when we support a wildcard device_id's.
        Ok(())
    }

    /// Configures a WAN uplink from the stored configuration.
    async fn configure_wan(&mut self, pid: PortId, topological_path: &str) -> error::Result<()> {
        let wan_name = self.config.get_interface_name(topological_path)?;
        let properties = self.config.create_wan_properties(topological_path)?;
        let lif = self.create_lif(LIFType::WAN, wan_name, None, &[pid]).await?;
        self.update_lif_properties(lif.id().uuid(), &properties).await?;
        self.apply_packet_filters(topological_path).await?;
        self.hal.set_interface_state(pid, true).await?;
        info!("WAN configured: pid: {:?}, lif: {:?}, properties: {:?} ", pid, lif, properties);
        Ok(())
    }

    /// Configures a bridge from the provided ports.
    async fn configure_lan_bridge(&mut self, pids: &[PortId]) -> error::Result<()> {
        let switched_vlans = pids.iter().map(|pid| {
            self.port_manager
                .topo_path(*pid)
                .ok_or_else(|| {
                    error::NetworkManager::Config(error::Config::NotFound {
                        msg: format!("topo_path not found in port manager map: {:?}", pid),
                    })
                })
                .and_then(|t| self.config.get_switched_vlan_by_device_id(t))
        });
        let routed_vlan = self.config.all_ports_have_same_bridge(switched_vlans)?;
        let bridge_name = self.config.get_bridge_name(&routed_vlan).to_string();
        let vlan_id = routed_vlan.vlan_id;
        let properties = self.config.create_routed_vlan_properties(&routed_vlan)?;
        let lif = self.create_lif(LIFType::LAN, bridge_name, Some(vlan_id), pids).await?;
        self.update_lif_properties(lif.id().uuid(), &properties).await?;
        // TODO(cgibson): Add support for packet filtering on bridge ports.
        info!("Created new LAN bridge with properties: {:?}", properties);
        Ok(())
    }

    async fn configure_lan(
        &mut self,
        pids: &[PortId],
        topological_path: &str,
    ) -> error::Result<()> {
        let name = self.config.get_interface_name(topological_path)?;
        let properties = self.config.create_lan_properties(topological_path)?;
        let lif = self.create_lif(LIFType::LAN, name, None, pids).await?;
        info!("LAN configured: pids: {:?} lif: {:?} properties: {:?} ", pids, lif, properties);
        self.update_lif_properties(lif.id().uuid(), &properties).await?;
        self.apply_packet_filters(topological_path).await?;
        for pid in pids {
            self.hal.set_interface_state(*pid, true).await?
        }
        Ok(())
    }

    /// Configures this device according the defaults provided in `default_interface`.
    async fn configure_default_interface(
        &mut self,
        pid: PortId,
        topological_path: &str,
    ) -> error::Result<()> {
        match self.config.default_interface() {
            Some(default_interface) => match default_interface.config.interface_type {
                config::InterfaceType::IfUplink => self.configure_wan(pid, topological_path).await,
                config::InterfaceType::IfEthernet => {
                    self.configure_lan(&[pid], topological_path).await
                }
                InterfaceType::IfAggregate
                | InterfaceType::IfLoopback
                | InterfaceType::IfRoutedVlan
                | InterfaceType::IfSonet
                | InterfaceType::IfTunnelGre4
                | InterfaceType::IfTunnelGre6 => {
                    Err(error::NetworkManager::Config(error::Config::NotSupported {
                        msg: format!(
                            "Unsupported default interface type: {:?}",
                            default_interface.config.interface_type
                        ),
                    }))
                }
            },
            None => Err(error::NetworkManager::Config(error::Config::NotFound {
                msg: format!(
                    "Trying to configure {} as a default interface but none was provided",
                    topological_path
                ),
            })),
        }
    }

    /// Applies configuration to the new port.
    async fn apply_configuration_on_new_port(
        &mut self,
        pid: PortId,
        topological_path: &str,
    ) -> error::Result<()> {
        // TODO(51208): Figure out a better way to do this.
        if self.config.device_id_is_an_uplink(topological_path) {
            info!("Discovered a new uplink: {}", topological_path);
            return self.configure_wan(pid, topological_path).await;
        }
        if self.config.device_id_is_a_downlink(topological_path) {
            info!("Discovered a new downlink: {}", topological_path);
            return self.configure_lan(&[pid], topological_path).await;
        }
        if self.config.is_unknown_device_id(topological_path) {
            info!("Discovered a new unknown device: {}", topological_path);
            return self.configure_default_interface(pid, topological_path).await;
        }

        // TODO(43251): Refactor this when we have the ability to add one port at a time to the
        // bridge, currently we have to wait until we've discovered all the ports before creating
        // the bridge.
        self.lans.push(pid);

        // If the configuration contains switched_vlan interfaces that matches this
        // topological_path add it to the bridge.
        if self.config.device_id_is_a_switched_vlan(topological_path) {
            // TODO(43251): Need to do this until we can add individual bridge members.
            if self.lans.len() == NUMBER_OF_PORTS_IN_LAN {
                let lans = self.lans.clone();
                info!("Creating new bridge with LAN ports: {:?}", lans);
                return self.configure_lan_bridge(&lans).await;
            }
        }
        Ok(())
    }

    /// Returns a [`hal::Port`] from the topological path.
    pub async fn port_with_topopath(&self, path: &str) -> error::Result<Option<hal::Port>> {
        let port = self.hal.ports().await?.into_iter().find(|x| x.path == path);
        info!("ports {:?}", port);
        Ok(port)
    }

    /// Informs network manager of an external event from fuchsia.netstack containing updates to
    /// properties associated with an interface.
    pub async fn oir_event(&mut self, event: oir::OIRInfo) -> error::Result<()> {
        match event.action {
            oir::Action::ADD => {
                // Add to netstack if not there already.
                let pid = if let Some(p) = self.port_with_topopath(&event.topological_path).await? {
                    info!("port already added {}: {:?}", &event.topological_path, p);
                    p.id
                } else {
                    let info = event.device_information.ok_or(error::Oir::MissingInformation)?;
                    let port =
                        oir::PortDevice::new(&event.file_path, &event.topological_path, info)?;
                    info!("adding port {}: {:?}", &event.topological_path, port);
                    self.hal.add_ethernet_device(event.device_channel.unwrap(), port).await?
                };

                // Verify port manager is consistent.
                if let Some(id) = self.port_manager.port_id(&event.topological_path) {
                    if pid != *id {
                        error!("port {:?} already exists with id {:?}", event.topological_path, id);
                        return Err(error::NetworkManager::Oir(error::Oir::InconsistentState));
                    }
                    return Ok(());
                }

                // Port manager does not know about this port, add it.

                // No version change, this is just a port insertion,
                // not a configuration update.
                self.add_port(pid, &event.topological_path, self.version());

                // Apply initial configuration.
                self.apply_configuration_on_new_port(pid, &event.topological_path).await?
            }
            oir::Action::REMOVE => {
                // Tell portmanager interface is removed. It should keep any config there.
                // Tell LIF manager associated port is gone, keep config there so it can be
                // reapplied if it comes back.
                info!("device {:?} will be removed", event.topological_path);
                // Tell netstack interface is gone.
                oir::remove_interface(&event.topological_path);
            }
        }
        Ok(())
    }

    /// Update internal state with information about an LIF that was added or updated externally.
    /// When an interface not known before is found, it will add it.
    /// If it's a know interface, it will log the new operational state.
    fn notify_lif_added_or_updated(&mut self, iface: hal::Interface) -> error::Result<()> {
        match self.lif_manager.lif_at_port(iface.id) {
            Some(lif) => log_property_change(lif, iface),
            None => self.notify_lif_added(iface)?,
        };
        Ok(())
    }

    /// Update internal state with information about an LIF that was added externally.
    fn notify_lif_added(&mut self, iface: hal::Interface) -> error::Result<()> {
        if iface.get_address_v4().is_none() {
            return Ok(());
        }
        let port = iface.id;
        self.port_manager.use_port(port);
        let l = lifmgr::LIF::new(
            self.version,
            LIFType::WAN,
            &iface.topo_path,
            iface.id,
            vec![iface.id],
            0, // vlan
            Some(lifmgr::LIFProperties {
                dhcp: if iface.dhcp_client_enabled {
                    lifmgr::Dhcp::Client
                } else {
                    lifmgr::Dhcp::None
                },
                dhcp_config: None,
                address_v4: iface.get_address_v4(),
                address_v6: iface.get_address_v6(),
                enabled: true,
            }),
        )
        .or_else(|e| {
            self.port_manager.release_port(port);
            Err(e)
        })?;
        self.lif_manager.add_lif(&l).or_else(|e| {
            self.port_manager.release_port(port);
            Err(e)
        })?;
        // TODO(guzt): This should ideally compare metrics or be manually settable when there are
        // multiple WAN ports.
        if let Some(addr) = iface.get_address_v4() {
            self.service_manager.set_global_ip_nat(addr, port)
        }
        Ok(())
    }

    /// Populate state based on lower layers.
    pub async fn populate_state(&mut self) -> error::Result<()> {
        for p in self.hal.ports().await?.iter() {
            self.add_port(p.id, &p.path, self.version());
        }
        self.hal
            .interfaces()
            .await?
            .into_iter()
            .try_for_each(|iface| self.notify_lif_added(iface))?;
        self.version += 1;
        Ok(())
    }

    /// Adds a new port.
    pub fn add_port(&mut self, id: PortId, path: &str, v: Version) {
        self.port_manager.add_port(portmgr::Port::new(id, path, v));
        // This is a response to an event, not a configuration change. Version should not be
        // incremented.
    }

    /// Releases the given ports.
    fn release_ports(&mut self, ports: &[PortId]) {
        ports.iter().for_each(|p| {
            self.port_manager.release_port(*p);
        });
    }

    /// Creates a LIF of the given type.
    pub async fn create_lif(
        &mut self,
        lif_type: LIFType,
        name: String,
        vlan: Option<u16>,
        ports: &[PortId],
    ) -> error::Result<lifmgr::LIF> {
        if ports.is_empty() {
            // At least one port is needed.
            return Err(error::NetworkManager::Lif(error::Lif::NotSupported));
        }
        let reserved: Vec<PortId> = ports
            .iter()
            .filter_map(|p| if self.port_manager.use_port(*p) { Some(*p) } else { None })
            .collect();
        if reserved.len() != ports.len() {
            self.release_ports(&reserved);
            return Err(error::NetworkManager::Lif(error::Lif::InvalidPort));
        }

        // TODO(cgibson): Once we can configure VLANs, we need to handle VLAN ID 0 vs None, since
        // VLAN ID 0x0000 is a reserved value in dot1q. fxb/41746.
        let vid = match vlan {
            Some(v) => v,
            None => 0,
        };

        let mut l = lifmgr::LIF::new(
            self.version,
            lif_type,
            &name,
            PortId::from(0),
            reserved.clone(),
            vid,
            Some(LIFProperties::default()),
        )
        .or_else(|e| {
            self.release_ports(&reserved);
            Err(e)
        })?;
        if reserved.len() > 1 {
            // Multiple ports, bridge them.
            let i = self.hal.create_bridge(reserved.clone()).await.or_else(|e| {
                self.release_ports(&reserved);
                Err(e)
            })?;
            // LIF ID is associated with the bridge.
            l.set_pid(i.id);
        } else {
            l.set_pid(reserved[0]);
        }
        let r = self.lif_manager.add_lif(&l);
        if let Err(e) = r {
            self.release_ports(&reserved);
            // nothing to do if delete_bridge fails, all state changes have been reverted
            // already, just return an error to let caller handle it as appropriate.
            self.hal.delete_bridge(l.pid()).await?;
            return Err(e);
        }
        // all went well, increase version.
        self.version += 1;
        Ok(l)
    }

    /// Deletes the LIF of the given type.
    pub async fn delete_lif(&mut self, lif_id: UUID) -> Result<(), error::NetworkManager> {
        // Locate the lif to delete.
        let lif = self.lif_manager.lif_mut(&lif_id);
        let lif = match lif {
            None => {
                info!("delete_lif: lif not found {:?} ", lif_id);
                return Err(error::NetworkManager::Lif(error::Lif::NotFound));
            }
            Some(x) => x.clone(),
        };
        // reset all properties, shut down LIF
        self.hal.apply_properties(lif.pid(), lif.properties(), &LIFProperties::default()).await?;
        // delete bridge if there is one and shut down the related ports
        let ports = lif.ports();
        if ports.len() > 1 {
            self.hal.delete_bridge(lif.pid()).await?;
            //TODO(dpradilla) shut down the ports.
        }
        self.release_ports(&ports.collect::<Vec<PortId>>());
        // delete from database
        if self.lif_manager.remove_lif(lif_id).is_none() {
            return Err(error::NetworkManager::Lif(error::Lif::NotFound));
        }
        Ok(())
    }

    /// Configures an interface with the given properties, and updates the LIF information.
    pub async fn update_lif_properties_fidl(
        &mut self,
        lif_id: UUID,
        properties: &netconfig::LifProperties,
    ) -> error::Result<()> {
        let lif = match self.lif_manager.lif_mut(&lif_id) {
            None => {
                info!("update_lif_properties: lif not found {:?} ", lif_id);
                return Err(error::NetworkManager::Lif(error::Lif::NotFound));
            }
            Some(x) => x,
        };

        let old = lif.properties();
        let lp = old.get_updated(&properties)?;

        self.update_lif_properties(lif_id, &lp).await
    }

    /// Configures an interface with the given properties, and updates the LIF information.
    pub async fn update_lif_properties(
        &mut self,
        lif_id: UUID,
        lp: &lifmgr::LIFProperties,
    ) -> error::Result<()> {
        let lif = match self.lif_manager.lif_mut(&lif_id) {
            None => {
                info!("update_lif_properties: lif not found {:?} ", lif_id);
                return Err(error::NetworkManager::Lif(error::Lif::NotFound));
            }
            Some(x) => x,
        };

        let old = lif.properties();
        self.hal.apply_properties(lif.pid(), &old, &lp).await?;
        if let Some(addr) = lp.address_v4 {
            match lif.ltype() {
                LIFType::WAN => self.service_manager.set_global_ip_nat(addr.clone(), lif.pid()),
                LIFType::LAN => self.service_manager.set_local_subnet_nat(addr.clone()),
                _ => (),
            }
        }
        match self.packet_filter.update_nat_config(self.service_manager.get_nat_config()).await {
            // If the result of this call was `UpdateNatPendingConfig` or `NatNotEnabled`, then NAT
            // is not yet ready to be enabled until we have more configuration data.
            Ok(())
            | Err(error::NetworkManager::Service(error::Service::UpdateNatPendingConfig))
            | Err(error::NetworkManager::Service(error::Service::NatNotEnabled)) => {}
            Err(e) => {
                // Otherwise, this was an actual error and we should not increment the
                // version number.
                error!("Failed to update NAT rules: {:?}; Version not incremented.", e);
                return Err(e);
            }
        }
        lif.set_properties(self.version, lp.clone())?;
        self.version += 1;
        Ok(())
    }

    /// Returns the LIF with the given `UUID`.
    pub fn lif(&self, lif_id: UUID) -> Option<&lifmgr::LIF> {
        self.lif_manager.lif(&lif_id)
    }

    /// Returns all LIFs of the given type.
    pub fn lifs(&self, t: LIFType) -> impl Iterator<Item = &lifmgr::LIF> {
        self.lif_manager.lifs(t)
    }

    /// Returns all managed ports.
    pub fn ports(&self) -> impl ExactSizeIterator<Item = &portmgr::Port> {
        self.port_manager.ports()
    }

    /// Returns the current configuration version.
    ///
    /// The configuration version is monotonically increased every time there is a configuration
    /// change.
    pub fn version(&self) -> Version {
        self.version
    }

    /// Returns whether NAT is administratively enabled.
    pub fn is_nat_enabled(&self) -> bool {
        self.service_manager.is_nat_enabled()
    }

    /// Enables NAT.
    ///
    /// Administratively enable Network Address Translation.
    pub fn enable_nat(&mut self) {
        self.service_manager.enable_nat();
    }

    /// Disables NAT.
    ///
    /// Administratively disable Network Address Translation.
    pub fn disable_nat(&mut self) {
        self.service_manager.disable_nat();
    }

    /// Installs a new packet filter [`netconfig::FilterRule`] on the provided `nicid`.
    pub async fn set_filter_on_interface(
        &self,
        rule: &netconfig::FilterRule,
        nicid: u32,
    ) -> error::Result<()> {
        match self.packet_filter.set_filter(rule, nicid).await {
            Ok(_) => Ok(()),
            Err(e) => {
                warn!("Failed to set new filter rules: {:?}", e);
                Err(error::NetworkManager::Service(error::Service::ErrorAddingPacketFilterRules))
            }
        }
    }

    /// Deletes a packet filter rule.
    pub async fn delete_filter(&self, _id: netconfig::Id) -> error::Result<()> {
        // TODO(44183): Currently this method deletes all of the installed filter rules.
        // We need to associate each rule with an id when it is installed, so that individual filter
        // rules can be removed.
        warn!("Deleting numbered filter rules is not supported yet: Clearing all rules instead.");

        // TODO(cgibson): Need to support tracking element numbers. fxb/44183.
        match self.packet_filter.clear_filters().await {
            Ok(_) => Ok(()),
            Err(e) => {
                warn!("Failed to clear filter rules: {:?}", e);
                Err(error::NetworkManager::Service(error::Service::ErrorClearingPacketFilterRules))
            }
        }
    }

    /// Returns the currently installed packet filter rules.
    pub async fn get_filters(&self) -> error::Result<Vec<netconfig::FilterRule>> {
        self.packet_filter.get_filters().await.map_err(|e| {
            warn!("Failed to get filter rules: {:?}", e);
            error::NetworkManager::Service(error::Service::ErrorGettingPacketFilterRules)
        })
    }

    /// Sets the DNS servers for name resolution.
    ///
    /// `servers` must be sorted in priority order, with the most preferred server at the front
    /// of the list.
    pub async fn set_dns_resolvers(
        &mut self,
        mut servers: Vec<fnet::SocketAddress>,
    ) -> error::Result<ElementId> {
        if servers == self.dns_config.servers {
            // No change needed.
            return Ok(self.dns_config.id);
        }
        // Just return the error, nothing to undo.
        //
        // NB: iter().map(...) is used over clone() to avoid allocating a vector.
        self.hal.set_dns_resolvers(servers.iter_mut()).await?;
        self.dns_config.id.version = self.version;
        self.dns_config.servers = servers;
        self.version += 1;
        Ok(self.dns_config.id)
    }

    /// Returns the dns resolver configuration.
    pub async fn get_dns_resolver(&self) -> netconfig::DnsResolverConfig {
        netconfig::DnsResolverConfig::from(&self.dns_config)
    }
}

/// Log that the lif properties have changed.
///
/// This will be later use to update the operational state, but not the configuration state.
/// (we are not currently caching operational state, just querying for it).
fn log_property_change(lif: &mut lifmgr::LIF, iface: hal::Interface) {
    let properties = lif.properties();
    info!("log_property_change {:?} {:?}", iface.id, iface.name);
    let new_properties: lifmgr::LIFProperties = iface.into();
    if properties != &new_properties {
        info!("Properties have changed {:?}: new properties {:?}", properties, new_properties);
    }
}

/// Represents the version of the configuration associated to a device object (i.e. an
/// interface, and ACL, etc). It should only be updated when configuration state is updated. It
/// should never be updated due to operational state changes.
///
/// For example, if an interface is configured to get its IP address via DHCP, the configuration
/// is changed to enable DHCP client on the interface. The address received from DHCP is an
/// operational state change. It is not to be saved in the configuration.
///
/// Adding a static neighbor entry to the neighbor table is a configuration change. Learning an
/// entry dynamically and adding it to the neighbor table is an operational change.
///
/// For a good definition of configuration vs. operational state, please see:
/// https://tools.ietf.org/html/rfc6244#section-4.3
type Version = u64;
type UUID = u128;

static ID: AtomicUsize = AtomicUsize::new(0);

fn generate_uuid() -> UUID {
    ID.fetch_add(1, Ordering::Relaxed) as UUID
}

#[derive(Eq, PartialEq, Debug, Copy, Clone, Default)]
pub struct ElementId {
    uuid: UUID,
    version: Version,
}

impl ElementId {
    pub fn new(v: Version) -> Self {
        ElementId { uuid: generate_uuid(), version: v }
    }
    pub fn update(&mut self, version: Version) {
        self.version = version;
    }
    pub fn uuid(&self) -> UUID {
        self.uuid
    }
    pub fn version(&self) -> u64 {
        self.version
    }
}

impl From<netconfig::Id> for ElementId {
    fn from(id: netconfig::Id) -> Self {
        ElementId { uuid: u128::from_ne_bytes(id.uuid), version: id.version }
    }
}

impl From<ElementId> for netconfig::Id {
    fn from(id: ElementId) -> Self {
        netconfig::Id { uuid: id.uuid.to_ne_bytes(), version: id.version }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::address::LifIpAddr;
    use fidl_fuchsia_net as net;
    use fuchsia_async as fasync;
    use std::net::IpAddr;

    #[test]
    fn test_elementid() {
        let uuid = generate_uuid();
        let version = 4;
        let new_version = 34;

        let mut e = ElementId { uuid, version };

        assert_eq!(e.uuid(), uuid);
        assert_eq!(e.version(), version);

        e.update(new_version);
        assert_eq!(e.version(), new_version);

        assert_eq!(
            netconfig::Id::from(e),
            netconfig::Id { uuid: uuid.to_ne_bytes(), version: new_version },
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_new_device_state() {
        let d = DeviceState::new(
            hal::NetCfg::new().unwrap(),
            packet_filter::PacketFilter::start().unwrap(),
        );

        assert_eq!(d.version, 0);
        assert_eq!(d.port_manager.ports().count(), 0);
        assert_eq!(d.lif_manager.all_lifs().count(), 0);
    }

    fn net_interface_with_flags(
        port: u32,
        addr: [u8; 4],
        dhcp: bool,
        enabled: bool,
    ) -> netstack::NetInterface {
        let mut flags = netstack::Flags::empty();
        if enabled {
            flags |= netstack::Flags::Up;
        }
        if dhcp {
            flags |= netstack::Flags::Dhcp;
        }
        netstack::NetInterface {
            id: port,
            flags,
            features: fidl_fuchsia_hardware_ethernet::Features::empty(),
            configuration: 0,
            name: port.to_string(),
            addr: net::IpAddress::Ipv4(net::Ipv4Address { addr }),
            netmask: net::IpAddress::Ipv4(net::Ipv4Address { addr: [255, 255, 255, 0] }),
            broadaddr: net::IpAddress::Ipv4(net::Ipv4Address { addr: [1, 2, 3, 255] }),
            ipv6addrs: vec![],
            hwaddr: [1, 2, 3, 4, 5, port as u8].to_vec(),
        }
    }

    fn net_interface(port: u32, addr: [u8; 4]) -> netstack::NetInterface {
        net_interface_with_flags(port, addr, true, true)
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_update_state_for_netstack_event() {
        let mut device_state = DeviceState::new(
            hal::NetCfg::new().unwrap(),
            packet_filter::PacketFilter::start().unwrap(),
        );

        // Create a few ports.
        device_state.port_manager.add_port(portmgr::Port {
            e_id: ElementId::new(1),
            port_id: hal::PortId::from(5),
            path: "path".to_string(),
        });
        device_state.port_manager.add_port(portmgr::Port {
            e_id: ElementId::new(2),
            port_id: hal::PortId::from(4),
            path: "path".to_string(),
        });
        let ports = device_state.ports().collect::<Vec<&portmgr::Port>>();
        assert_eq!(ports.len(), 2);

        // Netstack informs network manager of a new interface at port 5 with ip 1.2.3.4.
        assert!(device_state
            .update_state_for_netstack_event(netstack::NetstackEvent::OnInterfacesChanged {
                interfaces: vec![net_interface(5, [1, 2, 3, 4])],
            },)
            .await
            .is_ok());
        // Port 5 should now be marked used.
        assert!(
            !device_state.port_manager.port_available(&hal::PortId::from(5)).unwrap(),
            "lif at port {:?}: {:?}",
            5,
            device_state.lif_manager.lif_at_port(5.into())
        );
        // Assert that network manager now knows about this new interface.
        {
            let lifs: Vec<&lifmgr::LIF> = device_state.lifs(LIFType::WAN).collect();
            assert_eq!(lifs.len(), 1);
            assert_eq!(lifs[0].pid().to_u64(), 5);
            // Also make sure that it is set as the NAT global IP.
            assert_eq!(
                device_state.service_manager.get_nat_config().global_ip,
                Some(LifIpAddr { address: IpAddr::from([1, 2, 3, 4]), prefix: 24 })
            );
        }

        // Netstack informs network manager of a new interface at port 4.
        assert!(device_state
            .update_state_for_netstack_event(netstack::NetstackEvent::OnInterfacesChanged {
                interfaces: vec![net_interface(4, [3, 4, 5, 6])],
            },)
            .await
            .is_ok());
        // Assert that network manager now knows about the new interface and updates to the other
        // interface.
        {
            let lifs: Vec<&lifmgr::LIF> = device_state.lifs(LIFType::WAN).collect();
            lifs.iter().find(|lif| lif.pid().to_u64() == 5).unwrap();
            lifs.iter().find(|lif| lif.pid().to_u64() == 4).unwrap();
        }
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_dont_update_state_for_netstack_event_without_ip() {
        let mut device_state = DeviceState::new(
            hal::NetCfg::new().unwrap(),
            packet_filter::PacketFilter::start().unwrap(),
        );
        device_state.port_manager.add_port(portmgr::Port {
            e_id: ElementId::new(1),
            port_id: hal::PortId::from(5),
            path: "path".to_string(),
        });
        assert!(device_state
            .update_state_for_netstack_event(netstack::NetstackEvent::OnInterfacesChanged {
                interfaces: vec![
                    net_interface_with_flags(5, [0, 0, 0, 0], false, true),
                    net_interface_with_flags(5, [0, 0, 0, 0], true, false),
                ],
            },)
            .await
            .is_ok());
        // Port should still be free.
        assert!(device_state.port_manager.port_available(&hal::PortId::from(5)).unwrap());
        assert_eq!(device_state.lifs(LIFType::WAN).next(), None);
    }
}
