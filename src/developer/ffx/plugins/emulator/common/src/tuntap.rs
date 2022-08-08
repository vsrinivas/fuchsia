// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Result};
use cfg_if::cfg_if;
use mockall::automock;
use serde::Deserialize;
use serde_json::from_str;
use std::{process::Command, str::from_utf8};

// TODO(fxbug.dev/100022): Make this configurable.
// The interface name, "qemu", provided here used to be provided by the qemu
// executable when it was left for qemu to start the upscript. Since we need
// to time the sudo prompt and the socket creation, we now launch the script
// ourselves, so we also have to provide the interface name. This will likely
// need to eventually be configurable to support running emulators on
// multiple tap interfaces.
pub const TAP_INTERFACE_NAME: &'static str = "qemu";

// The following data structures are derived from the output of the "ip" command.
// In both cases, there is additional data available in the output; but we don't
// have a use for those fields at this time, so they are ignored/excluded during
// deserialization.
#[derive(Deserialize, Debug, Default, Eq, PartialEq)]
pub(crate) struct LinkInfo {
    info_kind: String,
}
#[derive(Deserialize, Debug, Eq, PartialEq)]
pub(crate) struct IpInterface {
    ifname: String,
    flags: Vec<String>,
    operstate: String,
    #[serde(default)]
    linkinfo: LinkInfo,
}

pub(crate) mod tap {
    use super::*;

    #[derive(Clone, Default)]
    pub(crate) struct QemuTunTap {}

    #[automock]
    impl QemuTunTap {
        // These functions are not "dead", but because of the automocking and cfg_if use below,
        // the compiler doesn't realize the non-mocked versions are being used by available.
        #[allow(dead_code)]
        pub(crate) fn interface_is_up(&self, interface: &IpInterface) -> bool {
            interface.flags.contains(&String::from("UP"))
        }

        #[allow(dead_code)]
        pub(crate) fn interface_is_tap(&self, interface: &IpInterface) -> bool {
            interface.linkinfo.info_kind == String::from("tun")
        }

        #[allow(dead_code)]
        pub(crate) fn interface_is_in_use(&self, interface: &IpInterface) -> bool {
            interface.operstate == String::from("UP")
        }

        #[allow(dead_code)]
        pub(crate) fn get_interface_details(&self) -> Result<Vec<u8>> {
            Ok(Command::new("ip").args(["--json", "--details", "link", "show"]).output()?.stdout)
        }

        #[allow(dead_code)]
        pub(crate) fn host_is_mac(&self) -> bool {
            crate::host_is_mac()
        }

        #[allow(dead_code)]
        pub(crate) fn parse_interface_from_details(&self, output: Vec<u8>) -> Result<IpInterface> {
            // Output contains a list of interfaces.
            let text = from_utf8(&output)?;
            let mut interfaces: Vec<IpInterface> = from_str(text)?;

            if let Some(position) =
                interfaces.iter().position(|i| i.ifname == String::from(TAP_INTERFACE_NAME))
            {
                Ok(interfaces.remove(position))
            } else {
                bail!(format!(
                    "Couldn't find an interface named '{}'. \
                    Configure Tun/Tap on your host or try --net user.\n\
                    To use emu with Tun/Tap networking on Linux, run:\n    \
                        sudo ip tuntap add dev {} mode tap user $USER && sudo ip link set {} up",
                    TAP_INTERFACE_NAME, TAP_INTERFACE_NAME, TAP_INTERFACE_NAME
                ))
            }
        }
    }
}

cfg_if! {
    if #[cfg(test)] {
        use self::tap::MockQemuTunTap as QemuTunTap;
    } else {
        use self::tap::QemuTunTap;
    }
}

/// A utility function for testing if a Tap interface is configured. Assumes the existence
/// of the "ip" program for finding the interface, which is usually preinstalled on Linux hosts
/// but not MacOS hosts. Conservatively assumes any error indicates Tap is unavailable.
pub fn tap_available() -> Result<()> {
    let tap = QemuTunTap::default();
    tap_inner(&tap, false)
}

/// A utility function for testing is the Tap interface is available and ready for use. Same
/// assumptions as tap_available(), but also ensures that the interface is UP and not in use by
/// another process.
pub fn tap_ready() -> Result<()> {
    let tap = QemuTunTap::default();
    tap_inner(&tap, true)
}

fn tap_inner(tap: &QemuTunTap, check_for_ready: bool) -> Result<()> {
    // Mac's don't include the "ip" program by default.
    if tap.host_is_mac() {
        bail!("Tun/Tap isn't supported on MacOS.")
    }

    // Make sure we have an interface named TAP_INTERFACE_NAME.
    let interface = tap.parse_interface_from_details(tap.get_interface_details()?)?;

    // It's there, now make sure it's tap.
    if !tap.interface_is_tap(&interface) {
        bail!(format!(
            "The '{}' interface exists, but it's not a Tun/Tap interface. Type is '{}'",
            TAP_INTERFACE_NAME, interface.linkinfo.info_kind
        ))
    }

    // We don't check for ready when resolving --net auto, only during validation.
    if check_for_ready {
        if !tap.interface_is_up(&interface) {
            bail!(format!(
                "The Tun/Tap interface '{}' exists, but it's currently DOWN.\n\
                To bring it up, you can run:\n    \
                    sudo ip link set {} up",
                TAP_INTERFACE_NAME, TAP_INTERFACE_NAME
            ))
        }
    }

    // Also check for busy-ness.
    if tap.interface_is_in_use(&interface) {
        bail!(format!(
            "The Tun/Tap interface '{}' exists, but it's in use by another process.",
            TAP_INTERFACE_NAME,
        ))
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    const INVALID: &'static str = "Something not json";

    const LOOPBACK: &'static str = r#"{
        "ifindex": 1,
        "ifname": "qemu",
        "flags": [ "LOOPBACK","UP","LOWER_UP" ],
        "mtu": 65536,
        "qdisc": "noqueue",
        "operstate": "UNKNOWN",
        "linkmode": "DEFAULT",
        "group": "default",
        "txqlen": 1000,
        "link_type": "loopback",
        "address": "00:00:00:00:00:00",
        "broadcast": "00:00:00:00:00:00",
        "promiscuity": 0,
        "min_mtu": 0,
        "max_mtu": 0,
        "inet6_addr_gen_mode": "eui64",
        "num_tx_queues": 1,
        "num_rx_queues": 1,
        "gso_max_size": 65536,
        "gso_max_segs": 65535
    }"#;
    fn loopback() -> IpInterface {
        IpInterface {
            ifname: String::from("qemu"),
            flags: vec![String::from("LOOPBACK"), String::from("UP"), String::from("LOWER_UP")],
            operstate: String::from("UNKNOWN"),
            linkinfo: LinkInfo { info_kind: String::default() },
        }
    }

    const TUN_UP: &'static str = r#"{
        "ifindex": 12,
        "ifname": "qemu",
        "flags": [ "NO-CARRIER","BROADCAST","MULTICAST","UP" ],
        "mtu": 1500,
        "qdisc": "fq_codel",
        "operstate": "DOWN",
        "linkmode": "DEFAULT",
        "group": "default",
        "txqlen": 1000,
        "link_type": "ether",
        "address": "55:55:55:55:55:55",
        "broadcast": "ff:ff:ff:ff:ff:ff",
        "promiscuity": 0,
        "min_mtu": 68,
        "max_mtu": 65521,
        "linkinfo": {
            "info_kind": "tun",
            "info_data": {
                "type": "tap",
                "pi": false,
                "vnet_hdr": false,
                "multi_queue": false,
                "persist": true,
                "user": "user"
            }
        },
        "inet6_addr_gen_mode": "eui64",
        "num_tx_queues": 1,
        "num_rx_queues": 1,
        "gso_max_size": 65536,
        "gso_max_segs": 65535
    }"#;
    fn tun_up() -> IpInterface {
        IpInterface {
            ifname: String::from("qemu"),
            flags: vec![
                String::from("NO-CARRIER"),
                String::from("BROADCAST"),
                String::from("MULTICAST"),
                String::from("UP"),
            ],
            operstate: String::from("DOWN"),
            linkinfo: LinkInfo { info_kind: String::from("tun") },
        }
    }

    const WRONG_NAME: &'static str = r#"{
        "ifindex": 12,
        "ifname": "br0",
        "flags": [ "NO-CARRIER","BROADCAST","MULTICAST","UP" ],
        "mtu": 1500,
        "qdisc": "fq_codel",
        "operstate": "DOWN",
        "linkmode": "DEFAULT",
        "group": "default",
        "txqlen": 1000,
        "link_type": "ether",
        "address": "55:55:55:55:55:55",
        "broadcast": "ff:ff:ff:ff:ff:ff",
        "promiscuity": 0,
        "min_mtu": 68,
        "max_mtu": 65521,
        "linkinfo": {
            "info_kind": "tun",
            "info_data": {
                "type": "tap",
                "pi": false,
                "vnet_hdr": false,
                "multi_queue": false,
                "persist": true,
                "user": "user"
            }
        },
        "inet6_addr_gen_mode": "eui64",
        "num_tx_queues": 1,
        "num_rx_queues": 1,
        "gso_max_size": 65536,
        "gso_max_segs": 65535
    }"#;
    fn wrong_name() -> IpInterface {
        IpInterface {
            ifname: String::from("br0"),
            flags: vec![
                String::from("NO-CARRIER"),
                String::from("BROADCAST"),
                String::from("MULTICAST"),
                String::from("UP"),
            ],
            operstate: String::from("DOWN"),
            linkinfo: LinkInfo { info_kind: String::from("tun") },
        }
    }

    const BRIDGE: &'static str = r#"{
        "ifindex": 11,
        "ifname": "qemu",
        "flags": [ "NO-CARRIER","BROADCAST","MULTICAST","UP" ],
        "mtu": 1500,
        "qdisc": "noqueue",
        "operstate": "DOWN",
        "linkmode": "DEFAULT",
        "group": "default",
        "txqlen": 1000,
        "link_type": "ether",
        "address": "11:22:33:44:55:66",
        "broadcast": "ff:ff:ff:ff:ff:ff",
        "promiscuity": 0,
        "min_mtu": 68,
        "max_mtu": 65535,
        "linkinfo": {
            "info_kind": "bridge",
            "info_data": {
                "forward_delay": 1500,
                "hello_time": 200,
                "max_age": 2000,
                "ageing_time": 30000,
                "stp_state": 0,
                "priority": 32768,
                "vlan_filtering": 0,
                "vlan_protocol": "802.1Q",
                "bridge_id": "8000.11:22:33:44:55:66",
                "root_id": "8000.11:22:33:44:55:66",
                "root_port": 0,
                "root_path_cost": 0,
                "topology_change": 0,
                "topology_change_detected": 0,
                "hello_timer": 0.00,
                "tcn_timer": 0.00,
                "topology_change_timer": 0.00,
                "gc_timer": 160.35,
                "vlan_default_pvid": 1,
                "vlan_stats_enabled": 0,
                "vlan_stats_per_port": 0,
                "group_fwd_mask": "0",
                "group_addr": "11:22:33:00:00:00",
                "mcast_snooping": 1,
                "mcast_vlan_snooping": 0,
                "mcast_router": 1,
                "mcast_query_use_ifaddr": 0,
                "mcast_querier": 0,
                "mcast_hash_elasticity": 16,
                "mcast_hash_max": 4096,
                "mcast_last_member_cnt": 2,
                "mcast_startup_query_cnt": 2,
                "mcast_last_member_intvl": 100,
                "mcast_membership_intvl": 26000,
                "mcast_querier_intvl": 25500,
                "mcast_query_intvl": 12500,
                "mcast_query_response_intvl": 1000,
                "mcast_startup_query_intvl": 3124,
                "mcast_stats_enabled": 0,
                "mcast_igmp_version": 2,
                "mcast_mld_version": 1,
                "nf_call_iptables": 0,
                "nf_call_ip6tables": 0,
                "nf_call_arptables": 0
            }
        },
        "inet6_addr_gen_mode": "none",
        "num_tx_queues": 1,
        "num_rx_queues": 1,
        "gso_max_size": 65536,
        "gso_max_segs": 65535
    }"#;
    fn bridge() -> IpInterface {
        IpInterface {
            ifname: String::from("qemu"),
            flags: vec![
                String::from("NO-CARRIER"),
                String::from("BROADCAST"),
                String::from("MULTICAST"),
                String::from("UP"),
            ],
            operstate: String::from("DOWN"),
            linkinfo: LinkInfo { info_kind: String::from("bridge") },
        }
    }

    const TUN_DOWN: &'static str = r#"{
        "ifindex": 14,
        "ifname": "qemu",
        "flags": [ "BROADCAST","MULTICAST" ],
        "mtu": 1500,
        "qdisc": "fq_codel",
        "operstate": "DOWN",
        "linkmode": "DEFAULT",
        "group": "default",
        "txqlen": 1000,
        "link_type": "ether",
        "address": "aa:bb:cc:dd:ee:ff",
        "broadcast": "ff:ff:ff:ff:ff:ff",
        "promiscuity": 0,
        "min_mtu": 68,
        "max_mtu": 65521,
        "linkinfo": {
            "info_kind": "tun",
            "info_data": {
                "type": "tap",
                "pi": false,
                "vnet_hdr": false,
                "multi_queue": false,
                "persist": true,
                "user": "user"
            }
        },
        "inet6_addr_gen_mode": "eui64",
        "num_tx_queues": 1,
        "num_rx_queues": 1,
        "gso_max_size": 65536,
        "gso_max_segs": 65535
    }"#;
    fn tun_down() -> IpInterface {
        IpInterface {
            ifname: String::from("qemu"),
            flags: vec![String::from("BROADCAST"), String::from("MULTICAST")],
            operstate: String::from("DOWN"),
            linkinfo: LinkInfo { info_kind: String::from("tun") },
        }
    }

    const CONNECTED: &'static str = r#"{
        "ifindex": 18,
        "ifname": "qemu",
        "flags": [ "BROADCAST","MULTICAST","UP","LOWER_UP" ],
        "mtu": 1500,
        "qdisc": "fq_codel",
        "operstate": "UP",
        "linkmode": "DEFAULT",
        "group": "default",
        "txqlen": 1000,
        "link_type": "ether",
        "address": "bb:aa:dd:dd:aa:dd",
        "broadcast": "ff:ff:ff:ff:ff:ff",
        "promiscuity": 0,
        "min_mtu": 68,
        "max_mtu": 65521,
        "linkinfo": {
            "info_kind": "tun",
            "info_data": {
                "type": "tap",
                "pi": false,
                "vnet_hdr": true,
                "multi_queue": false,
                "persist": true,
                "user": "user"
            }
        },
        "inet6_addr_gen_mode": "eui64",
        "num_tx_queues": 1,
        "num_rx_queues": 1,
        "gso_max_size": 65536,
        "gso_max_segs": 65535
    }"#;
    fn connected() -> IpInterface {
        IpInterface {
            ifname: String::from("qemu"),
            flags: vec![
                String::from("BROADCAST"),
                String::from("MULTICAST"),
                String::from("UP"),
                String::from("LOWER_UP"),
            ],
            operstate: String::from("UP"),
            linkinfo: LinkInfo { info_kind: String::from("tun") },
        }
    }

    #[test]
    fn test_parse_interface_from_details() -> Result<()> {
        let tap = tap::QemuTunTap::default();

        // When the return is not correctly formatted JSON.
        let result = tap.parse_interface_from_details(format!("[{}]", INVALID).as_bytes().to_vec());
        assert!(result.is_err());

        // Loopback interface isn't tap.
        let result =
            tap.parse_interface_from_details(format!("[{}]", LOOPBACK).as_bytes().to_vec());
        assert!(result.is_ok(), "{:?}", result.unwrap_err());
        assert_eq!(result.unwrap(), loopback());

        // Bridge isn't tap.
        let result = tap.parse_interface_from_details(format!("[{}]", BRIDGE).as_bytes().to_vec());
        assert!(result.is_ok(), "{:?}", result.unwrap_err());
        assert_eq!(result.unwrap(), bridge());

        // Valid tap interface, but not the one we're planning on using.
        let result =
            tap.parse_interface_from_details(format!("[{}]", WRONG_NAME).as_bytes().to_vec());
        assert!(result.is_err());

        // Valid tap, but administratively DOWN.
        let result =
            tap.parse_interface_from_details(format!("[{}]", TUN_DOWN).as_bytes().to_vec());
        assert!(result.is_ok(), "{:?}", result.unwrap_err());
        assert_eq!(result.unwrap(), tun_down());

        // Valid tap interface, but in use by another process.
        let result =
            tap.parse_interface_from_details(format!("[{}]", CONNECTED).as_bytes().to_vec());
        assert!(result.is_ok(), "{:?}", result.unwrap_err());
        assert_eq!(result.unwrap(), connected());

        // Valid tap interface, ready to use.
        let result = tap.parse_interface_from_details(format!("[{}]", TUN_UP).as_bytes().to_vec());
        assert!(result.is_ok(), "{:?}", result.unwrap_err());
        assert_eq!(result.unwrap(), tun_up());

        Ok(())
    }

    #[test]
    fn test_interface_is_up() -> Result<()> {
        let tap = tap::QemuTunTap::default();
        assert!(tap.interface_is_up(&loopback()));
        assert!(tap.interface_is_up(&tun_up()));
        assert!(tap.interface_is_up(&bridge()));
        assert!(!tap.interface_is_up(&tun_down()));
        assert!(tap.interface_is_up(&wrong_name()));
        assert!(tap.interface_is_up(&connected()));
        Ok(())
    }

    #[test]
    fn test_interface_is_tap() -> Result<()> {
        let tap = tap::QemuTunTap::default();
        assert!(!tap.interface_is_tap(&loopback()));
        assert!(tap.interface_is_tap(&tun_up()));
        assert!(!tap.interface_is_tap(&bridge()));
        assert!(tap.interface_is_tap(&tun_down()));
        assert!(tap.interface_is_tap(&wrong_name()));
        assert!(tap.interface_is_tap(&connected()));
        Ok(())
    }

    #[test]
    fn test_interface_is_in_use() -> Result<()> {
        let tap = tap::QemuTunTap::default();
        assert!(!tap.interface_is_in_use(&loopback()));
        assert!(!tap.interface_is_in_use(&tun_up()));
        assert!(!tap.interface_is_in_use(&bridge()));
        assert!(!tap.interface_is_in_use(&tun_down()));
        assert!(!tap.interface_is_in_use(&wrong_name()));
        assert!(tap.interface_is_in_use(&connected()));
        Ok(())
    }

    #[test]
    fn test_tap_inner() -> Result<()> {
        let mut tap = QemuTunTap::default();
        let real_thing = tap::QemuTunTap::default();
        let clone1 = real_thing.clone();
        let clone2 = real_thing.clone();
        tap.expect_interface_is_up().returning(move |i| real_thing.interface_is_up(&i));
        tap.expect_interface_is_tap().returning(move |i| clone1.interface_is_tap(&i));
        tap.expect_interface_is_in_use().returning(move |i| clone2.interface_is_in_use(&i));
        tap.expect_host_is_mac().returning(|| false).times(12);
        tap.expect_get_interface_details()
            .returning(|| Ok(String::from("ignored value").as_bytes().to_vec()));

        // Error condition means no interface.
        tap.expect_parse_interface_from_details().returning(|_| bail!("error")).times(2);
        let result = tap_inner(&tap, false);
        assert!(result.is_err());
        let result = tap_inner(&tap, true);
        assert!(result.is_err());

        // Loopback interface isn't tap.
        tap.expect_parse_interface_from_details().returning(|_| Ok(loopback())).times(2);
        let result = tap_inner(&tap, false);
        assert!(result.is_err());
        let result = tap_inner(&tap, true);
        assert!(result.is_err());

        // Bridge isn't tap.
        tap.expect_parse_interface_from_details().returning(|_| Ok(bridge())).times(2);
        let result = tap_inner(&tap, false);
        assert!(result.is_err());
        let result = tap_inner(&tap, true);
        assert!(result.is_err());

        // Valid tap, but administratively DOWN.
        // Should be an error if check_for_ready is true, Ok otherwise.
        tap.expect_parse_interface_from_details().returning(|_| Ok(tun_down())).times(2);
        let result = tap_inner(&tap, false);
        assert!(result.is_ok(), "{:?}", result.unwrap_err());
        let result = tap_inner(&tap, true);
        assert!(result.is_err());

        // Valid tap interface, but in use by another process.
        tap.expect_parse_interface_from_details().returning(|_| Ok(connected())).times(2);
        let result = tap_inner(&tap, false);
        assert!(result.is_err());
        let result = tap_inner(&tap, true);
        assert!(result.is_err());

        // Valid tap interface, ready to use.
        tap.expect_parse_interface_from_details().returning(|_| Ok(tun_up())).times(2);
        let result = tap_inner(&tap, false);
        assert!(result.is_ok(), "{:?}", result.unwrap_err());
        let result = tap_inner(&tap, true);
        assert!(result.is_ok(), "{:?}", result.unwrap_err());

        // Add a check for Macs. If this was on Linux, it would be Ok, but Mac is unsupported.
        tap.expect_parse_interface_from_details().returning(|_| Ok(tun_up())).times(0);
        tap.expect_host_is_mac().returning(|| true).times(2);
        let result = tap_inner(&tap, false);
        assert!(result.is_err());
        let result = tap_inner(&tap, true);
        assert!(result.is_err());

        Ok(())
    }
}
