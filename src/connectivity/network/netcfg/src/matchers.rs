// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::de::Error;
use serde::Deserialize;

#[derive(Debug, Eq, PartialEq, Copy, Clone)]
pub(crate) enum IpAddressConfig {
    Dhcp,
    StaticIp(fidl_fuchsia_net_ext::Subnet),
}

pub(crate) struct InterfaceConfig {
    pub fidl: fidl_fuchsia_netstack::InterfaceConfig,
    pub ip_address_config: IpAddressConfig,
}

pub(crate) fn config_for_device(
    device_info: &fidl_fuchsia_hardware_ethernet_ext::EthernetInfo,
    name: String,
    topological_path: &str,
    metric: u32,
    rules: &Vec<InterfaceSpec>,
    filepath: &std::path::PathBuf,
) -> InterfaceConfig {
    rules.iter().filter_map(|spec| matches_info(&spec, &topological_path, device_info)).fold(
        InterfaceConfig {
            fidl: fidl_fuchsia_netstack::InterfaceConfig {
                name,
                filepath: filepath.display().to_string(),
                metric,
            },
            ip_address_config: IpAddressConfig::Dhcp,
        },
        |seed, opt| match opt {
            ConfigOption::IpConfig(value) => {
                InterfaceConfig { ip_address_config: (*value).into(), ..seed }
            }
        },
    )
}

fn matches_info<'a>(
    spec: &'a InterfaceSpec,
    topological_path: &str,
    info: &fidl_fuchsia_hardware_ethernet_ext::EthernetInfo,
) -> Option<&'a ConfigOption> {
    let matches = match &spec.matcher {
        InterfaceMatcher::All => true,
        InterfaceMatcher::TopoPath(path) => path == topological_path,
        InterfaceMatcher::MacAddress(address) => {
            address == &fidl_fuchsia_hardware_ethernet_ext::MacAddress { octets: info.mac.octets }
        }
        InterfaceMatcher::Feature(matcher_features) => info.features.contains(*matcher_features),
    };
    if matches {
        Some(&spec.config)
    } else {
        None
    }
}

#[derive(Debug, Eq, PartialEq)]
pub enum InterfaceMatcher {
    All,
    TopoPath(String),
    MacAddress(fidl_fuchsia_hardware_ethernet_ext::MacAddress),
    Feature(fidl_fuchsia_hardware_ethernet::Features),
}

fn parse_feature(s: &str) -> Result<fidl_fuchsia_hardware_ethernet::Features, anyhow::Error> {
    match s.as_ref() {
        "synthetic" => Ok(fidl_fuchsia_hardware_ethernet::Features::Synthetic),
        "loopback" => Ok(fidl_fuchsia_hardware_ethernet::Features::Loopback),
        "wireless" => Ok(fidl_fuchsia_hardware_ethernet::Features::Wlan),
        s => Err(anyhow::anyhow!("unknown network interface feature \"{}\"", s)),
    }
}

impl InterfaceMatcher {
    fn parse_as_tuple(matcher: (&str, &str)) -> Result<Self, anyhow::Error> {
        match matcher {
            ("all", _) => Ok(InterfaceMatcher::All),
            ("topological_path", p) => Ok(InterfaceMatcher::TopoPath(p.to_string())),
            ("mac_address", address) => Ok(InterfaceMatcher::MacAddress(
                address.parse::<fidl_fuchsia_hardware_ethernet_ext::MacAddress>()?,
            )),
            ("feature", feature) => Ok(InterfaceMatcher::Feature(parse_feature(feature)?)),
            (unknown, _) => {
                Err(anyhow::format_err!("invalid matcher option for interface: {}", unknown))
            }
        }
    }
}

#[derive(Debug, Eq, PartialEq, Copy, Clone)]
pub(crate) enum ConfigOption {
    IpConfig(IpAddressConfig),
}

impl ConfigOption {
    fn parse_as_tuple(config: (&str, &str)) -> Result<Self, anyhow::Error> {
        match config {
            ("ip_address", "dhcp") => Ok(ConfigOption::IpConfig(IpAddressConfig::Dhcp)),
            ("ip_address", static_ip) => Ok(ConfigOption::IpConfig(IpAddressConfig::StaticIp(
                static_ip
                    .parse::<fidl_fuchsia_net_ext::Subnet>()
                    .expect("subnet parse should succeed"),
            ))),
            (unknown, _) => {
                Err(anyhow::format_err!("invalid config option for interface: {}", unknown))
            }
        }
    }
}

type InterfaceSpecSyntax = ((String, String), (String, String));

impl std::convert::TryInto<InterfaceSpec> for InterfaceSpecSyntax {
    type Error = anyhow::Error;

    fn try_into(self) -> Result<InterfaceSpec, anyhow::Error> {
        let ((matcher_type, matcher_value), (config_type, config_value)) = self;
        Ok(InterfaceSpec {
            matcher: InterfaceMatcher::parse_as_tuple((&matcher_type, &matcher_value))?,
            config: ConfigOption::parse_as_tuple((&config_type, &config_value))?,
        })
    }
}

#[derive(Debug, Eq, PartialEq)]
pub(crate) struct InterfaceSpec {
    pub matcher: InterfaceMatcher,
    pub config: ConfigOption,
}

impl InterfaceSpec {
    pub fn parse_as_tuples<'de, D>(deserializer: D) -> Result<Vec<InterfaceSpec>, D::Error>
    where
        D: serde::de::Deserializer<'de>,
    {
        let specs: Vec<InterfaceSpecSyntax> = Deserialize::deserialize(deserializer)?;
        specs
            .into_iter()
            .map(std::convert::TryInto::try_into)
            .collect::<Result<_, <InterfaceSpecSyntax as std::convert::TryInto<Self>>::Error>>()
            .map_err(D::Error::custom)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_interface_matcher_parse_as_tuple() {
        assert_eq!(
            InterfaceMatcher::All,
            InterfaceMatcher::parse_as_tuple(("all", "all")).expect("parse matcher should succeed")
        );
        assert_eq!(
            InterfaceMatcher::TopoPath("/some/topo/path".to_string()),
            InterfaceMatcher::parse_as_tuple(("topological_path", "/some/topo/path"))
                .expect("parse matcher should succeed")
        );
        assert_eq!(
            InterfaceMatcher::MacAddress(fidl_fuchsia_hardware_ethernet_ext::MacAddress {
                octets: [170, 187, 204, 221, 238, 255]
            }),
            InterfaceMatcher::parse_as_tuple(("mac_address", "AA:BB:CC:DD:EE:FF"))
                .expect("parse matcher should succeed")
        );
        assert_eq!(
            InterfaceMatcher::Feature(fidl_fuchsia_hardware_ethernet::Features::Synthetic),
            InterfaceMatcher::parse_as_tuple(("feature", "synthetic"))
                .expect("parse matcher should succeed")
        );
        assert!(InterfaceMatcher::parse_as_tuple(("unknown_matcher_type", "")).is_err());
        assert!(InterfaceMatcher::parse_as_tuple(("feature", "unknown_feature_type")).is_err());
    }

    #[test]
    fn test_config_option_parse_as_tuple() {
        assert_eq!(
            ConfigOption::IpConfig(IpAddressConfig::Dhcp),
            ConfigOption::parse_as_tuple(("ip_address", "dhcp"))
                .expect("parse config should succeed")
        );
        assert_eq!(
            ConfigOption::IpConfig(IpAddressConfig::StaticIp(
                "192.168.42.10/32"
                    .parse::<fidl_fuchsia_net_ext::Subnet>()
                    .expect("subnet parse should succeed")
            )),
            ConfigOption::parse_as_tuple(("ip_address", "192.168.42.10"))
                .expect("parse config should succeed")
        );
    }

    #[test]
    fn test_rule_overrides() {
        let got = config_for_device(
            &fidl_fuchsia_hardware_ethernet_ext::EthernetInfo {
                features: fidl_fuchsia_hardware_ethernet::Features::empty(),
                mac: fidl_fuchsia_hardware_ethernet_ext::MacAddress { octets: [0; 6] },
                mtu: 0,
            },
            "".to_string(),
            "",
            100,
            &vec![
                InterfaceSpec {
                    matcher: InterfaceMatcher::All,
                    config: ConfigOption::IpConfig(IpAddressConfig::Dhcp),
                },
                InterfaceSpec {
                    matcher: InterfaceMatcher::All,
                    config: ConfigOption::IpConfig(IpAddressConfig::StaticIp(
                        "127.0.0.1/32"
                            .parse::<fidl_fuchsia_net_ext::Subnet>()
                            .expect("subnet parse should succeed"),
                    )),
                },
            ],
            &std::path::PathBuf::from("filepath"),
        );

        assert_eq!(
            IpAddressConfig::StaticIp(
                "127.0.0.1/32"
                    .parse::<fidl_fuchsia_net_ext::Subnet>()
                    .expect("subnet parse should succeed")
            ),
            got.ip_address_config
        );
    }
}
