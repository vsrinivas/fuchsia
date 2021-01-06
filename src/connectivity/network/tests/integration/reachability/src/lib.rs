// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
// Increase recursion limit in order to use `futures::select`.
#![recursion_limit = "256"]

use anyhow::{anyhow, Context as _};
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_neighbor as fnet_neighbor;
use fidl_fuchsia_net_stack as fnet_stack;
use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use futures::{FutureExt as _, StreamExt as _, TryFutureExt as _, TryStreamExt as _};
use net_declare::fidl_subnet;
use net_types::ip::{Ipv4, Ipv6};
use netstack_testing_common::{
    constants::{eth as eth_consts, ipv4 as ipv4_consts, ipv6 as ipv6_consts},
    environments::{Netstack2, Reachability as _, ReachabilityMonitor, TestSandboxExt as _},
    get_inspect_data, send_ra_with_router_lifetime, EthertapName as _, Result,
};
use netstack_testing_macros::variants_test;
use packet::{Buf, InnerPacketBuilder as _, Serializer as _};
use packet_formats::{
    ethernet::{EtherType, EthernetFrameBuilder},
    icmp::ndp::options::{NdpOption, PrefixInformation},
    icmp::{IcmpEchoRequest, IcmpPacketBuilder, IcmpUnusedCode, MessageBody as _},
    ip::IpProto,
    ipv4::Ipv4PacketBuilder,
    ipv6::Ipv6PacketBuilder,
    testutil::parse_icmp_packet_in_ip_packet_in_ethernet_frame,
};

const GATEWAY_ADDR: net_types::ip::Ipv4Addr = net_types::ip::Ipv4Addr::new([192, 168, 0, 1]);

/// Try to parse `frame` as an ICMP or ICMPv6 Echo Request message, and if successful returns the
/// Echo Reply message that the netstack would expect as a reply.
fn reply_if_echo_request(frame: Vec<u8>, gateway_only: bool) -> Result<Option<Buf<Vec<u8>>>> {
    let mut icmp_body = Vec::new();
    let r = parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv4, _, IcmpEchoRequest, _>(
        &frame,
        |p| {
            icmp_body.extend(p.body().bytes());
        },
    );
    match r {
        Ok((src_mac, dst_mac, src_ip, dst_ip, _ttl, message, _code)) => {
            if gateway_only && dst_ip != GATEWAY_ADDR {
                return Ok(None);
            }
            return Ok(Some(
                icmp_body
                    .into_serializer()
                    .encapsulate(IcmpPacketBuilder::<Ipv4, &[u8], _>::new(
                        dst_ip,
                        src_ip,
                        IcmpUnusedCode,
                        message.reply(),
                    ))
                    .encapsulate(Ipv4PacketBuilder::new(
                        dst_ip,
                        src_ip,
                        ipv4_consts::DEFAULT_TTL,
                        IpProto::Icmp,
                    ))
                    .encapsulate(EthernetFrameBuilder::new(dst_mac, src_mac, EtherType::Ipv4))
                    .serialize_vec_outer()
                    .map_err(|e| anyhow!("failed to serialize ICMP packet: {:?}", e))?
                    .unwrap_b(),
            ));
        }
        Err(packet_formats::error::IpParseError::Parse {
            error: packet_formats::error::ParseError::NotExpected,
        }) => {}
        Err(e) => {
            return Err(e.into());
        }
    }

    let mut icmp_body = Vec::new();
    let r = parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv6, _, IcmpEchoRequest, _>(
        &frame,
        |p| {
            icmp_body.extend(p.body().bytes());
        },
    );
    match r {
        Ok((src_mac, dst_mac, src_ip, dst_ip, _ttl, message, _code)) => {
            if gateway_only && dst_ip != ipv6_consts::LINK_LOCAL_ADDR {
                return Ok(None);
            }
            return Ok(Some(
                icmp_body
                    .into_serializer()
                    .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                        dst_ip,
                        src_ip,
                        IcmpUnusedCode,
                        message.reply(),
                    ))
                    .encapsulate(Ipv6PacketBuilder::new(
                        dst_ip,
                        src_ip,
                        ipv6_consts::DEFAULT_HOP_LIMIT,
                        IpProto::Icmpv6,
                    ))
                    .encapsulate(EthernetFrameBuilder::new(dst_mac, src_mac, EtherType::Ipv6))
                    .serialize_vec_outer()
                    .map_err(|e| anyhow::anyhow!("failed to serialize ICMPv6 packet: {:?}", e))?
                    .unwrap_b(),
            ));
        }
        Err(packet_formats::error::IpParseError::Parse {
            error: packet_formats::error::ParseError::NotExpected,
        }) => {}
        Err(e) => {
            return Err(e.into());
        }
    }
    Ok(None)
}

/// Extract the most recent reachability states for IPv4 and IPv6 from the inspect data.
fn extract_reachability_states(
    data: &diagnostics_hierarchy::DiagnosticsHierarchy,
) -> Result<(String, String)> {
    let (v4, v6) = data
        .children
        .get(0)
        .ok_or_else(|| anyhow!("failed to find \"system/root\" subtree in inspect data"))?
        .children
        .iter()
        .fold((None, None), |(mut v4, mut v6), info| {
            let get_latest_state = |states: &diagnostics_hierarchy::DiagnosticsHierarchy| {
                states.children.iter().fold((-1, None), |(latest_seqnum, latest_state), state| {
                    let seqnum = state
                        .name
                        .parse::<i64>()
                        .expect("failed to parse reachability state sequence number as integer");
                    if seqnum > latest_seqnum {
                        (
                            seqnum,
                            state.properties.iter().find_map(|p| {
                                if p.key() == "state" {
                                    p.string().map(|s| s.to_owned())
                                } else {
                                    None
                                }
                            }),
                        )
                    } else {
                        (latest_seqnum, latest_state)
                    }
                })
            };
            if info.name == "IPv4" {
                let (_, v4_latest) = get_latest_state(info);
                v4 = v4_latest;
            } else if info.name == "IPv6" {
                let (_, v6_latest) = get_latest_state(info);
                v6 = v6_latest;
            }
            (v4, v6)
        });
    let v4 = v4.ok_or_else(|| anyhow!("failed to find IPv4 reachability state in inspect data"))?;
    let v6 = v6.ok_or_else(|| anyhow!("failed to find IPv6 reachability state in inspect data"))?;
    Ok((v4, v6))
}

async fn test_reachability_monitor_state<E, F>(
    name: &str,
    gateway_only: bool,
    mut pred: F,
) -> Result
where
    E: netemul::Endpoint,
    F: FnMut(&str, &str) -> Result<bool>,
{
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let network = sandbox.create_network(name).await.context("failed to create network")?;
    let env = sandbox
        .create_netstack_environment_with::<Netstack2, _, _>(name, &[])
        .context("failed to create environment")?;
    let fake_ep = network.create_fake_endpoint().context("failed to create fake endpoint")?;

    const SUBNET: fnet::Subnet = fidl_subnet!("192.168.0.2/24");
    let iface = env
        .join_network::<E, _>(
            &network,
            name.ethertap_compatible_name(),
            &netemul::InterfaceConfig::StaticIp(SUBNET),
        )
        .await
        .context("failed to join network with netdevice endpoint")?;

    // Start reachability monitor.
    let launcher = env.get_launcher().context("get launcher")?;
    let mut reachability = fuchsia_component::client::launch(
        &launcher,
        ReachabilityMonitor::PKG_URL.to_string(),
        None,
    )
    .context("failed to launch reachability monitor")?;

    // Add neighbor table entries for the gateway addresses.
    let controller = env
        .connect_to_service::<fnet_neighbor::ControllerMarker>()
        .context("failed to connect to Controller")?;
    let () = controller
        .add_entry(
            iface.id(),
            &mut fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                addr: ipv6_consts::LINK_LOCAL_ADDR.ipv6_bytes(),
            }),
            &mut fnet::MacAddress { octets: eth_consts::MAC_ADDR.bytes() },
        )
        .await
        .context("add_entry FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("add IPv6 gateway neighbor table entry failed")?;
    let () = controller
        .add_entry(
            iface.id(),
            &mut fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: GATEWAY_ADDR.ipv4_bytes() }),
            &mut fnet::MacAddress { octets: eth_consts::MAC_ADDR.bytes() },
        )
        .await
        .context("add_entry FIDL error")?
        .map_err(fuchsia_zircon::Status::from_raw)
        .context("add IPv4 gateway neighbor table entry failed")?;

    // Add an IPv4 default route.
    let stack = env
        .connect_to_service::<fnet_stack::StackMarker>()
        .context("failed to connect to Netstack")?;
    let () = stack
        .add_forwarding_entry(&mut fnet_stack::ForwardingEntry {
            subnet: fidl_subnet!("0.0.0.0/0"),
            destination: fnet_stack::ForwardingDestination::NextHop(fnet::IpAddress::Ipv4(
                fnet::Ipv4Address { addr: GATEWAY_ADDR.ipv4_bytes() },
            )),
        })
        .await
        .squash_result()
        .context("failed to add default IPv4 route")?;

    // Send a router advertisement message.
    let pi = PrefixInformation::new(
        ipv6_consts::PREFIX.prefix(),  /* prefix_length */
        true,                          /* on_link_flag */
        false,                         /* autonomous_address_configuration_flag */
        1000,                          /* valid_lifetime */
        0,                             /* preferred_lifetime */
        ipv6_consts::PREFIX.network(), /* prefix */
    );
    let options = [NdpOption::PrefixInformation(&pi)];
    let () = send_ra_with_router_lifetime(&fake_ep, 1000, &options)
        .await
        .context("failed to send router advertisement")?;

    let echo_reply_stream = fake_ep
        .frame_stream()
        .map(|r| r.context("fake endpoint frame stream error"))
        .try_filter_map(|(frame, dropped)| {
            assert_eq!(dropped, 0);
            async {
                let reply = match reply_if_echo_request(frame, gateway_only)
                    .context("failed to handle frame")?
                {
                    Some(reply) => reply,
                    None => return Ok(None),
                };
                fake_ep
                    .write(reply.as_ref())
                    .await
                    .map(Some)
                    .context("failed to write echo reply to fake endpoint")
            }
        })
        .fuse();
    futures::pin_mut!(echo_reply_stream);

    // Verify through the inspect data that the reachability states are as expected.
    // TODO(fxbug.dev/65585) Get reachability monitor's reachability state over FIDL rather than
    // the inspect data.
    const INSPECT_COMPONENT: &str = "reachability.cmx";
    const INSPECT_TREE_SELECTOR: &str = "root/system";
    let inspect_data_stream = futures::stream::try_unfold((), |()| {
        get_inspect_data(&env, INSPECT_COMPONENT, INSPECT_TREE_SELECTOR, "")
            .and_then(|data| {
                futures::future::ready(extract_reachability_states(&data).with_context(|| {
                    format!("failed to extract reachability states from inspect data: {:#?}", data)
                }))
            })
            .map_ok(|states| Some((states, ())))
    })
    .fuse();
    let mut reachability_monitor_wait_fut = reachability.wait().fuse();
    futures::pin_mut!(inspect_data_stream);

    // Ensure that at least one echo request has been replied to before polling the inspect data
    // stream to guarantee that reachability monitor has initialized its inspect data tree.
    let () = echo_reply_stream
        .try_next()
        .await
        .context("echo reply stream error")?
        .ok_or_else(|| anyhow!("echo reply stream ended unexpectedly"))?;
    loop {
        futures::select! {
            r = echo_reply_stream.try_next() => {
                let () = r.context("echo reply stream error")?
                    .ok_or_else(|| anyhow!("echo reply stream ended unexpectedly"))?;
            }
            r = inspect_data_stream.try_next() => {
                let (v4, v6) = r.context("inspect data stream error")?
                    .ok_or_else(|| anyhow!("inspect data stream ended unexpectedly"))?;
                if pred(&v4, &v6)? {
                    return Ok(());
                }
            }
            r = reachability_monitor_wait_fut => {
                let exit_status =
                    r.context("error while waiting for reachability monitor to exit")?;
                return Err(
                    anyhow!("reachability monitor terminated unexpectedly with: {}", exit_status)
                );
            }
        }
    }
}

const STATE_INTERNET: &str = "Internet";
const STATE_GATEWAY: &str = "Gateway";

/// Test that reachability monitor detects that the internet is reachable.
#[variants_test]
async fn internet_reachable<E: netemul::Endpoint>(name: &str) -> Result {
    test_reachability_monitor_state::<E, _>(name, false, |v4, v6| {
        Ok(v4 == STATE_INTERNET && v6 == STATE_INTERNET)
    })
    .await
}

/// Test that reachability monitor detects that the gateway is reachable (but the wider internet is
/// not).
#[variants_test]
async fn gateway_reachable<E: netemul::Endpoint>(name: &str) -> Result {
    test_reachability_monitor_state::<E, _>(name, true, |v4, v6| {
        if v4 == STATE_INTERNET || v6 == STATE_INTERNET {
            Err(anyhow!("IPv4={} IPv6={} reached Internet unexpectedly"))
        } else {
            Ok(v4 == STATE_GATEWAY && v6 == STATE_GATEWAY)
        }
    })
    .await
}
