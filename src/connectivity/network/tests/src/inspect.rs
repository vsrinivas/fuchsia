// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::environments::{Netstack2, TestSandboxExt};
use crate::Result;
use anyhow::Context as _;
use fidl_fuchsia_net_interfaces_ext::CloneExt as _;
use fuchsia_inspect_node_hierarchy::Property;
use net_declare::{fidl_mac, fidl_subnet};
use netemul::Endpoint as _;

/// A helper type to provide address verification in inspect NIC data.
///
/// Address matcher implements `PropertyAssertion` in a stateful manner. It
/// expects all addresses in its internal set to be consumed as part of property
/// matching.
#[derive(Clone)]
struct AddressMatcher {
    set: std::rc::Rc<std::cell::RefCell<std::collections::HashSet<String>>>,
}

impl AddressMatcher {
    /// Creates an `AddressMatcher` from interface properties.
    fn new(props: &fidl_fuchsia_net_interfaces::Properties, has_hwaddr: bool) -> Self {
        let set = props
            .addresses
            .iter()
            .flatten()
            .filter_map(|a| {
                a.addr.map(|a| {
                    let prefix = match a.addr {
                        fidl_fuchsia_net::IpAddress::Ipv4(_) => "ipv4",
                        fidl_fuchsia_net::IpAddress::Ipv6(_) => "ipv6",
                    };
                    format!("[{}] {}", prefix, fidl_fuchsia_net_ext::Subnet::from(a))
                })
            })
            .chain(if has_hwaddr {
                // If we have a non-empty hardware address, assume that we're going to
                // have the fixed ARP protocol address in the set.
                Some("[arp] 617270/0".to_string())
            } else {
                None
            })
            .collect::<std::collections::HashSet<_>>();

        Self { set: std::rc::Rc::new(std::cell::RefCell::new(set)) }
    }

    /// Checks that the internal set has been entirely consumed.
    ///
    /// Empties the internal set on return. Subsequent calls to check will
    /// always succeed.
    fn check(&self) -> Result<()> {
        let set = self.set.replace(Default::default());
        if set.is_empty() {
            Ok(())
        } else {
            Err(anyhow::anyhow!("unseen addresses left in set: {:?}", set))
        }
    }
}

impl std::ops::Drop for AddressMatcher {
    fn drop(&mut self) {
        // Always check for left over addresses on drop. Prevents the caller
        // from forgetting to do so.
        let () = self.check().expect("AddressMatcher was not emptied");
    }
}

impl fuchsia_inspect::testing::PropertyAssertion for AddressMatcher {
    fn run(&self, actual: &Property<String>) -> Result<()> {
        let actual = actual.string().ok_or_else(|| {
            anyhow::anyhow!("invalid property {:#?} for AddressMatcher, want String", actual)
        })?;
        if self.set.borrow_mut().remove(actual) {
            Ok(())
        } else {
            Err(anyhow::anyhow!("{} not in expected address set", actual))
        }
    }
}

/// Gets inspect data in environment.
///
/// Returns the resulting inspect data for `component`, filtered by
/// `tree_selector` and with inspect file starting with `file_prefix`.
async fn get_inspect_data<'a>(
    env: &netemul::TestEnvironment<'a>,
    component: impl Into<String>,
    tree_selector: impl Into<String>,
    file_prefix: &str,
) -> Result<fuchsia_inspect_node_hierarchy::NodeHierarchy> {
    let archive = env
        .connect_to_service::<fidl_fuchsia_diagnostics::ArchiveAccessorMarker>()
        .context("failed to connect to archive accessor")?;

    fuchsia_inspect_contrib::reader::ArchiveReader::new()
        .with_archive(archive)
        .add_selector(
            fuchsia_inspect_contrib::reader::ComponentSelector::new(vec![component.into()])
                .with_tree_selector(tree_selector.into()),
        )
        .retry_if_empty(false)
        .get()
        .await
        .context("failed to get inspect data")?
        .into_iter()
        .find_map(
            |diagnostics_data::InspectData {
                 data_source: _,
                 metadata,
                 moniker: _,
                 payload,
                 version: _,
             }| {
                if metadata.filename.starts_with(file_prefix) {
                    Some(payload)
                } else {
                    None
                }
            },
        )
        .ok_or_else(|| anyhow::anyhow!("failed to find inspect data"))?
        .ok_or_else(|| anyhow::anyhow!("empty inspect payload"))
}

#[fuchsia_async::run_singlethreaded(test)]
async fn inspect_nic() -> Result {
    // The number of IPv6 addresses that the stack will assign to an interface.
    //
    // When an interface comes up, the stack will assign two link-local IPv6
    // addresses to it.
    const EXPECTED_NUM_IPV6_ADDRESSES: usize = 2;

    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let network = sandbox.create_network("net").await.context("failed to create network")?;
    let env = sandbox
        .create_netstack_environment::<Netstack2, _>("inspect_objects")
        .context("failed to create environment")?;

    const ETH_MAC: fidl_fuchsia_net::MacAddress = fidl_mac!("02:01:02:03:04:05");
    const NETDEV_MAC: fidl_fuchsia_net::MacAddress = fidl_mac!("02:0A:0B:0C:0D:0E");

    let eth = env
        .join_network_with(
            &network,
            "eth-ep",
            netemul::Ethernet::make_config(netemul::DEFAULT_MTU, Some(ETH_MAC)),
            netemul::InterfaceConfig::StaticIp(fidl_subnet!(192.168.0.1/24)),
        )
        .await
        .context("failed to join network with ethernet endpoint")?;
    let netdev = env
        .join_network_with(
            &network,
            "netdev-ep",
            netemul::NetworkDevice::make_config(netemul::DEFAULT_MTU, Some(NETDEV_MAC)),
            netemul::InterfaceConfig::StaticIp(fidl_subnet!(192.168.0.2/24)),
        )
        .await
        .context("failed to join network with netdevice endpoint")?;

    let interfaces_state = env
        .connect_to_service::<fidl_fuchsia_net_interfaces::StateMarker>()
        .context("failed to connect to fuchsia.net.interfaces/State")?;

    // Wait for the world to stabilize and capture the state to verify inspect
    // data.
    let (loopback_props, netdev_props, eth_props) =
        fidl_fuchsia_net_interfaces_ext::wait_interface(
            fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interfaces_state)?,
            &mut std::collections::HashMap::new(),
            |if_map| {
                let loopback = if_map.iter().find_map(|(_, properties)| {
                    match properties.device_class.as_ref()? {
                        fidl_fuchsia_net_interfaces::DeviceClass::Loopback(
                            fidl_fuchsia_net_interfaces::Empty {},
                        ) => Some(properties.clone()),
                        fidl_fuchsia_net_interfaces::DeviceClass::Device(_) => None,
                    }
                })?;
                // Endpoint is up, has assigned IPv4 and at least the expected number of
                // IPv6 addresses.
                let get_properties = |id| {
                    let properties = if_map.get(&id)?;
                    let addresses = properties.addresses.as_ref()?;
                    if !properties.online? {
                        return None;
                    }
                    let (v4_count, v6_count) =
                        addresses.iter().fold((0, 0), |(v4_count, v6_count), a| match a.addr {
                            Some(fidl_fuchsia_net::Subnet {
                                addr: fidl_fuchsia_net::IpAddress::Ipv4(_),
                                prefix_len: _,
                            }) => (v4_count + 1, v6_count),
                            Some(fidl_fuchsia_net::Subnet {
                                addr: fidl_fuchsia_net::IpAddress::Ipv6(_),
                                prefix_len: _,
                            }) => (v4_count, v6_count + 1),
                            None => (v4_count, v6_count),
                        });
                    if v4_count > 0 && v6_count >= EXPECTED_NUM_IPV6_ADDRESSES {
                        Some(properties.clone())
                    } else {
                        None
                    }
                };
                Some((loopback, get_properties(netdev.id())?, get_properties(eth.id())?))
            },
        )
        .await
        .context("failed to wait for interfaces up and addresses configured")?;
    let loopback_id = loopback_props.id.ok_or(anyhow::anyhow!("loopback ID missing"))?;
    let loopback_addrs = AddressMatcher::new(&loopback_props, false);
    let netdev_addrs = AddressMatcher::new(&netdev_props, true);
    let eth_addrs = AddressMatcher::new(&eth_props, true);

    let data = get_inspect_data(&env, "netstack-debug.cmx", "NICs", "interfaces")
        .await
        .context("get_inspect_data failed")?;
    // Debug print the tree to make debugging easier in case of failures.
    println!("Got inspect data: {:#?}", data);
    use fuchsia_inspect::testing::AnyProperty;
    fuchsia_inspect::assert_inspect_tree!(data, NICs: {
        loopback_id.to_string() => {
            Name: loopback_props.name.ok_or(anyhow::anyhow!("loopback name missing"))?,
            Loopback: "true",
            LinkOnline: "true",
            AdminUp: "true",
            Promiscuous: "false",
            Up: "true",
            MTU: 65536u64,
            NICID: loopback_id.to_string(),
            Running: "true",
            "DHCP enabled": "false",
            ProtocolAddress0: loopback_addrs.clone(),
            ProtocolAddress1: loopback_addrs.clone(),
            Stats: {
                DisabledRx: {
                    Bytes: 0u64,
                    Packets: 0u64,
                },
                Tx: {
                   Bytes: 0u64,
                   Packets: 0u64,
                },
                Rx: {
                    Bytes: 0u64,
                    Packets: 0u64,
                }
            }
        },
        eth.id().to_string() => {
            Name: eth_props.name.ok_or(anyhow::anyhow!("eth name missing"))?,
            Loopback: "false",
            LinkOnline: "true",
            AdminUp: "true",
            Promiscuous: "false",
            Up: "true",
            MTU: u64::from(netemul::DEFAULT_MTU),
            NICID: eth.id().to_string(),
            Running: "true",
            "DHCP enabled": "false",
            LinkAddress: fidl_fuchsia_net_ext::MacAddress::from(ETH_MAC).to_string(),
            // NB: The interface has 4 addresses: IPv4, 2x link-local IPv6, and
            // the ARP protocol address that is always reported.
            ProtocolAddress0: eth_addrs.clone(),
            ProtocolAddress1: eth_addrs.clone(),
            ProtocolAddress2: eth_addrs.clone(),
            ProtocolAddress3: eth_addrs.clone(),
            Stats: {
                DisabledRx: {
                    Bytes: AnyProperty,
                    Packets: AnyProperty,
                },
                Tx: {
                   Bytes: AnyProperty,
                   Packets: AnyProperty,
                },
                Rx: {
                    Bytes: AnyProperty,
                    Packets: AnyProperty,
                }
            },
            "Ethernet Info": {
                Filepath: "",
                Topopath: "eth-ep",
                Features: "Synthetic",
                TxDrops: AnyProperty,
                RxReads: contains {},
                RxWrites: contains {},
                TxReads: contains {},
                TxWrites: contains {}
            }
        },
        netdev.id().to_string() => {
            Name: netdev_props.name.ok_or(anyhow::anyhow!("netdev name missing"))?,
            Loopback: "false",
            LinkOnline: "true",
            AdminUp: "true",
            Promiscuous: "false",
            Up: "true",
            MTU: u64::from(netemul::DEFAULT_MTU),
            NICID: netdev.id().to_string(),
            Running: "true",
            "DHCP enabled": "false",
            LinkAddress: fidl_fuchsia_net_ext::MacAddress::from(NETDEV_MAC).to_string(),
            // NB: The interface has 4 addresses: IPv4, 2x link-local IPv6, and
            // the ARP protocol address that is always reported.
            ProtocolAddress0: netdev_addrs.clone(),
            ProtocolAddress1: netdev_addrs.clone(),
            ProtocolAddress2: netdev_addrs.clone(),
            ProtocolAddress3: netdev_addrs.clone(),
            Stats: {
                DisabledRx: {
                    Bytes: AnyProperty,
                    Packets: AnyProperty,
                },
                Tx: {
                   Bytes: AnyProperty,
                   Packets: AnyProperty,
                },
                Rx: {
                    Bytes: AnyProperty,
                    Packets: AnyProperty,
                }
            },
            "Network Device Info": {
                TxDrops: AnyProperty,
                Class: "Unknown",
                RxReads: contains {},
                RxWrites: contains {},
                TxReads: contains {},
                TxWrites: contains {}
            }
        }
    });

    let () = loopback_addrs.check().context("loopback addresses match failed")?;
    let () = eth_addrs.check().context("ethernet addresses match failed")?;
    let () = netdev_addrs.check().context("netdev addresses match failed")?;

    Ok(())
}
