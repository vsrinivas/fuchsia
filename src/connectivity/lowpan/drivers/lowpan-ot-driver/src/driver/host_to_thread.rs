// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use lowpan_driver_common::spinel::*;

use anyhow::{Context as _, Error};

use futures::prelude::*;
use lowpan_driver_common::net::NetworkInterfaceEvent;
use lowpan_driver_common::spinel::Subnet;

use packet::ParsablePacket;
use packet_formats::icmp::mld::MldPacket;
use packet_formats::icmp::{IcmpParseArgs, Icmpv6Packet};
use packet_formats::ip::{IpPacket, Ipv6Proto};
use packet_formats::ipv6::Ipv6Packet;

/// Callbacks from netstack and other on-host systems.
impl<OT, NI> OtDriver<OT, NI>
where
    OT: ot::InstanceInterface + Send,
    NI: NetworkInterface,
{
    pub(crate) async fn on_regulatory_region_changed(&self, region: String) -> Result<(), Error> {
        fx_log_info!("Got region code {:?}", region);

        Ok(self.driver_state.lock().ot_instance.set_region(region.try_into()?)?)
    }

    pub(crate) async fn on_network_interface_event(
        &self,
        event: NetworkInterfaceEvent,
    ) -> Result<(), Error> {
        Ok(match event {
            NetworkInterfaceEvent::InterfaceEnabledChanged(enabled) => {
                let mut driver_state = self.driver_state.lock();

                let new_connectivity_state = if enabled {
                    driver_state.updated_connectivity_state().activated()
                } else {
                    driver_state.updated_connectivity_state().deactivated()
                };

                if new_connectivity_state != driver_state.connectivity_state {
                    let old_connectivity_state = driver_state.connectivity_state;
                    driver_state.connectivity_state = new_connectivity_state;
                    std::mem::drop(driver_state);
                    self.driver_state_change.trigger();
                    self.on_connectivity_state_change(
                        new_connectivity_state,
                        old_connectivity_state,
                    );
                }
            }
            NetworkInterfaceEvent::AddressWasAdded(x) => self.on_netstack_added_address(x).await?,
            NetworkInterfaceEvent::AddressWasRemoved(x) => {
                self.on_netstack_removed_address(x).await?
            }
            NetworkInterfaceEvent::RouteToSubnetProvided(x) => {
                self.on_netstack_added_route(x).await?
            }
            NetworkInterfaceEvent::RouteToSubnetRevoked(x) => {
                self.on_netstack_removed_route(x).await?
            }
        })
    }

    pub(crate) fn on_netstack_joined_multicast_group(
        &self,
        group: std::net::Ipv6Addr,
    ) -> Result<(), anyhow::Error> {
        let driver_state = self.driver_state.lock();

        if let Err(err) =
            driver_state.ot_instance.ip6_join_multicast_group(&group).ignore_already_exists()
        {
            fx_log_warn!("Unable to join multicast group {} on OpenThread: {:?}", group, err);
        }

        Ok(())
    }

    pub(crate) fn on_netstack_left_multicast_group(
        &self,
        group: std::net::Ipv6Addr,
    ) -> Result<(), anyhow::Error> {
        let driver_state = self.driver_state.lock();

        if let Err(err) = driver_state
            .ot_instance
            .ip6_leave_multicast_group(&group)
            .ignore_already_exists()
            .ignore_not_found()
        {
            fx_log_warn!("Unable to leave multicast group {} on OpenThread: {:?}", group, err);
        }

        Ok(())
    }

    pub(crate) async fn on_netstack_added_address(&self, subnet: Subnet) -> Result<(), Error> {
        fx_log_debug!("Netstack added address: {:?}", subnet);

        let should_skip = {
            let driver_state = self.driver_state.lock();
            let addr_entry =
                AddressTableEntry { subnet: subnet.clone(), ..AddressTableEntry::default() };
            !driver_state.is_active_and_ready() || driver_state.address_table.contains(&addr_entry)
        };

        if !should_skip {
            let netif_addr = ot::NetifAddress::new(subnet.addr, subnet.prefix_len);
            let driver_state = self.driver_state.lock();

            driver_state
                .ot_instance
                .ip6_add_unicast_address(&netif_addr)
                .ignore_already_exists()
                .or_else(move |err| {
                    fx_log_warn!(
                        "OpenThread refused to add unicast address {:?}, will remove from netstack. (Error: {:?})",
                        netif_addr,
                        err
                    );
                    self.net_if.remove_address(&subnet)
                })
                .context("on_netstack_added_address")?;
        }

        Ok(())
    }

    pub(crate) async fn on_netstack_removed_address(&self, subnet: Subnet) -> Result<(), Error> {
        fx_log_debug!("Netstack removed address: {:?}", subnet);

        let driver_state = self.driver_state.lock();

        driver_state
            .ot_instance
            .ip6_remove_unicast_address(&subnet.addr)
            .ignore_not_found()
            .context("on_netstack_added_address")
    }

    pub(crate) async fn on_netstack_added_route(&self, subnet: Subnet) -> Result<(), Error> {
        fx_log_debug!("Netstack added route: {:?} (ignored)", subnet);

        let erc = ot::ExternalRouteConfig::from_prefix(ot::Ip6Prefix::new(
            subnet.addr,
            subnet.prefix_len,
        ));

        let driver_state = self.driver_state.lock();

        driver_state
            .ot_instance
            .add_external_route(&erc)
            .ignore_already_exists()
            .context("on_netstack_added_route")
    }

    pub(crate) async fn on_netstack_removed_route(&self, subnet: Subnet) -> Result<(), Error> {
        fx_log_debug!("Netstack removed route: {:?}", subnet);

        let driver_state = self.driver_state.lock();

        driver_state
            .ot_instance
            .remove_external_route(&ot::Ip6Prefix::new(subnet.addr, subnet.prefix_len))
            .ignore_not_found()
            .context("on_netstack_removed_route")
    }

    async fn on_netstack_packet_for_thread(&self, packet: Vec<u8>) -> Result<(), Error> {
        if !self.intercept_from_host(packet.as_slice()).await {
            fx_log_debug!("Outbound packet handled internally, dropping.");
            return Ok(());
        }

        let driver_state = self.driver_state.lock();

        if driver_state.ot_instance.is_active_scan_in_progress() {
            fx_log_info!("OpenThread Send Failed: Active scan in progress.");
        } else if driver_state.ot_instance.is_energy_scan_in_progress() {
            fx_log_info!("OpenThread Send Failed: Energy scan in progress.");
        } else {
            if let Err(err) = driver_state.ot_instance.ip6_send_data(packet.as_slice()) {
                match err {
                    ot::Error::MessageDropped => {
                        fx_log_info!("OpenThread dropped a packet due to packet processing rules.");
                    }
                    ot::Error::NoRoute => {
                        fx_log_info!(
                            "OpenThread dropped a packet because there was no route to host."
                        );
                    }
                    x => {
                        fx_log_warn!("Send packet to OpenThread failed: \"{:?}\"", x);
                        fx_log_debug!(
                            "Message Buffer Info: {:?}",
                            driver_state.ot_instance.get_buffer_info()
                        );
                    }
                }
            }
        }
        Ok(())
    }
}

/// Outbound (Host-to-Thread) Traffic Handling.
impl<OT, NI> OtDriver<OT, NI>
where
    OT: ot::InstanceInterface + Send,
    NI: NetworkInterface,
{
    /// Packet pump stream that pulls packets from the network interface,
    /// processes them, and then sends them to OpenThread.
    pub(crate) fn outbound_packet_pump(
        &self,
    ) -> impl TryStream<Ok = (), Error = Error> + Send + '_ {
        futures::stream::try_unfold((), move |()| {
            self.net_if
                .outbound_packet_from_stack()
                .map(|x: Result<Vec<u8>, Error>| x.map(|x| Some((x, ()))))
        })
        .and_then(move |packet| self.on_netstack_packet_for_thread(packet))
    }

    /// Processes the given IPv6 packet from the host and determines if it should
    /// be forwarded or not.
    ///
    /// Returns true if the packet should be forwarded to the thread network, false otherwise.
    async fn intercept_from_host(&self, mut packet_bytes: &[u8]) -> bool {
        match Ipv6Packet::parse(&mut packet_bytes, ()) {
            Ok(packet) => match packet.proto() {
                Ipv6Proto::Icmpv6 => {
                    let args = IcmpParseArgs::new(packet.src_ip(), packet.dst_ip());
                    match Icmpv6Packet::parse(&mut packet_bytes, args) {
                        Ok(Icmpv6Packet::Mld(MldPacket::MulticastListenerReport(msg))) => {
                            let group =
                                std::net::Ipv6Addr::from(msg.body().group_addr.ipv6_bytes());

                            if let Err(err) = self.on_netstack_joined_multicast_group(group) {
                                warn!(
                                    "Netstack refused to join multicast group {:?}: {:?}",
                                    group, err
                                );
                            }

                            // Drop the packet
                            return false;
                        }
                        Ok(Icmpv6Packet::Mld(MldPacket::MulticastListenerDone(msg))) => {
                            let group =
                                std::net::Ipv6Addr::from(msg.body().group_addr.ipv6_bytes());

                            if let Err(err) = self.on_netstack_left_multicast_group(group) {
                                warn!(
                                    "Netstack refused to leave multicast group {:?}: {:?}",
                                    group, err
                                );
                            }

                            // Drop the packet
                            return false;
                        }
                        Ok(_) => {}
                        Err(err) => {
                            warn!("Unable to parse ICMPv6 packet from host: {:?}", err);
                        }
                    }
                }
                _ => {}
            },

            Err(err) => {
                warn!("Unable to parse IPv6 packet from host: {:?}", err);

                // Drop the packet
                return false;
            }
        }

        // Pass the packet along to OpenThread.
        true
    }
}
