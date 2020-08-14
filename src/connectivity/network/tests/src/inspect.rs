// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::environments::{Netstack2, TestSandboxExt};
use crate::Result;
use anyhow::Context as _;
use fuchsia_inspect_node_hierarchy::Property;
use futures::TryStreamExt as _;
use net_declare::fidl_ip;
use std::convert::TryInto as _;

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
    fn new(props: &fidl_fuchsia_netstack::NetInterface) -> Self {
        // NB: We naively count ones in the netmask to get the prefix. The
        // assumption being that netmask is well formed.
        let pfx_len: u8 = match &props.netmask {
            fidl_fuchsia_net::IpAddress::Ipv4(a) => &a.addr[..],
            fidl_fuchsia_net::IpAddress::Ipv6(a) => &a.addr[..],
        }
        .iter()
        .map(|b| b.count_ones())
        .sum::<u32>()
        .try_into()
        .expect("invalid prefix length");

        let set =
            std::iter::once(&fidl_fuchsia_net::Subnet { addr: props.addr, prefix_len: pfx_len })
                .chain(props.ipv6addrs.iter())
                .map(|addr| {
                    let prefix = match addr.addr {
                        fidl_fuchsia_net::IpAddress::Ipv4(_) => "ipv4",
                        fidl_fuchsia_net::IpAddress::Ipv6(_) => "ipv6",
                    };
                    format!("[{}] {}", prefix, fidl_fuchsia_net_ext::Subnet::from(*addr))
                })
                .chain(if props.hwaddr.is_empty() {
                    None
                } else {
                    // If we have a non-empty hardware address, assume that we're going to
                    // have the fixed ARP protocol address in the set.
                    Some("[arp] 617270/0".to_string())
                })
                .collect();

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
    let sandbox = netemul::TestSandbox::new().context("failed to create sandbox")?;
    let network = sandbox.create_network("net").await.context("failed to create network")?;
    let env = sandbox
        .create_netstack_environment::<Netstack2, _>("inspect_objects")
        .context("failed to create environment")?;

    let eth = env
        .join_network::<netemul::Ethernet, _>(
            &network,
            "eth-ep",
            netemul::InterfaceConfig::StaticIp(fidl_fuchsia_net::Subnet {
                addr: fidl_ip!(192.168.0.1),
                prefix_len: 24,
            }),
        )
        .await
        .context("failed to join network with ethernet endpoint")?;

    let netstack = env
        .connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>()
        .context("failed to connect to netstack")?;

    // Wait for the world to stabilize and capture the state to verify inspect
    // data.
    let (loopback_id, mut ifaces) = netstack
        .take_event_stream()
        .try_filter_map(
            |fidl_fuchsia_netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
                let interfaces_by_id = interfaces
                    .into_iter()
                    .map(|i| (u64::from(i.id), i))
                    .collect::<std::collections::HashMap<_, _>>();
                // Find the loopback interface.
                let loopback = if let Some((id, _)) = interfaces_by_id.iter().find(|(_, iface)| {
                    iface.features & fidl_fuchsia_hardware_ethernet::INFO_FEATURE_LOOPBACK != 0
                }) {
                    *id
                } else {
                    return futures::future::ok(None);
                };

                // Verify installed interface state.
                let ready = interfaces_by_id
                    .get(&eth.id())
                    .map(|iface| {
                        // eth endpoint is up, has assigned IPv4 and IPv6
                        // addresses.
                        iface.flags & fidl_fuchsia_netstack::NET_INTERFACE_FLAG_UP != 0
                            && iface.addr != fidl_ip!(0.0.0.0)
                            && iface.ipv6addrs.len() != 0
                    })
                    .unwrap_or(false);
                futures::future::ok(if ready { Some((loopback, interfaces_by_id)) } else { None })
            },
        )
        .try_next()
        .await
        .context("failed to observe desired final state")?
        .ok_or_else(|| anyhow::anyhow!("netstack stream ended unexpectedly"))?;

    let loopback_props = ifaces.remove(&loopback_id).expect("loopback missing");
    let eth_props = ifaces.remove(&eth.id()).expect("eth missing");
    let loopback_id = format!("{}", loopback_id);
    let eth_id = format!("{}", eth_props.id);
    let eth_mac = format!(
        "{}",
        fidl_fuchsia_net_ext::MacAddress {
            octets: (&eth_props.hwaddr[..])
                .try_into()
                .context("invalid hardware address length")?
        }
    );
    let loopback_addrs = AddressMatcher::new(&loopback_props);
    let eth_addrs = AddressMatcher::new(&eth_props);

    let data = get_inspect_data(&env, "netstack-debug.cmx", "NICs", "interfaces")
        .await
        .context("get_inspect_data failed")?;

    // Debug print the tree to make debugging easier in case of failures.
    println!("Got inspect data: {:#?}", data);
    use fuchsia_inspect::testing::AnyProperty;
    fuchsia_inspect::assert_inspect_tree!(data, NICs: contains {
        loopback_id.clone() => {
            Name: loopback_props.name,
            Loopback: "true",
            LinkOnline: "true",
            AdminUp: "true",
            Promiscuous: "false",
            Up: "true",
            MTU: 65536u64,
            NICID: loopback_id,
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
        eth_id.clone() => {
            Name: eth_props.name,
            Loopback: "false",
            LinkOnline: "true",
            AdminUp: "true",
            Promiscuous: "false",
            Up: "true",
            MTU: u64::from(netemul::DEFAULT_MTU),
            NICID: eth_id,
            Running: "true",
            "DHCP enabled": "false",
            LinkAddress: eth_mac,
            // NB: The interface has 3 addresses: IPv4, IPv6, and the ARP
            // protocol address that is always reported.
            ProtocolAddress0: eth_addrs.clone(),
            ProtocolAddress1: eth_addrs.clone(),
            ProtocolAddress2: eth_addrs.clone(),
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
        }
    });

    let () = loopback_addrs.check().context("loopback addresses match failed")?;
    let () = eth_addrs.check().context("ethernet addresses match failed")?;

    Ok(())
}
