// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context as _};
use fidl_fuchsia_data as fdata;
use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_netemul_network as fnetemul_network;
use std::collections::{hash_map, HashMap, HashSet};
use std::str::FromStr;

#[derive(serde::Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub(crate) struct Network {
    name: String,
    endpoints: Vec<Endpoint>,
}

#[derive(serde::Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub(crate) struct Endpoint {
    name: String,
    mac: Option<fnet_ext::MacAddress>,
    #[serde(default = "Endpoint::default_mtu")]
    mtu: u16,
    #[serde(default = "Endpoint::default_link_up")]
    up: bool,
    #[serde(default)]
    backing: EndpointBacking,
}

impl Endpoint {
    const fn default_mtu() -> u16 {
        netemul::DEFAULT_MTU
    }

    const fn default_link_up() -> bool {
        true
    }
}

#[derive(serde::Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields, rename_all = "snake_case")]
pub(crate) enum EndpointBacking {
    Ethertap,
    NetworkDevice,
}

impl Default for EndpointBacking {
    fn default() -> Self {
        Self::Ethertap
    }
}

impl From<EndpointBacking> for fnetemul_network::EndpointBacking {
    fn from(backing: EndpointBacking) -> Self {
        match backing {
            EndpointBacking::Ethertap => Self::Ethertap,
            EndpointBacking::NetworkDevice => Self::NetworkDevice,
        }
    }
}

#[derive(serde::Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub(crate) struct Netstack {
    name: String,
    interfaces: Vec<Interface>,
}

#[derive(serde::Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub(crate) struct Interface {
    name: String,
    #[serde(deserialize_with = "deserialize_subnets")]
    static_ips: Vec<fnet_ext::Subnet>,
}

fn deserialize_subnets<'de, D>(deserializer: D) -> Result<Vec<fnet_ext::Subnet>, D::Error>
where
    D: serde::Deserializer<'de>,
{
    let v = <Vec<String> as serde::Deserialize>::deserialize(deserializer)?;
    v.into_iter()
        .map(|s| fnet_ext::Subnet::from_str(&s))
        .collect::<Result<Vec<_>, _>>()
        .map_err(serde::de::Error::custom)
}

// Represents a configuration that has been deserialized but not yet validated.
//
// To produce a valid `Config`, pass an `UnvalidatedConfig` to
// `Config::validate`.
#[derive(serde::Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
struct UnvalidatedConfig {
    networks: Vec<Network>,
    netstacks: Vec<Netstack>,
}

#[derive(Debug, PartialEq)]
pub(crate) struct Config {
    pub networks: Vec<Network>,
    pub netstacks: Vec<Netstack>,
}

#[derive(thiserror::Error, Debug, PartialEq)]
pub(crate) enum Error {
    #[error("duplicate network `{0}`, network names must be unique")]
    DuplicateNetwork(String),
    #[error("duplicate netstack `{0}`, netstack names must be unique")]
    DuplicateNetstack(String),
    #[error("duplicate endpoint `{0}`, endpoint names must be unique")]
    DuplicateEndpoint(String),
    #[error("endpoint `{0}` assigned to a netstack multiple times")]
    EndpointAssignedMultipleTimes(String),
    #[error("unknown endpoint `{0}`, must be declared on a network")]
    UnknownEndpoint(String),
}

impl UnvalidatedConfig {
    fn validate(self) -> Result<Config, Error> {
        let Self { networks, netstacks } = &self;

        let mut network_names = HashSet::new();
        let mut installed_endpoints = HashMap::new();
        let mut netstack_names = HashSet::new();

        for Network { name, endpoints } in networks {
            if !network_names.insert(name) {
                return Err(Error::DuplicateNetwork(name.to_string()));
            }
            for Endpoint { name, .. } in endpoints {
                if let Some(_) = installed_endpoints.insert(name, false) {
                    return Err(Error::DuplicateEndpoint(name.to_string()));
                }
            }
        }

        for Netstack { name, interfaces } in netstacks {
            if !netstack_names.insert(name) {
                return Err(Error::DuplicateNetstack(name.to_string()));
            }
            for Interface { name, .. } in interfaces {
                match installed_endpoints.entry(&name) {
                    hash_map::Entry::Occupied(mut entry) => {
                        if *entry.get() {
                            return Err(Error::EndpointAssignedMultipleTimes(name.to_string()));
                        } else {
                            *entry.get_mut() = true;
                        }
                    }
                    hash_map::Entry::Vacant(_) => {
                        return Err(Error::UnknownEndpoint(name.to_string()));
                    }
                }
            }
        }

        let Self { networks, netstacks } = self;
        Ok(Config { networks, netstacks })
    }
}

impl FromStr for Config {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let config: UnvalidatedConfig = serde_json::from_str(s).context("deserializing config")?;
        config.validate().context("validating config")
    }
}

const NETWORK_CONFIG_PROPERTY_NAME: &str = "network_config";

/// Loads the virtual network configuration that the test should be run in.
///
/// The configuration should be a JSON file available in the package directory at `pkg_dir`, and the
/// filepath is specified in `program`.
pub(crate) async fn load_from_program(
    program: fdata::Dictionary,
    pkg_dir: &fidl_fuchsia_io::DirectoryProxy,
) -> Result<Config, anyhow::Error> {
    let fdata::Dictionary { entries, .. } = program;

    // TODO(https://fxbug.dev/92247): read the configuration directly from the `program` section
    // rather than from a packaged config file, once the CML schema supports it.
    let network_config = entries
        .context("`entries` field not set in program")?
        .into_iter()
        .find_map(|fdata::DictionaryEntry { key, value }| {
            (key == NETWORK_CONFIG_PROPERTY_NAME).then(|| value)
        })
        .with_context(|| format!("`{}` missing in program", NETWORK_CONFIG_PROPERTY_NAME))?
        .context("missing value for network configuration property")?;

    // Temporarily allow unreachable patterns while fuchsia.data.DictionaryValue
    // is migrated from `strict` to `flexible`.
    // TODO(https://fxbug.dev/92247): Remove this.
    #[allow(unreachable_patterns)]
    let network_config_path = match *network_config {
        fdata::DictionaryValue::Str(path) => Ok(path),
        fdata::DictionaryValue::StrVec(vec) => Err(anyhow!(
            "`{}` should be a single filepath; got a list: {:?}",
            NETWORK_CONFIG_PROPERTY_NAME,
            vec
        )),
        other => Err(anyhow::anyhow!("encountered unknown DictionaryValue variant: {:?}", other)),
    }?;

    let file = io_util::open_file(
        pkg_dir,
        std::path::Path::new(&network_config_path),
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
    )
    .with_context(|| format!("opening network config file at {}", network_config_path))?;
    let contents =
        io_util::file::read_to_string(&file).await.context("reading network config file")?;

    contents.parse()
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use net_declare::{fidl_mac, fidl_subnet};
    use test_case::test_case;

    #[test]
    fn valid_config() {
        let file = r#"
{
    "netstacks": [
        {
            "name": "local",
            "interfaces": [
                {
                    "name": "local-ep",
                    "static_ips": [ "192.168.0.2/24" ]
                },
                {
                    "name": "local-ep2",
                    "static_ips": [ "192.168.0.3/24" ]
                }
            ]
        },
        {
            "name": "remote",
            "interfaces": [
                {
                    "name": "remote-ep",
                    "static_ips": [ "192.168.0.1/24" ]
                }
            ]
        }
    ],
    "networks": [
        {
            "name": "net",
            "endpoints": [
                {
                    "name": "local-ep",
                    "mac": "aa:bb:cc:dd:ee:ff",
                    "mtu": 999,
                    "up": false,
                    "backing": "network_device"
                },
                {
                    "name": "local-ep2"
                },
                {
                    "name": "remote-ep"
                }
            ]
        }
    ]
}
"#;

        let expected = Config {
            netstacks: vec![
                Netstack {
                    name: "local".to_string(),
                    interfaces: vec![
                        Interface {
                            name: "local-ep".to_string(),
                            static_ips: vec![fidl_subnet!("192.168.0.2/24").into()],
                        },
                        Interface {
                            name: "local-ep2".to_string(),
                            static_ips: vec![fidl_subnet!("192.168.0.3/24").into()],
                        },
                    ],
                },
                Netstack {
                    name: "remote".to_string(),
                    interfaces: vec![Interface {
                        name: "remote-ep".to_string(),
                        static_ips: vec![fidl_subnet!("192.168.0.1/24").into()],
                    }],
                },
            ],
            networks: vec![Network {
                name: "net".to_string(),
                endpoints: vec![
                    Endpoint {
                        name: "local-ep".to_string(),
                        mac: Some(fidl_mac!("aa:bb:cc:dd:ee:ff").into()),
                        mtu: 999,
                        up: false,
                        backing: EndpointBacking::NetworkDevice,
                    },
                    Endpoint {
                        name: "local-ep2".to_string(),
                        mac: None,
                        mtu: Endpoint::default_mtu(),
                        up: Endpoint::default_link_up(),
                        backing: Default::default(),
                    },
                    Endpoint {
                        name: "remote-ep".to_string(),
                        mac: None,
                        mtu: Endpoint::default_mtu(),
                        up: Endpoint::default_link_up(),
                        backing: Default::default(),
                    },
                ],
            }],
        };

        let config: Config = file.parse().expect("deserialize network config");
        assert_eq!(config, expected);
    }

    #[test_case(r#"{ "netstacks": [] }"#; "missing required field `networks`")]
    #[test_case(
        r#"{
            "netstacks": [],
            "networks": [],
            "endpoints": []
        }"#;
        "unknown field `endpoints`"
    )]
    #[test_case(
        r#"{
            "netstacks": [],
            "networks": [
                {
                    "name": "net",
                    "endpoints": [
                        {
                            "name": "ep",
                            "mtu": 65536
                        }
                    ]
                }
            ]
        }"#;
        "invalid MTU (larger than `u16::MAX`)"
    )]
    fn invalid_parse(s: &str) {
        assert_matches!(s.parse::<Config>(), Err(_));
    }

    #[test_case(
        UnvalidatedConfig {
            netstacks: vec![
                Netstack {
                    name: "netstack".to_string(),
                    interfaces: vec![
                        Interface {
                            name: "ep".to_string(),
                            static_ips: vec![],
                        },
                    ],
                },
            ],
            networks: vec![],
        },
        Error::UnknownEndpoint("ep".to_string());
        "netstack interfaces must be declared as endpoints on a network"
    )]
    #[test_case(
        UnvalidatedConfig {
            netstacks: vec![
                Netstack {
                    name: "netstack".to_string(),
                    interfaces: vec![],
                },
                Netstack {
                    name: "netstack".to_string(),
                    interfaces: vec![],
                },
            ],
            networks: vec![],
        },
        Error::DuplicateNetstack("netstack".to_string());
        "netstack names must be unique"
    )]
    #[test_case(
        UnvalidatedConfig {
            netstacks: vec![],
            networks: vec![
                Network {
                    name: "net".to_string(),
                    endpoints: vec![],
                },
                Network {
                    name: "net".to_string(),
                    endpoints: vec![],
                },
            ],
        },
        Error::DuplicateNetwork("net".to_string());
        "network names must be unique"
    )]
    #[test_case(
        UnvalidatedConfig {
            netstacks: vec![],
            networks: vec![
                Network {
                    name: "net".to_string(),
                    endpoints: vec![
                        Endpoint {
                            name: "ep".to_string(),
                            mac: None,
                            mtu: Endpoint::default_mtu(),
                            up: Endpoint::default_link_up(),
                            backing: Default::default(),
                        },
                        Endpoint {
                            name: "ep".to_string(),
                            mac: None,
                            mtu: Endpoint::default_mtu(),
                            up: Endpoint::default_link_up(),
                            backing: Default::default(),
                        },
                    ],
                },
            ],
        },
        Error::DuplicateEndpoint("ep".to_string());
        "endpoint names must be unique"
    )]
    #[test_case(
        UnvalidatedConfig {
            netstacks: vec![
                Netstack {
                    name: "ns1".to_string(),
                    interfaces: vec![
                        Interface {
                            name: "ep".to_string(),
                            static_ips: vec![],
                        },
                    ],
                },
                Netstack {
                    name: "ns2".to_string(),
                    interfaces: vec![
                        Interface {
                            name: "ep".to_string(),
                            static_ips: vec![],
                        },
                    ],
                },
            ],
            networks: vec![
                Network {
                    name: "net".to_string(),
                    endpoints: vec![
                        Endpoint {
                            name: "ep".to_string(),
                            mac: None,
                            mtu: Endpoint::default_mtu(),
                            up: Endpoint::default_link_up(),
                            backing: Default::default(),
                        },
                    ],
                },
            ],
        },
        Error::EndpointAssignedMultipleTimes("ep".to_string());
        "endpoints may only be assigned once to a single netstack"
    )]
    fn invalid_config(config: UnvalidatedConfig, error: Error) {
        assert_eq!(config.validate(), Err(error));
    }
}
