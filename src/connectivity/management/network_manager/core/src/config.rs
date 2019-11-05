// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde_derive::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

/// Interface types defined by the OpenConfig interfaces model.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Hash, Clone)]
#[serde(deny_unknown_fields, rename_all = "SCREAMING_SNAKE_CASE")]
pub enum InterfaceType {
    IfEthernet,
    IfAggregate,
    IfLoopback,
    IfRoutedVlan,
    IfSonet,
    IfTunnelGre4,
    IfTunnelGre6,
}

/// The possible interface operational states from RFC2863 "Standard Interfaces MIB".
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Hash, Clone)]
#[serde(deny_unknown_fields, rename_all = "SCREAMING_SNAKE_CASE")]
pub enum OperState {
    Up,
    Down,
    Unknown,
    Testing,
    NotPresent,
    LowerLayerDown,
}

/// The possible interface admin states from RFC2863 "Standard Interfaces MIB".
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Hash, Clone)]
#[serde(deny_unknown_fields, rename_all = "SCREAMING_SNAKE_CASE")]
pub enum AdminState {
    Up,
    Down,
    Testing,
}

/// The IP address family.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Hash, Clone)]
#[serde(deny_unknown_fields, rename_all = "SCREAMING_SNAKE_CASE")]
pub enum AddressFamily {
    V4,
    V6,
}

/// When `auto-negotiate` is true, this optionally sets the duplex mode that will be advertised to
/// the peer. If unspecified, the interface should negotiate the duplex mode directly (typically
/// full-duplex). When auto-negotiate is false, this sets the duplex mode on the interface directly.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Hash, Clone)]
#[serde(deny_unknown_fields, rename_all = "SCREAMING_SNAKE_CASE")]
pub enum DuplexMode {
    Full,
    Half,
}

/// When `auto-negotiate` is true, this optionally sets the port-speed mode that will be advertised
/// to the peer for negotiation. If unspecified, it is expected that the interface will select the
/// highest speed available based on negotiation. When auto-negotiate is set to false, sets the
/// link speed to a fixed value.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Hash, Clone)]
#[serde(deny_unknown_fields)]
pub enum PortSpeed {
    #[serde(alias = "SPEED_10MB")]
    Speed10mb,
    #[serde(alias = "SPEED_100MB")]
    Speed100mb,
    #[serde(alias = "SPEED_1G")]
    Speed1g,
    #[serde(alias = "SPEED_2500MB")]
    Speed2500mb,
    #[serde(alias = "SPEED_5G")]
    Speed5g,
    #[serde(alias = "SPEED_10G")]
    Speed10g,
    #[serde(alias = "SPEED_UNKNOWN")]
    SpeedUnknown,
}

// TODO(cgibson): VLANs and IRBs.
// TODO(cgibson): ACLs.
// TODO(cgibson): WLAN.
// TODO(cgibson): Need to figure out versioning. Having "unknown" fields and "flatten"'ing themn
// into an `extras` field might be an interesting experiment.
#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct DeviceConfig {
    pub device: Option<Device>,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct Device {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub interfaces: Option<Interfaces>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub acls: Option<Acls>,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct Interfaces {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub interface: Option<Vec<Interface>>,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct Interface {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub config: Option<InterfaceConfig>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub oper_state: Option<OperState>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub subinterfaces: Option<Subinterfaces>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ethernet: Option<Ethernet>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tcp_offload: Option<bool>,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct InterfaceConfig {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub r#type: Option<InterfaceType>,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct Subinterfaces {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub subinterface: Option<Vec<Subinterface>>,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct Subinterface {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub config: Option<SubinterfaceConfig>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ipv4: Option<IpAddressConfig>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ipv6: Option<IpAddressConfig>,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct SubinterfaceConfig {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub admin_state: Option<AdminState>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub index: Option<u32>,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct IpAddressConfig {
    pub addresses: Option<Vec<IpAddress>>,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct IpAddress {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dhcp_client: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub address_family: Option<AddressFamily>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ip: Option<std::net::IpAddr>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub prefix_length: Option<u8>,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct Ethernet {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub config: Option<EthernetConfig>,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct EthernetConfig {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub auto_negotiate: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub duplex_mode: Option<DuplexMode>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub enable_flow_control: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub mac_address: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub mtu: Option<u16>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub port_speed: Option<PortSpeed>,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct Acls {}

#[derive(Debug)]
struct DeviceConfigPaths {
    user_config_path: PathBuf,
    factory_config_path: PathBuf,
    device_schema_path: PathBuf,
}

#[derive(Debug)]
pub struct Config {
    config: Option<DeviceConfig>,
    paths: DeviceConfigPaths,
}

impl Config {
    /// Creates a new Config object with the given user, factory, and device schema paths.
    pub fn new<P: Into<PathBuf>>(user_path: P, factory_path: P, device_schema: P) -> Config {
        Config {
            config: None,
            paths: DeviceConfigPaths {
                user_config_path: user_path.into(),
                factory_config_path: factory_path.into(),
                device_schema_path: device_schema.into(),
            },
        }
    }

    /// Returns the user config path.
    pub fn user_config_path(&self) -> &Path {
        self.paths.user_config_path.as_path()
    }

    /// Returns the factory config path.
    pub fn factory_config_path(&self) -> &Path {
        self.paths.factory_config_path.as_path()
    }

    /// Returns the device schema path.
    pub fn device_schema_path(&self) -> &Path {
        self.paths.device_schema_path.as_path()
    }

    /// TODO(cgibson): Implement me.
    pub fn load_config(&mut self) {
        // TODO(cgibson): Implement me.
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_new() {
        let user_cfg = "/data/my/user.cfg";
        let factory_cfg = "/data/my/factory.cfg";
        let device_schema = "/data/my/device_schema.cfg";
        let test_config = Config::new(user_cfg, factory_cfg, device_schema);
        assert_eq!(test_config.config.is_none(), true);
        assert_eq!(test_config.paths.user_config_path, Path::new(user_cfg).to_path_buf());
        assert_eq!(test_config.paths.factory_config_path, Path::new(factory_cfg).to_path_buf());
        assert_eq!(test_config.paths.device_schema_path, Path::new(device_schema).to_path_buf());
    }

    #[test]
    fn test_get_paths() {
        let user_cfg = "/data/my/user.cfg";
        let factory_cfg = "/data/my/factory.cfg";
        let device_schema = "/data/my/device_schema.cfg";
        let test_config = Config::new(user_cfg, factory_cfg, device_schema);

        assert_eq!(test_config.config.is_none(), true);
        assert_eq!(test_config.user_config_path(), Path::new(user_cfg));
        assert_eq!(test_config.factory_config_path(), Path::new(factory_cfg));
        assert_eq!(test_config.device_schema_path(), Path::new(device_schema));
    }
}
