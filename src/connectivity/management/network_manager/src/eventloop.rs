// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The special-purpose event loop used by the network manager.
//!
//! This event loop takes in events from Admin and State FIDL,
//! and implements handlers for FIDL calls.
//!
//! This is implemented with a single mpsc queue for all event types - `EventLoop` holds the
//! consumer, and any event handling that requires state within `EventLoop` holds a producer,
//! allowing it to delegate work to the `EventLoop` by sending a message. In this documentation, we
//! call anything that holds a producer a "worker".
//!
//! Having a single queue for all of the message types is beneficial, since in guarantees a FIFO
//! ordering for all messages - whichever messages arrive first, will be handled first.
//!
//! We'll look at each type of message, to see how each one is handled - starting with FIDL
//! messages, since they can be thought of as the entrypoint for the whole loop (as nothing happens
//! until a FIDL call is made).
//!
//! # FIDL Worker
//!
//! The FIDL part of the event loop implements the fuchsia.router.config Admin and State
//! interfaces. The type of the event loop message for a FIDL call is
//! simply the generated FIDL type. When the event loop starts up, we use `fuchsia_app` to start a
//! FIDL server that simply sends all of the events it receives to the event loop (via the sender
//! end of the mpsc queue). When `EventLoop` receives this message, it calls the
//! `handle_fidl_router_admin_request` or `handle_fidl_router_state_request` method, which,
//! depending on what the request is, either:
//!
//! * Responds with the requested information.
//! * Modifies the router state byt accessinc netcfg.
//! * Updates local state.

use crate::event::Event;
use failure::{bail, Error, ResultExt};
use fidl_fuchsia_router_config::{
    Id, Lif, Port, RouterAdminRequest, RouterStateGetPortsResponder, RouterStateRequest,
    SecurityFeatures,
};
use futures::channel::mpsc;
use futures::prelude::*;
use network_manager_core::{
    hal::NetCfg, lifmgr::LIFType, packet_filter::PacketFilter, portmgr::PortId, DeviceState,
};

macro_rules! router_error {
    ($code:ident, $desc:expr) => {
        Some(fidl::encoding::OutOfLine(&mut fidl_fuchsia_router_config::Error {
            code: fidl_fuchsia_router_config::ErrorCode::$code,
            description: $desc,
        }))
    };
}

macro_rules! not_supported {
    () => {
        router_error!(NotSupported, None)
    };
}
macro_rules! not_found {
    () => {
        router_error!(NotFound, None)
    };
}
macro_rules! internal_error {
    ($s:expr) => {
        router_error!(Internal, Some($s.to_string()))
    };
}

/// The event loop.
pub struct EventLoop {
    event_recv: mpsc::UnboundedReceiver<Event>,
    device: DeviceState,
}

impl EventLoop {
    pub fn new() -> Result<Self, Error> {
        let netcfg = NetCfg::new()?;
        let packet_filter = PacketFilter::start()
            .context("network_manager failed to start packet filter!")
            .unwrap();
        let mut device = DeviceState::new(netcfg, packet_filter);

        let streams = device.take_event_streams();
        let (event_send, event_recv) = futures::channel::mpsc::unbounded::<Event>();
        let fidl_worker = crate::fidl_worker::FidlWorker;
        let _ = fidl_worker.spawn(event_send.clone());
        let overnet_worker = crate::overnet_worker::OvernetWorker;
        let _r = overnet_worker.spawn(event_send.clone());
        let event_worker = crate::event_worker::EventWorker;
        event_worker.spawn(streams, event_send.clone());
        let oir_worker = crate::oir_worker::OirWorker;
        oir_worker.spawn(event_send);

        Ok(EventLoop { event_recv, device })
    }

    pub async fn run(mut self) -> Result<(), Error> {
        if let Err(e) = self.device.load_config().await {
            // TODO(cgibson): Kick off some sort of recovery process here: Prompt the user to
            // download a recovery app, set a static IP on the first interface and set everything
            // else down, restructure packet filter rules, etc.
            error!("Failed to load a device config: {}", e);
        }
        self.device.populate_state().await?;

        loop {
            match self.event_recv.next().await {
                Some(Event::FidlRouterAdminEvent(req)) => {
                    self.handle_fidl_router_admin_request(req).await;
                }
                Some(Event::FidlRouterStateEvent(req)) => {
                    self.handle_fidl_router_state_request(req).await;
                }
                Some(Event::StackEvent(event)) => self.handle_stack_event(event).await,
                Some(Event::NetstackEvent(event)) => self.handle_netstack_event(event).await,
                Some(Event::OIR(event)) => self.handle_oir_event(event).await,
                None => bail!("Stream of events ended unexpectedly"),
            }
        }
    }

    async fn handle_stack_event(&mut self, event: fidl_fuchsia_net_stack::StackEvent) {
        self.device
            .update_state_for_stack_event(event)
            .await
            .unwrap_or_else(|err| warn!("error updating state: {:?}", err));
    }

    async fn handle_netstack_event(&mut self, event: fidl_fuchsia_netstack::NetstackEvent) {
        self.device
            .update_state_for_netstack_event(event)
            .await
            .unwrap_or_else(|err| warn!("error updating state: {:?}", err));
    }

    async fn handle_oir_event(&mut self, event: network_manager_core::oir::OIRInfo) {
        self.device
            .oir_event(event)
            .await
            .unwrap_or_else(|err| warn!("error processing oir event: {:?}", err));
    }

    async fn handle_fidl_router_admin_request(&mut self, req: RouterAdminRequest) {
        match req {
            RouterAdminRequest::CreateWan { name, vlan, ports, responder } => {
                let r = self
                    .fidl_create_lif(
                        LIFType::WAN,
                        name,
                        vlan,
                        ports.iter().map(|x| PortId::from(u64::from(*x))).collect(),
                    )
                    .await;
                match r {
                    Ok(mut id) => responder.send(Some(fidl::encoding::OutOfLine(&mut id)), None),
                    Err(mut e) => responder.send(None, Some(fidl::encoding::OutOfLine(&mut e))),
                }
            }
            RouterAdminRequest::CreateLan { name, vlan, ports, responder } => {
                let r = self
                    .fidl_create_lif(
                        LIFType::LAN,
                        name,
                        vlan,
                        ports.iter().map(|x| PortId::from(u64::from(*x))).collect(),
                    )
                    .await;
                match r {
                    Ok(mut id) => responder.send(Some(fidl::encoding::OutOfLine(&mut id)), None),
                    Err(mut e) => responder.send(None, Some(fidl::encoding::OutOfLine(&mut e))),
                }
            }
            RouterAdminRequest::RemoveWan { wan_id, responder } => {
                let mut r = self.fidl_delete_lif(wan_id).await;
                responder.send(r.as_mut().map(fidl::encoding::OutOfLine)).or_else(|e| {
                    error!("Error sending response: {:?}", e);
                    Err(e)
                })
            }
            RouterAdminRequest::RemoveLan { lan_id, responder } => {
                let mut r = self.fidl_delete_lif(lan_id).await;
                responder.send(r.as_mut().map(fidl::encoding::OutOfLine)).or_else(|e| {
                    error!("Error sending response: {:?}", e);
                    Err(e)
                })
            }
            RouterAdminRequest::SetWanProperties { wan_id, properties, responder } => {
                let properties = fidl_fuchsia_router_config::LifProperties::Wan(properties);
                if self
                    .device
                    .update_lif_properties(u128::from_ne_bytes(wan_id.uuid), &properties)
                    .await
                    .is_err()
                {
                    warn!("WAN {:?} found but failed to update properties", wan_id);
                    responder.send(not_found!())
                } else {
                    info!("WAN properties updated");
                    responder.send(None)
                }
            }
            RouterAdminRequest::SetLanProperties { lan_id, properties, responder } => {
                let properties = fidl_fuchsia_router_config::LifProperties::Lan(properties);
                if self
                    .device
                    .update_lif_properties(u128::from_ne_bytes(lan_id.uuid), &properties)
                    .await
                    .is_err()
                {
                    warn!("failed to update LAN properties");
                    responder.send(not_found!())
                } else {
                    info!("LAN properties updated");
                    responder.send(None)
                }
            }
            RouterAdminRequest::SetDhcpServerOptions { lan_id, options, responder } => {
                info!("{:?}, {:?}", lan_id, options);
                responder.send(not_supported!())
            }
            RouterAdminRequest::SetDhcpAddressPool { lan_id, pool, responder } => {
                info!("{:?}, {:?}", lan_id, pool);
                responder.send(not_supported!())
            }
            RouterAdminRequest::SetDhcpReservation { lan_id, reservation, responder } => {
                info!("{:?}, {:?}", lan_id, reservation);
                responder.send(None, not_supported!())
            }
            RouterAdminRequest::DeleteDhcpReservation { reservation_id, responder } => {
                info!("{:?}", reservation_id);
                responder.send(not_supported!())
            }
            RouterAdminRequest::SetDnsResolver { mut config, responder } => {
                info!("{:?}", config);
                match self
                    .device
                    .set_dns_resolver(
                        &mut config.search.servers,
                        config.search.domain_name,
                        config.policy,
                    )
                    .await
                {
                    Ok(i) => responder.send(
                        Some(fidl::encoding::OutOfLine(&mut Id {
                            uuid: i.uuid().to_ne_bytes(),
                            version: i.version(),
                        })),
                        None,
                    ),
                    Err(e) => responder.send(None, internal_error!(e.to_string())),
                }
            }
            RouterAdminRequest::SetDnsForwarder { config, responder } => {
                info!("{:?}", config);
                responder.send(not_supported!())
            }
            RouterAdminRequest::AddDnsEntry { entry, responder } => {
                info!("{:?}", entry);
                responder.send(None, not_supported!())
            }
            RouterAdminRequest::DeleteDnsEntry { entry_id, responder } => {
                info!("{:?}", entry_id);
                responder.send(not_supported!())
            }
            RouterAdminRequest::SetRoute { route, responder } => {
                info!("{:?}", route);
                responder.send(None, not_supported!())
            }
            RouterAdminRequest::UpdateRouteMetric { route_id, metric, responder } => {
                info!("{:?} {:?}", route_id, metric);
                responder.send(not_supported!())
            }
            RouterAdminRequest::DeleteRoute { route_id, responder } => {
                info!("{:?}", route_id);
                responder.send(not_supported!())
            }
            RouterAdminRequest::SetSecurityFeatures { features, responder } => {
                info!("Updating SecurityFeatures: {:?}", features);
                match self.update_security_features(&features).await {
                    Ok(_) => responder.send(None),
                    Err(e) => responder.send(internal_error!(e.to_string())),
                }
            }
            RouterAdminRequest::SetPortForward { rule, responder } => {
                info!("{:?}", rule);
                responder.send(None, not_supported!())
            }
            RouterAdminRequest::DeletePortForward { rule_id, responder } => {
                info!("{:?}", rule_id);
                responder.send(not_supported!())
            }
            RouterAdminRequest::SetPortTrigger { rule, responder } => {
                info!("{:?}", rule);
                responder.send(None, not_supported!())
            }
            RouterAdminRequest::DeletePortTrigger { rule_id, responder } => {
                info!("{:?}", rule_id);
                responder.send(not_supported!())
            }
            RouterAdminRequest::SetFilter { rule, responder } => {
                let r = self
                    .device
                    .set_filter(rule)
                    .await
                    .context("Error installing new packet filter rule");
                match r {
                    Ok(()) => responder.send(None, None),
                    Err(e) => responder.send(None, internal_error!(e.to_string())),
                }
            }
            RouterAdminRequest::DeleteFilter { rule_id, responder } => {
                info!("{:?}", rule_id);
                responder.send(not_supported!())
            }
            RouterAdminRequest::SetIpv6PinHole { rule, responder } => {
                info!("{:?}", rule);
                responder.send(None, not_supported!())
            }
            RouterAdminRequest::DeleteIpv6PinHole { rule_id, responder } => {
                info!("{:?}", rule_id);
                responder.send(not_supported!())
            }
            RouterAdminRequest::SetDmzHost { rule, responder } => {
                info!("{:?}", rule);
                responder.send(None, not_supported!())
            }
            RouterAdminRequest::DeleteDmzHost { rule_id, responder } => {
                info!("{:?}", rule_id);
                responder.send(not_supported!())
            }
            RouterAdminRequest::SetSystemConfig { config, responder } => {
                info!("{:?}", config);
                responder.send(None, not_supported!())
            }
            RouterAdminRequest::CreateWlanNetwork { responder, .. } => {
                // TODO(guzt): implement
                responder.send(None, not_supported!())
            }
            RouterAdminRequest::DeleteWlanNetwork { responder, .. } => {
                // TODO(guzt): implement
                responder.send(not_supported!())
            }
        }
        .unwrap_or_else(|e| error!("Error sending response {}", e))
    }

    async fn fidl_create_lif(
        &mut self,
        lif_type: LIFType,
        name: String,
        vlan: u16,
        ports: Vec<PortId>,
    ) -> Result<Id, fidl_fuchsia_router_config::Error> {
        let lif = self.device.create_lif(lif_type, name, vlan, ports).await;
        match lif {
            Err(e) => {
                error!("Error creating lif {:?}", e);
                Err(fidl_fuchsia_router_config::Error {
                    code: fidl_fuchsia_router_config::ErrorCode::AlreadyExists,
                    description: None,
                })
            }
            Ok(l) => {
                let i = l.id();
                Ok(Id { uuid: i.uuid().to_ne_bytes(), version: i.version() })
            }
        }
    }

    async fn fidl_delete_lif(&mut self, id: Id) -> Option<fidl_fuchsia_router_config::Error> {
        let lif = self.device.delete_lif(u128::from_ne_bytes(id.uuid)).await;
        match lif {
            Err(e) => {
                error!("Error deleting lif {:?}", e);
                Some(fidl_fuchsia_router_config::Error {
                    code: fidl_fuchsia_router_config::ErrorCode::NotFound,
                    description: None,
                })
            }
            Ok(()) => None,
        }
    }

    async fn update_security_features(
        &mut self,
        security_features: &SecurityFeatures,
    ) -> Result<(), Error> {
        if let Some(nat) = security_features.nat {
            if nat {
                self.device.enable_nat().await?;
            } else {
                self.device.disable_nat().await?;
            }
        }
        // TODO(cgibson): Handle additional SecurityFeatures.
        Ok(())
    }

    async fn handle_fidl_router_state_request(&mut self, req: RouterStateRequest) {
        match req {
            RouterStateRequest::GetWanPorts { wan_id, responder } => {
                let lif = self.device.lif(u128::from_ne_bytes(wan_id.uuid));
                match lif {
                    None => {
                        warn!("WAN {:?} not found", wan_id);
                        responder.send(&mut None.into_iter(), not_found!())
                    }
                    Some(l) => responder.send(&mut l.ports().map(|p| p.to_u32()), None),
                }
            }
            RouterStateRequest::GetLanPorts { lan_id, responder } => {
                let lif = self.device.lif(u128::from_ne_bytes(lan_id.uuid));
                match lif {
                    None => {
                        warn!("LAN {:?} not found", lan_id);
                        responder.send(&mut None.into_iter(), not_found!())
                    }
                    Some(l) => responder.send(&mut l.ports().map(|p| p.to_u32()), None),
                }
            }
            RouterStateRequest::GetWan { wan_id, responder } => {
                let lif = self.device.lif(u128::from_ne_bytes(wan_id.uuid));
                info!("lifs {:?}", lif);
                match lif {
                    None => {
                        warn!("WAN {:?} not found", wan_id);
                        responder.send(
                            fidl_fuchsia_router_config::Lif {
                                element: None,
                                name: None,
                                port_ids: None,
                                properties: None,
                                vlan: None,
                                type_: None,
                            },
                            not_found!(),
                        )
                    }
                    Some(l) => {
                        let ll = l.to_fidl_lif();
                        responder.send(ll, None)
                    }
                }
            }
            RouterStateRequest::GetLan { lan_id, responder } => {
                let lif = self.device.lif(u128::from_ne_bytes(lan_id.uuid));
                match lif {
                    None => {
                        warn!("LAN {:?} not found", lan_id);
                        responder.send(
                            fidl_fuchsia_router_config::Lif {
                                element: None,
                                name: None,
                                port_ids: None,
                                properties: None,
                                vlan: None,
                                type_: None,
                            },
                            not_found!(),
                        )
                    }
                    Some(l) => {
                        let ll = l.to_fidl_lif();
                        responder.send(ll, None)
                    }
                }
            }
            RouterStateRequest::GetWans { responder } => {
                let lifs: Vec<Lif> =
                    self.device.lifs(LIFType::WAN).map(|l| l.to_fidl_lif()).collect();
                info!("result: {:?} ", lifs);
                responder.send(&mut lifs.into_iter())
            }
            RouterStateRequest::GetLans { responder } => {
                let lifs: Vec<Lif> =
                    self.device.lifs(LIFType::LAN).map(|l| l.to_fidl_lif()).collect();
                info!("result: {:?} ", lifs);
                responder.send(&mut lifs.into_iter())
            }
            RouterStateRequest::GetWanProperties { wan_id, responder } => {
                info!("{:?}", wan_id);
                let properties = fidl_fuchsia_router_config::WanProperties {
                    connection_type: None,
                    connection_parameters: None,
                    address_method: None,
                    address_v4: None,
                    gateway_v4: None,
                    connection_v6_mode: None,
                    address_v6: None,
                    gateway_v6: None,
                    hostname: None,
                    clone_mac: None,
                    mtu: None,
                    enable: None,
                    metric: None,
                };
                responder.send(properties, not_supported!())
            }
            RouterStateRequest::GetLanProperties { lan_id, responder } => {
                info!("{:?}", lan_id);
                let properties = fidl_fuchsia_router_config::LanProperties {
                    address_v4: None,
                    enable_dhcp_server: None,
                    dhcp_config: None,
                    address_v6: None,
                    enable_dns_forwarder: None,
                    enable: None,
                };

                responder.send(properties, not_supported!())
            }
            RouterStateRequest::GetDhcpConfig { lan_id, responder } => {
                info!("{:?}", lan_id);
                responder.send(None, not_supported!())
            }
            RouterStateRequest::GetDnsResolver { responder } => {
                let mut resolver = self.device.get_dns_resolver().await;
                responder.send(&mut resolver)
            }
            RouterStateRequest::GetDnsForwarder { responder } => {
                let config = fidl_fuchsia_router_config::DnsForwarderConfig {
                    element: Id {
                        uuid: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
                        version: 0,
                    },
                    search: fidl_fuchsia_router_config::DnsSearch {
                        domain_name: None,
                        servers: vec![],
                    },
                };
                let mut forwarder = fidl_fuchsia_router_config::DnsForwarder {
                    config,
                    interfaces: vec![],
                    resolver: vec![],
                };
                responder.send(&mut forwarder)
            }
            RouterStateRequest::GetRoutes { responder } => responder.send(&mut [].iter_mut()),
            RouterStateRequest::GetRoute { route_id, responder } => {
                info!("{:?}", route_id);
                responder.send(None, not_supported!())
            }
            RouterStateRequest::GetSecurityFeatures { responder } => {
                let security_features = fidl_fuchsia_router_config::SecurityFeatures {
                    allow_multicast: None,
                    drop_icmp_echo: None,
                    firewall: None,
                    h323_passthru: None,
                    ipsec_passthru: None,
                    l2_tp_passthru: None,
                    nat: Some(self.device.is_nat_enabled()),
                    pptp_passthru: None,
                    rtsp_passthru: None,
                    sip_passthru: None,
                    upnp: None,
                    v6_firewall: None,
                };
                responder.send(security_features)
            }
            RouterStateRequest::GetPortForwards { responder } => responder.send(&mut [].iter_mut()),
            RouterStateRequest::GetPortForward { rule_id, responder } => {
                info!("{:?}", rule_id);
                responder.send(None, not_supported!())
            }
            RouterStateRequest::GetPorts { responder } => {
                self.fidl_get_ports(responder).await;
                Ok(())
            }
            RouterStateRequest::GetPort { port_id, responder } => {
                info!("{:?}", port_id);
                responder.send(None, not_supported!())
            }
            RouterStateRequest::GetPortTrigger { rule_id, responder } => {
                info!("{:?}", rule_id);
                responder.send(None, not_supported!())
            }
            RouterStateRequest::GetPortTriggers { responder } => responder.send(&mut [].iter_mut()),
            RouterStateRequest::GetFilter { rule_id, responder } => {
                info!("{:?}", rule_id);
                responder.send(None, not_supported!())
            }
            RouterStateRequest::GetFilters { responder } => {
                let result = self.device.get_filters().await.context("Error getting filters");
                let mut filter_rules = Vec::new();
                match result {
                    Ok(f) => {
                        filter_rules = f.into_iter().collect();
                        info!("Filter rules returned: {:?}", filter_rules.len());
                    }
                    Err(e) => error!("Failed parsing filter rules: {}", e),
                }
                responder.send(&mut filter_rules.iter_mut())
            }
            RouterStateRequest::GetIpv6PinHole { rule_id, responder } => {
                info!("{:?}", rule_id);
                responder.send(None, not_supported!())
            }
            RouterStateRequest::GetIpv6PinHoles { responder } => responder.send(&mut [].iter_mut()),
            RouterStateRequest::GetDmzHost { rule_id, responder } => {
                info!("{:?}", rule_id);
                responder.send(None, not_supported!())
            }
            RouterStateRequest::GetSystemConfig { responder } => {
                let config = fidl_fuchsia_router_config::SystemConfig {
                    element: None,
                    timezone: None,
                    daylight_savings_time_enabled: None,
                    leds_enabled: None,
                    hostname: None,
                };
                responder.send(config)
            }
            RouterStateRequest::GetDevice { responder } => {
                let device = fidl_fuchsia_router_config::Device {
                    version: None,
                    topology: None,
                    config: None,
                };
                responder.send(device)
            }
            RouterStateRequest::GetWlanNetworks { responder } => {
                responder.send(&mut vec![].into_iter())
            }
            RouterStateRequest::GetRadios { responder } => responder.send(&mut vec![].into_iter()),
        }
        .unwrap_or_else(|e| error!("Error sending response {}", e))
    }

    async fn fidl_get_ports(&mut self, responder: RouterStateGetPortsResponder) {
        let ps = self.device.ports();
        let mut ports: Vec<Port> = ps
            .map(|p| Port {
                element: Id { uuid: p.e_id.uuid().to_ne_bytes(), version: p.e_id.version() },
                id: p.port_id.to_u32(),
                path: p.path.clone(),
            })
            .collect();
        responder.send(&mut ports.iter_mut()).context("Error sending a response").unwrap();
    }
}
