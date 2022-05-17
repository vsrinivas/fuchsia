// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! LoWPAN Network Tunnel Abstraction
use super::debug::*;
use super::iface::*;
use crate::prelude_internal::*;

use crate::spinel::Subnet;
use anyhow::Error;
use async_trait::async_trait;
use fidl::endpoints::{create_endpoints, create_proxy};
use fidl_fuchsia_hardware_network as fhwnet;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_ext as fnetext;
use fidl_fuchsia_net_interfaces_admin as fnetifadmin;
use fidl_fuchsia_net_interfaces_ext as fnetifext;
use fidl_fuchsia_net_stack as fnetstack;
use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use fidl_fuchsia_net_tun as ftun;
use fuchsia_component::client::{connect_channel_to_protocol, connect_to_protocol};
use fuchsia_zircon as zx;
use futures::stream::BoxStream;
use parking_lot::Mutex;
use std::convert::TryInto;
use std::net::{Ipv6Addr, UdpSocket};

const IPV6_MIN_MTU: u32 = 1280;
const TUN_PORT_ID: u8 = 0;

#[derive(Debug)]
pub struct TunNetworkInterface {
    tun_dev: ftun::DeviceProxy,
    tun_port: ftun::PortProxy,
    #[allow(unused)] // TODO (fxb/64704): use `control` after converting methods to async.
    control: fnetifext::admin::Control,
    control_sync: Mutex<fnetifadmin::ControlSynchronousProxy>,
    stack_sync: Mutex<fnetstack::StackSynchronousProxy>,
    mcast_socket: UdpSocket,
    id: u64,
}

impl TunNetworkInterface {
    pub async fn try_new(name: Option<String>) -> Result<TunNetworkInterface, Error> {
        let tun_control = connect_to_protocol::<ftun::ControlMarker>()?;

        let (tun_dev, req) = create_proxy::<ftun::DeviceMarker>()?;

        tun_control
            .create_device(
                ftun::DeviceConfig { blocking: Some(true), ..ftun::DeviceConfig::EMPTY },
                req,
            )
            .context("failed to create tun pair")?;

        let (tun_port, port_req) = create_proxy::<ftun::PortMarker>()?;
        tun_dev
            .add_port(
                ftun::DevicePortConfig {
                    base: Some(ftun::BasePortConfig {
                        id: Some(TUN_PORT_ID),
                        mtu: Some(IPV6_MIN_MTU),
                        rx_types: Some(vec![
                            fhwnet::FrameType::Ipv6,
                            // TODO(fxbug.dev/64292): Remove this once netstack doesn't require it.
                            fhwnet::FrameType::Ipv4,
                        ]),
                        tx_types: Some(vec![
                            fhwnet::FrameTypeSupport {
                                type_: fhwnet::FrameType::Ipv6,
                                features: fhwnet::FRAME_FEATURES_RAW,
                                supported_flags: fhwnet::TxFlags::empty(),
                            },
                            // TODO(fxbug.dev/64292): Remove this once netstack doesn't require it.
                            fhwnet::FrameTypeSupport {
                                type_: fhwnet::FrameType::Ipv4,
                                features: fhwnet::FRAME_FEATURES_RAW,
                                supported_flags: fhwnet::TxFlags::empty(),
                            },
                        ]),
                        ..ftun::BasePortConfig::EMPTY
                    }),
                    ..ftun::DevicePortConfig::EMPTY
                },
                port_req,
            )
            .context("failed to add device port")?;

        let (device, device_req) = create_endpoints::<fhwnet::DeviceMarker>()?;

        tun_dev.get_device(device_req).context("get device failed")?;

        let (control, control_sync) = {
            let installer = connect_to_protocol::<fnetifadmin::InstallerMarker>()?;
            let (device_control, server_end) = create_proxy::<fnetifadmin::DeviceControlMarker>()?;
            installer.install_device(device, server_end).context("install_device failed")?;
            // Interface lifetime is already tied to us because of tun device,
            // no need to keep this extra channel around.
            device_control.detach().context("device control detach failed")?;

            let (port, server_end) = create_proxy::<fhwnet::PortMarker>()?;
            tun_port.get_port(server_end).context("get_port failed")?;
            let mut port_id = port
                .get_info()
                .await
                .context("get_info failed")?
                .id
                .ok_or_else(|| anyhow::anyhow!("port id missing from info"))?;

            let (control_sync_client_channel, control_sync_server) = zx::Channel::create()?;
            let control_sync =
                fnetifadmin::ControlSynchronousProxy::new(control_sync_client_channel);
            device_control
                .create_interface(
                    &mut port_id,
                    control_sync_server.into(),
                    fnetifadmin::Options { name: name.clone(), ..fnetifadmin::Options::EMPTY },
                )
                .context("create_interface failed")?;

            let (control, server_end) = fnetifext::admin::Control::create_endpoints()?;
            device_control
                .create_interface(
                    &mut port_id,
                    server_end,
                    fnetifadmin::Options { name, ..fnetifadmin::Options::EMPTY },
                )
                .context("create_interface failed")?;

            (control, Mutex::new(control_sync))
        };

        let id = control_sync.lock().get_id(zx::Time::INFINITE).context("get_id failed")?;
        let _was_disabled: bool = control_sync
            .lock()
            .enable(zx::Time::INFINITE)
            .context("enable error")?
            .map_err(|e| anyhow::anyhow!("enable failed {:?}", e))?;

        let (client, server) = zx::Channel::create()?;
        connect_channel_to_protocol::<fnetstack::StackMarker>(server)?;
        let stack_sync = Mutex::new(fnetstack::StackSynchronousProxy::new(client));
        let mcast_socket = UdpSocket::bind((Ipv6Addr::LOCALHOST, 0)).context("UdpSocket::bind")?;

        Ok(TunNetworkInterface {
            tun_dev,
            tun_port,
            control,
            control_sync,
            stack_sync,
            mcast_socket,
            id,
        })
    }
}

#[async_trait]
impl NetworkInterface for TunNetworkInterface {
    fn get_index(&self) -> u64 {
        self.id
    }

    async fn outbound_packet_from_stack(&self) -> Result<Vec<u8>, Error> {
        let frame = self
            .tun_dev
            .read_frame()
            .await
            .context("FIDL error on read_frame")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("Error calling read_frame")?;

        if let Some(packet) = frame.data.as_ref() {
            fx_log_trace!(
                "TunNetworkInterface: Packet arrived from stack: {:?}",
                Ipv6PacketDebug(packet)
            );
        }

        Ok(frame.data.ok_or(format_err!("data field was absent"))?)
    }

    async fn inbound_packet_to_stack(&self, packet: &[u8]) -> Result<(), Error> {
        fx_log_trace!("TunNetworkInterface: Packet sent to stack: {:?}", Ipv6PacketDebug(packet));

        Ok(self
            .tun_dev
            .write_frame(ftun::Frame {
                port: Some(TUN_PORT_ID),
                frame_type: Some(fhwnet::FrameType::Ipv6),
                data: Some(packet.to_vec()),
                meta: None,
                ..fidl_fuchsia_net_tun::Frame::EMPTY
            })
            .await?
            .map_err(fuchsia_zircon::Status::from_raw)?)
    }

    async fn set_online(&self, online: bool) -> Result<(), Error> {
        fx_log_info!("TunNetworkInterface: Interface online: {:?}", online);

        if online {
            self.tun_port.set_online(true).await?;
            let _was_disabled: bool = self
                .control_sync
                .lock()
                .enable(zx::Time::INFINITE)
                .context("enable error")?
                .map_err(|e| anyhow::anyhow!("enable failed {:?}", e))?;
        } else {
            self.tun_port.set_online(false).await?;
        }

        Ok(())
    }

    async fn set_enabled(&self, enabled: bool) -> Result<(), Error> {
        fx_log_info!("TunNetworkInterface: Interface enabled: {:?}", enabled);
        if enabled {
            let _was_disabled: bool = self
                .control_sync
                .lock()
                .enable(zx::Time::INFINITE)
                .context("enable error")?
                .map_err(|e| anyhow::anyhow!("enable failed {:?}", e))?;
        } else {
            let _was_enabled: bool = self
                .control_sync
                .lock()
                .disable(zx::Time::INFINITE)
                .context("disable error")?
                .map_err(|e| anyhow::anyhow!("disable failed {:?}", e))?;
        }
        Ok(())
    }

    fn add_address(&self, addr: &Subnet) -> Result<(), Error> {
        fx_log_info!("TunNetworkInterface: Adding Address: {:?}", addr);
        let mut device_addr = fnet::Subnet {
            addr: fnetext::IpAddress(addr.addr.into()).into(),
            prefix_len: addr.prefix_len,
        };
        let (address_state_provider, server_end) = fidl::endpoints::create_proxy::<
            fidl_fuchsia_net_interfaces_admin::AddressStateProviderMarker,
        >()
        .expect("create proxy");
        address_state_provider.detach()?;

        self.control_sync.lock().add_address(
            &mut device_addr,
            fidl_fuchsia_net_interfaces_admin::AddressParameters::EMPTY,
            server_end,
        )?;

        let mut forwarding_entry = fnetstack::ForwardingEntry {
            subnet: fnetext::apply_subnet_mask(fnet::Subnet {
                addr: fnetext::IpAddress(addr.addr.into()).into(),
                prefix_len: addr.prefix_len,
            }),
            device_id: self.id,
            next_hop: None,
            metric: 0,
        };
        self.stack_sync
            .lock()
            .add_forwarding_entry(&mut forwarding_entry, zx::Time::INFINITE)?
            .expect("add_forwarding_entry");

        fx_log_info!("TunNetworkInterface: Successfully added address {:?}", addr);
        Ok(())
    }

    fn remove_address(&self, addr: &Subnet) -> Result<(), Error> {
        fx_log_info!("TunNetworkInterface: Removing Address: {:?}", addr);
        let mut device_addr = fnet::Subnet {
            addr: fnetext::IpAddress(addr.addr.into()).into(),
            prefix_len: addr.prefix_len,
        };
        let mut forwarding_entry = fnetstack::ForwardingEntry {
            subnet: fnetext::apply_subnet_mask(fnet::Subnet {
                addr: fnetext::IpAddress(addr.addr.into()).into(),
                prefix_len: addr.prefix_len,
            }),
            device_id: self.id,
            next_hop: None,
            metric: 0,
        };

        self.stack_sync
            .lock()
            .del_forwarding_entry(&mut forwarding_entry, zx::Time::INFINITE)
            .squash_result()?;

        self.control_sync
            .lock()
            .remove_address(&mut device_addr, zx::Time::INFINITE)?
            .expect("control_sync.remove_address");
        fx_log_info!("TunNetworkInterface: Successfully removed address {:?}", addr);
        Ok(())
    }

    fn add_external_route(&self, addr: &Subnet) -> Result<(), Error> {
        fx_log_info!("TunNetworkInterface: Adding external route: {:?} (CURRENTLY IGNORED)", addr);
        Ok(())
    }

    fn remove_external_route(&self, addr: &Subnet) -> Result<(), Error> {
        fx_log_info!(
            "TunNetworkInterface: Removing external route: {:?} (CURRENTLY IGNORED)",
            addr
        );
        Ok(())
    }

    /// Has the interface join the given multicast group.
    fn join_mcast_group(&self, addr: &std::net::Ipv6Addr) -> Result<(), Error> {
        fx_log_info!("TunNetworkInterface: Joining multicast group: {:?}", addr);
        self.mcast_socket.join_multicast_v6(addr, self.id.try_into().unwrap())?;
        Ok(())
    }

    /// Has the interface leave the given multicast group.
    fn leave_mcast_group(&self, addr: &std::net::Ipv6Addr) -> Result<(), Error> {
        fx_log_info!(
            "TunNetworkInterface: Leaving multicast group: {:?} (CURRENTLY IGNORED)",
            addr
        );
        self.mcast_socket.leave_multicast_v6(addr, self.id.try_into().unwrap())?;
        Ok(())
    }

    fn take_event_stream(&self) -> BoxStream<'_, Result<NetworkInterfaceEvent, Error>> {
        let enabled_stream = futures::stream::try_unfold((), move |()| async move {
            loop {
                if let ftun::InternalState { has_session: Some(has_session), .. } =
                    self.tun_port.watch_state().await?
                {
                    break Ok(Some((
                        NetworkInterfaceEvent::InterfaceEnabledChanged(has_session),
                        (),
                    )));
                }
            }
        });

        use fidl_fuchsia_net_interfaces::*;
        use std::convert::TryInto;

        struct EventState {
            prev_prop: Properties,
            watcher: Option<WatcherProxy>,
            next_events: Vec<NetworkInterfaceEvent>,
        }
        let init_state =
            EventState { prev_prop: Properties::EMPTY, watcher: None, next_events: Vec::default() };

        let if_event_stream = futures::stream::try_unfold(init_state, move |mut state| {
            async move {
                if state.watcher.is_none() {
                    let fnif_state = connect_to_protocol::<StateMarker>()?;
                    let (watcher, req) = create_proxy::<WatcherMarker>()?;
                    fnif_state.get_watcher(WatcherOptions::EMPTY, req)?;
                    state.watcher = Some(watcher);
                }

                loop {
                    // Flush out any pending events
                    if let Some(event) = state.next_events.pop() {
                        return Ok(Some((event, state)));
                    }

                    match state.watcher.as_ref().unwrap().watch().await? {
                        Event::Existing(prop) if prop.id == Some(self.id) => {
                            assert!(
                                state.prev_prop.id == None,
                                "Got Event::Existing twice for same interface"
                            );
                            state.prev_prop = prop;
                            continue;
                        }
                        Event::Idle(_) => {
                            if state.prev_prop.id == None {
                                return Err(format_err!("Interface no longer exists"));
                            }
                        }
                        Event::Removed(id) if id == self.id => return Ok(None),

                        Event::Changed(prop) if prop.id == Some(self.id) => {
                            assert!(state.prev_prop.id.is_some());

                            traceln!("TunNetworkInterface: Got Event::Changed({:#?})", prop);

                            if let Some(addrs) = prop.addresses.as_ref() {
                                let empty_addrs = vec![];
                                let prev_addrs =
                                    state.prev_prop.addresses.as_ref().unwrap_or(&empty_addrs);
                                state.next_events.extend(
                                    addrs.iter().filter(|x| !prev_addrs.contains(x)).filter_map(
                                        |Address { addr, valid_until: _, .. }| {
                                            addr.unwrap()
                                                .try_into()
                                                .ok()
                                                .map(NetworkInterfaceEvent::AddressWasAdded)
                                        },
                                    ),
                                );
                                state.next_events.extend(
                                    prev_addrs.iter().filter(|x| !addrs.contains(x)).filter_map(
                                        |Address { addr, valid_until: _, .. }| {
                                            addr.unwrap()
                                                .try_into()
                                                .ok()
                                                .map(NetworkInterfaceEvent::AddressWasRemoved)
                                        },
                                    ),
                                );
                            }

                            traceln!(
                                "TunNetworkInterface: Queued events: {:#?}",
                                state.next_events
                            );

                            state.prev_prop = prop;
                        }

                        _ => continue,
                    }
                }
            }
        });

        futures::stream::select(enabled_stream, if_event_stream).boxed()
    }

    async fn set_ipv6_forwarding_enabled(&self, enabled: bool) -> Result<(), Error> {
        // Ignore the configuration before our change was applied.
        let _: fnetifadmin::Configuration = self
            .control_sync
            .lock()
            .set_configuration(
                fnetifadmin::Configuration {
                    ipv6: Some(fnetifadmin::Ipv6Configuration {
                        forwarding: Some(enabled),
                        ..fnetifadmin::Ipv6Configuration::EMPTY
                    }),
                    ..fnetifadmin::Configuration::EMPTY
                },
                zx::Time::INFINITE,
            )
            .map_err(anyhow::Error::new)
            .and_then(|res| {
                res.map_err(|e: fnetifadmin::ControlSetConfigurationError| {
                    anyhow::anyhow!("{:?}", e)
                })
            })
            .context("set configuration")?;

        Ok(())
    }
}
