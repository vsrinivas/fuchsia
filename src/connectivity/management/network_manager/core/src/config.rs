// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error;
use crate::lifmgr;
use serde_derive::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::HashSet;
use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};
use valico::json_schema::{self, schema};

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
    IfUplink,
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

/// When `auto-negotiate` is true, this optionally sets the duplex mode that will be advertised to
/// the peer. If unspecified, the interface should negotiate the duplex mode directly (typically
/// full-duplex). When auto-negotiate is false, this sets the duplex mode on the interface directly.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Hash, Clone)]
#[serde(deny_unknown_fields, rename_all = "SCREAMING_SNAKE_CASE")]
pub enum DuplexMode {
    Full,
    Half,
}

/// Defines VLAN interface types.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Hash, Clone)]
#[serde(deny_unknown_fields, rename_all = "SCREAMING_SNAKE_CASE")]
pub enum InterfaceMode {
    Access,
    Trunk,
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

// TODO(cgibson): VLANs.
// TODO(cgibson): ACLs.
// TODO(cgibson): WLAN.
// TODO(cgibson): Need to figure out versioning. Having "unknown" fields and "flatten"'ing themn
// into an `extras` field might be an interesting experiment.
#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct DeviceConfig {
    pub device: Device,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Device {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub interfaces: Option<Vec<Interfaces>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub acls: Option<Acls>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub services: Option<Services>,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Interfaces {
    pub interface: Interface,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Interface {
    // Certain types of interface (e.g. RoutedVlan) do not have a device_id.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub device_id: Option<String>,
    // Every Interface must have a config definition.
    pub config: InterfaceConfig,
    // If oper_state is omitted, then the default is `OperState::Up`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub oper_state: Option<OperState>,
    // An interface must contain exactly one: 'subinterfaces', 'switched_vlan', or 'routed_vlan'.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub subinterfaces: Option<Vec<Subinterface>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub switched_vlan: Option<SwitchedVlan>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub routed_vlan: Option<RoutedVlan>,
    // ethernet can be omitted and defaults will be applied.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ethernet: Option<Ethernet>,
    // tcp_offload can be omitted and the default will be applied.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tcp_offload: Option<bool>,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct InterfaceConfig {
    pub name: String,
    #[serde(rename = "type")]
    pub interface_type: InterfaceType,
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Subinterface {
    // If admin_state is omitted, then the default is AdminState::Up.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub admin_state: Option<AdminState>,
    // A subinterface must have at least one IP address configuration.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ipv4: Option<IpAddressConfig>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ipv6: Option<IpAddressConfig>,
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct IpAddressConfig {
    pub addresses: Vec<IpAddress>,
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct IpAddress {
    // If omitted, the default is to enable a DHCP client on this interface.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dhcp_client: Option<bool>,
    // If an IP address is provided, it must be paired with a prefix length.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ip: Option<std::net::IpAddr>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub prefix_length: Option<u8>,
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct SwitchedVlan {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub interface_mode: Option<InterfaceMode>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub access_vlan: Option<u16>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub trunk_vlans: Option<Vec<u16>>,
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct RoutedVlan {
    pub vlan_id: u16,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ipv4: Option<IpAddressConfig>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ipv6: Option<IpAddressConfig>,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Ethernet {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub config: Option<EthernetConfig>,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
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

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Acls {}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Services {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dhcp_server: Option<DhcpServer>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ip_forwarding: Option<IpForwarding>,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct DhcpServer {
    pub enabled: bool,
    pub dhcp_pool: DhcpPool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub static_ip_allocations: Option<Vec<StaticIpAllocations>>,
    pub interfaces: String,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct DhcpPool {
    pub start: String,
    pub end: String,
    pub lease_time: String,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct StaticIpAllocations {
    pub device_name: String,
    pub mac_address: String,
    pub ip_address: String,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct IpForwarding {
    pub enabled: bool,
}

#[derive(Debug, PartialEq)]
struct DeviceConfigPaths {
    user_config_path: PathBuf,
    factory_config_path: PathBuf,
    device_schema_path: PathBuf,
}

#[derive(Debug, PartialEq)]
pub struct Config {
    device_config: Option<DeviceConfig>,
    startup_path: Option<PathBuf>,
    paths: DeviceConfigPaths,
}

/// Converts a Valico JSON SchemaError to a `String`.
fn schema_error(error: schema::SchemaError) -> String {
    match error {
        schema::SchemaError::WrongId => String::from("Wrong Id"),
        schema::SchemaError::IdConflicts => String::from("Id conflicts"),
        schema::SchemaError::NotAnObject => String::from("Not an object"),
        schema::SchemaError::UrlParseError(p) => String::from(format!("Url parse error: {}", p)),
        schema::SchemaError::UnknownKey(key) => String::from(format!("Unknown key: {}", key)),
        schema::SchemaError::Malformed { path, detail } => {
            String::from(format!("Malformed: {}, {}", path, detail))
        }
    }
}

impl Config {
    /// Creates a new Config object with the given user, factory, and device schema paths.
    pub fn new<P: Into<PathBuf>>(user_path: P, factory_path: P, device_schema: P) -> Config {
        Config {
            device_config: None,
            startup_path: None,
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

    /// Returns the path of the config that was read at startup
    ///
    /// If no configuration file has been read yet, then returns an empty path.
    pub fn startup_path(&self) -> &Path {
        if let Some(p) = &self.startup_path {
            p.as_path()
        } else {
            Path::new("")
        }
    }

    /// Loads the relevant configuration file.
    ///
    /// This method tries to load the user configuration file. If the user configuration file does
    /// not exist (e.g. first boot after OOBE, FDR, etc), then fallback to loading the factory
    /// configuration file.
    ///
    /// If this method returns successfully, there will be a newly loaded and properly validated
    /// deserialized configuration available.
    pub async fn load_config(&mut self) -> error::Result<()> {
        let loaded_config;
        let loaded_path;
        match self.try_load_config(&self.user_config_path()).await {
            Ok(c) => {
                loaded_config = c;
                loaded_path = self.paths.user_config_path.to_path_buf();
            }
            Err(e) => {
                warn!("Failed to load user config: {}", e);
                loaded_config = self.try_load_config(&self.factory_config_path()).await?;
                loaded_path = self.paths.factory_config_path.to_path_buf();
            }
        }
        match self.validate_with_schema(&loaded_config).await {
            Ok(_) => {
                self.device_config = Some(serde_json::from_value(loaded_config).map_err(|e| {
                    error::NetworkManager::CONFIG(error::Config::FailedToDeserializeConfig {
                        path: String::from(loaded_path.to_string_lossy()),
                        error: e.to_string(),
                    })
                })?);
                self.startup_path = Some(loaded_path);
                return self.final_validation().await;
            }
            Err(e) => Err(e),
        }
    }

    /// Attempts to load a given configuration file.
    ///
    /// Returns the [`serde_json::Value`] of the loaded configuration file. It is important to note
    /// that if the config fails further validation then it will not be used.
    ///
    /// # Errors
    ///
    /// If the config file does not exist, cannot be read, or is not a file, then an error result
    /// of `ConfigNotLoaded` will be returned.
    ///
    /// If the loaded configuration fails to deserialize, then an error result of
    /// `FailedToDeserializeConfig` will be returned.
    async fn try_load_config(&self, config_path: &Path) -> error::Result<Value> {
        info!("Trying to load from: {}", String::from(config_path.to_string_lossy()));
        if config_path.is_file() {
            let mut contents = String::new();
            let mut f = File::open(config_path).map_err(|e| {
                error::NetworkManager::CONFIG(error::Config::ConfigNotLoaded {
                    path: String::from(config_path.to_string_lossy()),
                    error: e.to_string(),
                })
            })?;
            f.read_to_string(&mut contents).map_err(|e| {
                error::NetworkManager::CONFIG(error::Config::ConfigNotLoaded {
                    path: String::from(config_path.to_string_lossy()),
                    error: e.to_string(),
                })
            })?;
            // Note that counter to intuition it is faster to read the full configuration file
            // into memory and deserialize it using serde_json::from_str(), than using
            // serde_json::from_reader(), see: https://github.com/serde-rs/json/issues/160.
            let json: Value = serde_json::from_str(&contents).map_err(|e| {
                error::NetworkManager::CONFIG(error::Config::FailedToDeserializeConfig {
                    path: String::from(config_path.to_string_lossy()),
                    error: e.to_string(),
                })
            })?;
            return Ok(json);
        }
        Err(error::NetworkManager::CONFIG(error::Config::ConfigNotFound {
            path: String::from(config_path.to_string_lossy()),
        }))
    }

    /// Ensure that the configuration validates against the device schema.
    ///
    /// # Errors
    ///
    /// If the actual device schema fails to load, then Valico will emit an appropriate error
    /// message that can be used to fix device schema errors.
    ///
    /// If the config does not validate against the device schema, then an error result
    /// `FailedToValidateConfig` will be returned providing information with what went wrong.
    async fn validate_with_schema(&self, config: &Value) -> error::Result<()> {
        info!("Validating config against the device schema");
        let device_schema = self.try_load_config(&self.device_schema_path()).await?;
        let mut scope = json_schema::Scope::new();
        let schema = scope.compile_and_return(device_schema, false).map_err(|e| {
            error!("Failed to validate schema: {:?}", e);
            error::NetworkManager::CONFIG(error::Config::FailedToValidateConfig {
                path: String::from(self.device_schema_path().to_string_lossy()),
                error: schema_error(e),
            })
        })?;
        // Use the JSON Schema to give the JSON config some formal structure, but we need to
        // do most of the validation ourselves.
        let result = schema.validate(config);
        if !result.is_strictly_valid() {
            let mut err_msgs = Vec::new();
            for e in &result.errors {
                err_msgs.push(format!("{} at {}", e.get_title(), e.get_path()).into_boxed_str());
            }
            for u in &result.missing {
                err_msgs.push(format!("Device Config missing: {}", u).into_boxed_str());
            }
            // The ordering in which valico emits these errors is unstable. Sort error messages so
            // that the resulting message is predictable.
            err_msgs.sort_unstable();
            return Err(error::NetworkManager::CONFIG(error::Config::FailedToValidateConfig {
                path: String::from(self.startup_path().to_string_lossy()),
                error: err_msgs.join(", "),
            }));
        }
        Ok(())
    }

    /// Validates an [`config::Interface`]'s configuration.
    ///
    /// An Interface definition is valid if it has exactly one configuration type (e.g.
    /// subinterface, switched_vlan, or routed_vlan).
    fn validate_interface_config(&self, intf: &Interface) -> error::Result<()> {
        // TODO(cgibson): Try using serde's 'externally tagged' enum representation to remove
        // this validation step: https://serde.rs/enum-representations.html
        if intf.subinterfaces.is_some()
            && intf.switched_vlan.is_none()
            && intf.routed_vlan.is_none()
        {
            return Ok(());
        } else if intf.switched_vlan.is_some()
            && intf.subinterfaces.is_none()
            && intf.routed_vlan.is_none()
        {
            return Ok(());
        } else if intf.routed_vlan.is_some()
            && intf.subinterfaces.is_none()
            && intf.switched_vlan.is_none()
        {
            return Ok(());
        }
        Err(error::NetworkManager::CONFIG(error::Config::FailedToValidateConfig {
            path: String::from(self.startup_path().to_string_lossy()),
            error: concat!(
                "Interface must be exactly one of either: ",
                "'subinterfaces', 'routed_vlan', or 'switched_vlan'"
            )
            .to_string(),
        }))
    }

    /// Validates a [`config::InterfaceType`].
    ///
    /// If an Interface's type is [`InterfaceType::IfUplink`], then the Interface must have a
    /// [`Interface::Subinterfaces`] definition.
    fn validate_interface_types(&self, intf: &Interface) -> error::Result<()> {
        match intf.config.interface_type {
            InterfaceType::IfUplink => {
                if intf.subinterfaces.is_none() {
                    return Err(error::NetworkManager::CONFIG(
                        error::Config::FailedToValidateConfig {
                            path: String::from(self.startup_path().to_string_lossy()),
                            error:
                                "Interface type is 'IF_UPLINK' but does not define a 'subinterface'"
                                    .to_string(),
                        },
                    ));
                }
                if let Some(subifs) = intf.subinterfaces.as_ref() {
                    self.validate_subinterfaces(subifs)?;
                }
            }
            InterfaceType::IfRoutedVlan => {
                if intf.routed_vlan.is_none() {
                    return Err(error::NetworkManager::CONFIG(
                        error::Config::FailedToValidateConfig {
                            path: String::from(self.startup_path().to_string_lossy()),
                            error: concat!(
                                "Interface type is 'IF_ROUTED_VLAN' but does ",
                                "not define a 'routed_vlan'"
                            )
                            .to_string(),
                        },
                    ));
                }
            }
            // Add additional type validation here.
            _ => return Ok(()),
        }
        Ok(())
    }

    /// Validates an [`config::IpAddress`].
    fn validate_ip_address(&self, addr: &IpAddress) -> error::Result<()> {
        let has_static = addr.ip.is_some() || addr.prefix_length.is_some();
        let valid_static = !(addr.ip.is_some() ^ addr.prefix_length.is_some());
        let dhcp = addr.dhcp_client.unwrap_or(false);
        let valid_xor = dhcp ^ has_static;
        if valid_xor && valid_static {
            Ok(())
        } else {
            Err(error::NetworkManager::CONFIG(error::Config::Malformed {
                msg: format!("Invalid IpAddress configuration: {:?}", addr),
            }))
        }
    }

    /// Validates each [`config::Subinterface`].
    fn validate_subinterfaces(&self, subinterfaces: &Vec<Subinterface>) -> error::Result<()> {
        for subif in subinterfaces.into_iter() {
            if let Some(v4addr) = &subif.ipv4 {
                for a in v4addr.addresses.iter() {
                    self.validate_ip_address(&a)?;
                }
            }
            if let Some(v6addr) = &subif.ipv6 {
                for a in v6addr.addresses.iter() {
                    self.validate_ip_address(&a)?;
                }
            }
        }
        Ok(())
    }

    /// Validates an [`config::Interface`] configuration.
    fn validate_interface(&self, intf: &Interface) -> error::Result<()> {
        self.validate_interface_config(&intf)?;
        self.validate_interface_types(&intf)?;
        Ok(())
    }

    /// Performs an additional layer of validation checks that cannot be expressed in JSON Schema.
    async fn final_validation(&self) -> error::Result<()> {
        let mut intf_names = HashSet::new();
        let intfs = match self.interfaces() {
            Some(intfs) => intfs,
            None => {
                return Err(error::NetworkManager::CONFIG(error::Config::NotFound {
                    msg: format!("Config contains no interfaces"),
                }));
            }
        };
        for intfs in intfs.iter() {
            self.validate_interface(&intfs.interface)?;

            // Interface names must be unique.
            if intf_names.contains(&intfs.interface.config.name) {
                return Err(error::NetworkManager::CONFIG(error::Config::FailedToValidateConfig {
                    path: String::from(self.startup_path().to_string_lossy()),
                    error: format!(
                        "Duplicate interface names detected: '{}'",
                        intfs.interface.config.name
                    ),
                }));
            }
            intf_names.insert(intfs.interface.config.name.clone());
        }
        Ok(())
    }

    /// Returns [`config::Device`] from the config.
    pub fn device(&self) -> error::Result<&Device> {
        self.device_config.as_ref().map(|c| &c.device).ok_or_else(|| {
            error::NetworkManager::CONFIG(error::Config::NotFound {
                msg: format!("Device was not found yet. Is the config loaded?"),
            })
        })
    }

    /// Returns all the configured [`config::Interfaces`].
    pub fn interfaces(&self) -> Option<&Vec<Interfaces>> {
        self.device().ok().and_then(|x| x.interfaces.as_ref())
    }

    /// Returns the [`config::Interface`] that matches the device_id contained in `topo_path`.
    pub fn get_interface_by_device_id(&self, topo_path: &str) -> Option<&Interface> {
        if let Some(ifs) = self.interfaces() {
            for intfs in ifs.iter() {
                if let Some(d) = &intfs.interface.device_id {
                    if topo_path.contains(d.as_str()) {
                        return Some(&intfs.interface);
                    }
                }
            }
        }
        None
    }

    /// Returns `true` if the device id from the `topo_path` is an uplink.
    ///
    /// An "uplink" is defined by having an [`InterfaceType::IfUplink`] and having a "subinterface"
    /// definition.
    pub fn device_id_is_a_wan_uplink(&self, topo_path: &str) -> bool {
        if let Some(intf) = self.get_interface_by_device_id(topo_path) {
            match intf.config.interface_type {
                InterfaceType::IfUplink => return true,
                _ => return false,
            }
        }
        false
    }

    /// Returns a tuple of IPv4 and IPv6 [`config::IpAddress`]'s for this interface.
    ///
    /// The 'switched_vlan' configuration does not support IP addressing, so any `Interface` that
    /// has a 'switched_vlan' configuration will return `None`.
    pub fn get_ip_address<'a>(
        &self,
        intf: &'a Interface,
    ) -> error::Result<(Option<&'a IpAddress>, Option<&'a IpAddress>)> {
        if let Some(subifs) = &intf.subinterfaces {
            if subifs.is_empty() {
                return Err(error::NetworkManager::CONFIG(error::Config::Malformed {
                    msg: format!("Interface must have at least one 'subinterface' definition"),
                }));
            }
            // TODO(cgibson): LIFProperties doesn't support vectors of IP addresses yet. fxb/42315.
            if subifs.len() != 1 {
                warn!("LIFProperties does not support multiple addresses yet.")
            }
            let subif = &subifs[0];
            let v4addr = subif.ipv4.as_ref().and_then(|c| c.addresses.iter().nth(0));
            let v6addr = subif.ipv6.as_ref().and_then(|c| c.addresses.iter().nth(0));
            return Ok((v4addr, v6addr));
        }

        // TODO(cgibson): Add support for getting IP addreses from 'routed_vlan'.
        Err(error::NetworkManager::CONFIG(error::Config::NotSupported {
            msg: format!("Getting IP addresses from a 'routed_vlan' is not supported yet"),
        }))
    }

    /// Updates [`lifmgr::LIFProperties`] with the given IP address configuration.
    ///
    /// If `ipconfig` is `None` then this method has no effect.
    fn set_ip_address_config(
        &self,
        properties: &mut lifmgr::LIFProperties,
        ipconfig: &Option<&IpAddress>,
    ) {
        if let Some(c) = ipconfig {
            if let Some(dhcp_client) = c.dhcp_client {
                properties.dhcp = dhcp_client;
            }
            // TODO(cgibson): LIFProperties doesn't support IPv6 addresses yet. fxb/42316.
            match (c.ip, c.prefix_length) {
                (Some(address), Some(prefix)) => {
                    properties.address = Some(lifmgr::LifIpAddr { address, prefix });
                }
                _ => {}
            }
        }
    }

    /// Returns a WAN-specific [`lifmgr::LIFProperties`] based on the running configuration.
    ///
    /// Configures the WAN uplink's LIFProperties, initially discovers the interface that contains
    /// matches the device ID from the topological path. Then sets the LIFProperties for the admin
    /// state of the interface as well as configures the IP address.
    pub fn create_wan_properties(&self, topo_path: &str) -> error::Result<lifmgr::LIFProperties> {
        let mut properties =
            crate::lifmgr::LIFProperties { enabled: true, dhcp: true, address: None };
        let intf = match self.get_interface_by_device_id(topo_path) {
            Some(x) => x,
            None => {
                return Err(error::NetworkManager::CONFIG(error::Config::Malformed {
                    msg: format!(
                        "Cannot find an Interface matching `device_id` from topo: {}",
                        topo_path
                    ),
                }))
            }
        };
        let subifs = match &intf.subinterfaces {
            Some(subif) => subif,
            None => {
                return Err(error::NetworkManager::CONFIG(error::Config::Malformed {
                    msg: format!("An uplink must have a 'subinterface' configuration"),
                }))
            }
        };
        if subifs.len() != 1 {
            return Err(error::NetworkManager::CONFIG(error::Config::NotSupported {
                msg: format!("Multiple subinterfaces on a single interface are not supported"),
            }));
        }
        if let Some(subif) = subifs.get(0) {
            match subif.admin_state {
                Some(AdminState::Up) => {
                    properties.enabled = true;
                }
                Some(AdminState::Down) => {
                    warn!("WAN subinterface is admin down by config");
                    properties.enabled = false;
                }
                Some(AdminState::Testing) => {
                    warn!("Admin state 'TESTING' is not supported");
                    properties.enabled = true;
                }
                _ => {
                    // Subinterface's are enabled by default.
                    properties.enabled = true;
                }
            }
        }

        let (v4addr, v6addr) = self.get_ip_address(intf)?;
        // TODO(cgibson): LIFProperties doesn't support IPv6 addresses yet. fxb/42316.
        if v6addr.is_some() {
            warn!("Setting IPv6 addresses is not supported yet.");
        }
        self.set_ip_address_config(&mut properties, &v4addr);
        Ok(properties)
    }

    /// Returns configured VLAN IDs for the given device that matches `topo_path`.
    ///
    /// If the interface is a `switched_vlan` then the VLAN IDs will be depend on if the interface
    /// mode is either a trunk or access.
    ///
    /// If the interface is a `routed_vlan` then the VLAN ID will be a vector with a single element
    /// containing the `routed_vlan`'s VLAN ID.
    ///
    /// If the interface is a `subinterface` then the vector will be empty.
    pub fn get_vlans(&self, topo_path: &str) -> Vec<u16> {
        let mut vids = Vec::new();
        if let Some(intf) = self.get_interface_by_device_id(topo_path) {
            match &intf.routed_vlan {
                Some(r) => vids.push(r.vlan_id),
                None => {}
            }
            match intf.switched_vlan.as_ref().and_then(|x| x.interface_mode.as_ref()) {
                Some(InterfaceMode::Access) => {
                    if let Some(access_vlan) =
                        intf.switched_vlan.as_ref().and_then(|x| x.access_vlan)
                    {
                        vids.push(access_vlan);
                    }
                }
                Some(InterfaceMode::Trunk) => {
                    if let Some(trunk_vlans) =
                        intf.switched_vlan.as_ref().and_then(|x| x.trunk_vlans.as_ref())
                    {
                        vids.extend(trunk_vlans.iter().cloned());
                    }
                }
                None => (),
            }
        }
        vids
    }

    /// Returns the [`config::Services`] definition.
    ///
    /// # Errors
    ///
    /// If there is no "services" definition in the configuration, returns a
    /// [`error::Config::NotFound`] error.
    fn get_services(&self) -> error::Result<&Services> {
        self.device()?.services.as_ref().ok_or(error::NetworkManager::CONFIG(
            error::Config::NotFound { msg: format!("'services' definition was not found") },
        ))
    }

    /// Returns the IP forwarding configuration.
    ///
    /// If IP forwarding is enabled in the config, then this method will return true.
    pub fn get_ip_forwarding_state(&self) -> bool {
        self.get_services()
            .ok()
            .and_then(|x| x.ip_forwarding.as_ref())
            .map(|i| i.enabled)
            .unwrap_or(false)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::error::NetworkManager::CONFIG;
    use fuchsia_async as fasync;
    use std::fs;

    fn create_test_config_no_paths() -> Config {
        Config::new("/user", "/factory", "/device_schema")
    }

    fn create_test_config(user_cfg: &str, factory_cfg: &str, device_schema: &str) -> Config {
        Config::new(user_cfg, factory_cfg, device_schema)
    }

    fn create_test_interface() -> Interface {
        Interface {
            config: InterfaceConfig {
                name: "test_eth".to_string(),
                interface_type: InterfaceType::IfEthernet,
            },
            ethernet: None,
            tcp_offload: None,
            device_id: Some("device_id".to_string()),
            oper_state: Some(OperState::Up),
            subinterfaces: Some(vec![Subinterface {
                admin_state: Some(AdminState::Up),
                ipv4: Some(IpAddressConfig {
                    addresses: vec![IpAddress {
                        dhcp_client: Some(true),
                        ip: None,
                        prefix_length: None,
                    }],
                }),
                ipv6: None,
            }]),
            switched_vlan: None,
            routed_vlan: None,
        }
    }

    fn build_full_config() -> DeviceConfig {
        DeviceConfig {
            device: Device {
                acls: None,
                interfaces: Some(vec![
                    Interfaces {
                        interface: Interface {
                            config: InterfaceConfig {
                                name: "test_wan".to_string(),
                                interface_type: InterfaceType::IfUplink,
                            },
                            oper_state: None,
                            device_id: Some("test_device_id".to_string()),
                            ethernet: None,
                            tcp_offload: None,
                            routed_vlan: None,
                            switched_vlan: None,
                            subinterfaces: Some(vec![Subinterface {
                                admin_state: Some(AdminState::Up),
                                ipv4: Some(IpAddressConfig {
                                    addresses: vec![IpAddress {
                                        dhcp_client: Some(true),
                                        ip: Some("127.0.0.1".parse().unwrap()),
                                        prefix_length: Some(32),
                                    }],
                                }),
                                ipv6: None,
                            }]),
                        },
                    },
                    Interfaces {
                        interface: Interface {
                            config: InterfaceConfig {
                                name: "test_eth0".to_string(),
                                interface_type: InterfaceType::IfEthernet,
                            },
                            oper_state: None,
                            device_id: Some("routed_vlan".to_string()),
                            ethernet: None,
                            tcp_offload: None,
                            subinterfaces: None,
                            switched_vlan: None,
                            routed_vlan: Some(RoutedVlan { vlan_id: 1, ipv4: None, ipv6: None }),
                        },
                    },
                ]),
                services: Some(Services {
                    dhcp_server: None,
                    ip_forwarding: Some(IpForwarding { enabled: true }),
                }),
            },
        }
    }

    #[test]
    fn test_new() {
        let user_cfg = "/data/my/user.cfg";
        let factory_cfg = "/data/my/factory.cfg";
        let device_schema = "/data/my/device_schema.cfg";
        let test_config = create_test_config(user_cfg, factory_cfg, device_schema);

        assert_eq!(test_config.device_config.is_none(), true);
        assert_eq!(test_config.paths.user_config_path, Path::new(user_cfg).to_path_buf());
        assert_eq!(test_config.paths.factory_config_path, Path::new(factory_cfg).to_path_buf());
        assert_eq!(test_config.paths.device_schema_path, Path::new(device_schema).to_path_buf());
    }

    #[test]
    fn test_get_paths() {
        let user_cfg = "/data/my/user.cfg";
        let factory_cfg = "/data/my/factory.cfg";
        let device_schema = "/data/my/device_schema.cfg";
        let test_config = create_test_config(user_cfg, factory_cfg, device_schema);

        assert_eq!(test_config.device_config.is_none(), true);
        assert_eq!(test_config.user_config_path(), Path::new(user_cfg));
        assert_eq!(test_config.factory_config_path(), Path::new(factory_cfg));
        assert_eq!(test_config.device_schema_path(), Path::new(device_schema));
        assert_eq!(test_config.startup_path(), Path::new(""));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_config_file() {
        let test_config = create_test_config_no_paths();

        // Missing config should raise an `error::Config::ConfigNotFound`.
        let doesntexist = String::from("/doesntexist");
        match test_config.try_load_config(Path::new(&doesntexist)).await {
            Err(CONFIG(error::Config::ConfigNotFound { path })) => {
                assert_eq!(doesntexist, path);
            }
            Ok(r) => panic!("Got unexpected 'Ok' result: {}", r),
            Err(e) => panic!("Got unexpected error result: {}", e),
        }

        // An invalid config should fail to deserialize.
        let invalid_empty = String::from("/pkg/data/invalid_empty.json");
        match test_config.try_load_config(Path::new(&invalid_empty)).await {
            Err(CONFIG(error::Config::FailedToDeserializeConfig { path, error: _ })) => {
                assert_eq!(invalid_empty, path);
            }
            Ok(r) => panic!("Got unexpected 'Ok' result: {}", r),
            Err(e) => panic!("Got unexpected error result: {}", e),
        }

        // A valid config should deserialize successfully.
        let valid_empty = String::from("/pkg/data/valid_empty.json");
        let contents = fs::read_to_string(&valid_empty)
            .expect(format!("Failed to open testdata file: {}", valid_empty).as_str());

        // The expected configuration should deserialize successfully.
        let expected_config: Value;
        match serde_json::from_str(&contents) {
            Ok(j) => expected_config = j,
            Err(e) => panic!("Got unexpected error result: {}", e),
        }

        // The serde_json::Value's should match.
        match test_config.try_load_config(Path::new(&valid_empty)).await {
            Ok(j) => {
                assert_eq!(expected_config, j);
            }
            Err(e) => panic!("Got unexpected error result: {}", e),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_validate_schema() {
        let test_config =
            create_test_config("/doesntmatter", "/doesntmatter", "/pkg/data/device_schema.json");

        let valid_config = r#"{
          "device": {
            "interfaces": [
              {
                "interface": {
                  "config": {
                    "name": "wan",
                    "type": "IF_UPLINK"
                  },
                  "subinterfaces": [
                      {
                        "ipv4": {
                          "addresses": [
                            {
                              "dhcp_client": false,
                              "ip": "1.1.1.1",
                              "prefix_length": 32
                            }
                          ]
                        }
                      }
                  ],
                  "device_id": "my_test_device"
                }
              }
            ]
          }
        }"#;
        let expected_config: Value;
        match serde_json::from_str(&valid_config) {
            Ok(j) => expected_config = j,
            Err(e) => panic!("Got unexpected error result: {}", e),
        }

        // Make sure that the configuration actually validates.
        match test_config.validate_with_schema(&expected_config).await {
            Ok(_) => (),
            Err(e) => panic!("Got unexpected error result: {}", e),
        }
    }

    #[test]
    fn verify_toulouse_factory_config() {
        let config_path = "/pkg/data/toulouse_factory_config.json";
        let mut contents = String::new();
        let mut f = File::open(config_path).unwrap();
        f.read_to_string(&mut contents).unwrap();
        let _deserialized_config: DeviceConfig = serde_json::from_str(&contents)
            .expect(format!("Failed to deserialized {}", config_path).as_ref());
    }

    #[test]
    fn test_validate_interface_types() {
        let test_config = create_test_config_no_paths();
        let mut intf = create_test_interface();

        // If the Interface type is `InterfaceType::IfUplink`, then there must be a 'subinterface'
        // defined.
        intf.config =
            InterfaceConfig { name: "test".to_string(), interface_type: InterfaceType::IfUplink };
        match test_config.validate_interface_types(&intf) {
            Ok(_) => (),
            Err(e) => panic!("Got unexpected error result: {:?}", e),
        }

        // If the Interface type is `InterfaceType::IfRoutedVlan`, then there must be a
        // 'routed_vlan' defined. Anything else should fail.
        intf.config = InterfaceConfig {
            name: "test".to_string(),
            interface_type: InterfaceType::IfRoutedVlan,
        };
        match test_config.validate_interface_types(&intf) {
            Ok(_) => panic!("Got unexpected 'ok' result"),
            Err(_) => (),
        }
    }

    #[test]
    fn test_validate_interface_config() {
        let fake_factory_path = "/fake_factory_path";
        let test_config = create_test_config("/doesntmatter", fake_factory_path, "/doesntmatter");
        let mut intf = create_test_interface();

        // Should pass because an interface must have exactly one configuration.
        match test_config.validate_interface_config(&intf) {
            Ok(_) => (),
            Err(e) => panic!("Got unexpected error result: {:?}", e),
        }

        // The following should all fail for the same reason as above, exactly one configuration
        // per interface.
        let routed_vlan = Some(RoutedVlan { vlan_id: 1, ipv4: None, ipv6: None });
        let switched_vlan =
            Some(SwitchedVlan { interface_mode: None, access_vlan: None, trunk_vlans: None });
        let subinterfaces = Subinterface { admin_state: None, ipv4: None, ipv6: None };

        intf.routed_vlan = routed_vlan.clone();
        intf.subinterfaces = Some(vec![subinterfaces.clone()]);
        intf.switched_vlan = switched_vlan.clone();
        let r = test_config.validate_interface_config(&intf);
        assert_eq!(true, r.is_err());

        intf.routed_vlan = None;
        intf.subinterfaces = Some(vec![subinterfaces.clone()]);
        intf.switched_vlan = switched_vlan.clone();
        let r = test_config.validate_interface_config(&intf);
        assert_eq!(true, r.is_err());

        intf.routed_vlan = routed_vlan.clone();
        intf.subinterfaces = None;
        intf.switched_vlan = switched_vlan.clone();
        let r = test_config.validate_interface_config(&intf);
        assert_eq!(true, r.is_err());

        intf.routed_vlan = routed_vlan.clone();
        intf.subinterfaces = Some(vec![subinterfaces.clone()]);
        intf.switched_vlan = None;
        let r = test_config.validate_interface_config(&intf);
        assert_eq!(true, r.is_err());

        intf.routed_vlan = None;
        intf.subinterfaces = None;
        intf.switched_vlan = None;
        let r = test_config.validate_interface_config(&intf);
        assert_eq!(true, r.is_err());
    }

    #[test]
    fn test_validate_subinterfaces() {
        let fake_factory_path = "/fake_factory_path";
        let test_config = create_test_config("/doesntmatter", fake_factory_path, "/doesntmatter");

        // Should not fail because `dhcp_client` is set to true.
        let test_subif = vec![Subinterface {
            admin_state: Some(AdminState::Up),
            ipv4: Some(IpAddressConfig {
                addresses: vec![IpAddress {
                    dhcp_client: Some(true),
                    ip: None,
                    prefix_length: None,
                }],
            }),
            ipv6: None,
        }];
        match test_config.validate_subinterfaces(&test_subif) {
            Ok(_) => (),
            Err(e) => panic!("Got unexpected error result: {}", e),
        }

        // Should not fail with dhcp_client set to false, ip and prefix set to valid values.
        let test_subif = vec![Subinterface {
            admin_state: Some(AdminState::Up),
            ipv4: Some(IpAddressConfig {
                addresses: vec![IpAddress {
                    dhcp_client: Some(false),
                    ip: Some("127.0.0.1".parse().unwrap()),
                    prefix_length: Some(32),
                }],
            }),
            ipv6: None,
        }];
        match test_config.validate_subinterfaces(&test_subif) {
            Ok(_) => (),
            Err(e) => panic!("Got unexpected error result: {}", e),
        }

        // Should fail with dhcp_client set to false, ip and prefix set to None.
        let test_subif = vec![Subinterface {
            admin_state: Some(AdminState::Up),
            ipv4: Some(IpAddressConfig {
                addresses: vec![IpAddress {
                    dhcp_client: Some(false),
                    ip: None,
                    prefix_length: None,
                }],
            }),
            ipv6: None,
        }];
        match test_config.validate_subinterfaces(&test_subif) {
            Err(CONFIG(error::Config::Malformed { msg: _ })) => (),
            Err(e) => panic!("Got unexpected error result: {}", e),
            Ok(_) => panic!("Got unexpected 'Ok' result!"),
        }

        // Should not fail with dhcp client set to None, ip, and prefx set to valid values.
        let test_subif = vec![Subinterface {
            admin_state: Some(AdminState::Up),
            ipv4: Some(IpAddressConfig {
                addresses: vec![IpAddress {
                    dhcp_client: None,
                    ip: Some("127.0.0.1".parse().unwrap()),
                    prefix_length: Some(32),
                }],
            }),
            ipv6: None,
        }];
        match test_config.validate_subinterfaces(&test_subif) {
            Ok(_) => (),
            Err(e) => panic!("Got unexpected error result: {}", e),
        }

        // Should fail because both dhcp_client and ip/prefix_len are None.
        let test_subif = vec![Subinterface {
            admin_state: Some(AdminState::Up),
            ipv4: Some(IpAddressConfig {
                addresses: vec![IpAddress { dhcp_client: None, ip: None, prefix_length: None }],
            }),
            ipv6: None,
        }];
        match test_config.validate_subinterfaces(&test_subif) {
            Err(CONFIG(error::Config::Malformed { msg: _ })) => (),
            Err(e) => panic!("Got unexpected error result: {}", e),
            Ok(_) => panic!("Got unexpected 'Ok' result!"),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_final_validation() {
        let mut test_config =
            create_test_config("/user", "/factory", "/pkg/data/device_schema.json");

        let valid_config = r#"{
          "device": {
            "interfaces": [
              {
                "interface": {
                  "config": {
                    "name": "wan",
                    "type": "IF_UPLINK"
                  },
                  "subinterfaces": [
                    {
                      "ipv4": {
                        "addresses": [
                          {
                            "dhcp_client": false,
                            "ip": "1.1.1.1",
                            "prefix_length": 32
                          }
                        ]
                      }
                    }
                  ],
                  "device_id": "my_test_device"
                }
              }
            ]
          }
        }"#;
        match serde_json::from_str(&valid_config) {
            Ok(j) => test_config.device_config = j,
            Err(e) => panic!("Got unexpected error result: {}", e),
        }
        match test_config.final_validation().await {
            Ok(()) => (),
            Err(e) => panic!("Got unexpected error result: {:?}", e),
        }

        let invalid_config = r#"{
          "device": {
            "interfaces": [
              {
                "interface": {
                  "config": {
                    "name": "wan",
                    "type": "IF_UPLINK"
                  },
                  "subinterfaces": [
                    {
                      "ipv4": {
                        "addresses": [
                          {
                            "dhcp_client": false,
                            "ip": "1.1.1.1",
                            "prefix_length": 32
                          }
                        ]
                      }
                    }
                  ],
                  "device_id": "my_test_device"
                }
              },
              {
                "interface": {
                  "config": {
                    "name": "wan",
                    "type": "IF_UPLINK"
                  },
                  "routed_vlan": {
                    "vlan_id": 1
                  },
                  "subinterfaces": [
                    {
                      "ipv4": {
                        "addresses": [
                          {
                            "dhcp_client": false,
                            "ip": "1.1.1.1",
                            "prefix_length": 32
                          }
                        ]
                      }
                    }
                  ],
                  "device_id": "my_test_device"
                }
              }
            ]
          }
        }"#;
        match serde_json::from_str(&invalid_config) {
            Ok(j) => test_config.device_config = j,
            Err(e) => panic!("Got unexpected error result: {}", e),
        }
        match test_config.final_validation().await {
            Ok(_) => panic!("Got unexpected 'ok' result"),
            Err(CONFIG(error::Config::FailedToValidateConfig { path: _, error: _ })) => (),
            Err(e) => panic!("Got unexpected error result: {}", e),
        }
    }

    #[test]
    fn test_get_device() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        match test_config.device() {
            Ok(d) => {
                assert_eq!(*d, build_full_config().device);
            }
            Err(e) => panic!("Got unexpected error result: {}", e),
        }
    }

    #[test]
    fn test_get_interfaces() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        match test_config.interfaces() {
            Some(i) => {
                assert_eq!(*i, build_full_config().device.interfaces.unwrap());
            }
            None => panic!("Got unexpected 'None' option"),
        }
    }

    #[test]
    fn test_get_interface_by_device_id() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        match test_config.get_interface_by_device_id("/dev/sys/pci/test_device_id/ethernet") {
            Some(i) => {
                let intfs = &build_full_config().device.interfaces.unwrap()[0];
                assert_eq!(*i, intfs.interface);
            }
            None => panic!("Got unexpected 'None' option"),
        }

        match test_config.get_interface_by_device_id("does_not_exist") {
            Some(_) => panic!("Got unexpected 'None' option"),
            None => (),
        }
    }

    #[test]
    fn test_device_id_by_wan_uplink() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        match test_config.device_id_is_a_wan_uplink("/dev/sys/pci/test_device_id/ethernet") {
            true => (),
            false => panic!("Got unexpected 'false' value"),
        }

        match test_config.device_id_is_a_wan_uplink("does_not_exist") {
            true => panic!("Got unexpected 'false' value"),
            false => (),
        }
    }

    #[test]
    fn test_get_vlans() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        let v = test_config.get_vlans("routed_vlan");
        assert_eq!(v, vec![1]);

        let v = test_config.get_vlans("test_device_id");
        let empty_vec: Vec<u16> = Vec::new();
        assert_eq!(v, empty_vec);
    }

    #[test]
    fn test_get_services() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        let services = test_config.get_services();
        assert_eq!(*services.unwrap(), build_full_config().device.services.unwrap());
    }

    #[test]
    fn test_get_ip_forwarding() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        assert_eq!(test_config.get_ip_forwarding_state(), true);

        // removing the ip forwarding config should still return the default of false.
        test_config.device_config =
            Some(DeviceConfig { device: Device { acls: None, interfaces: None, services: None } });
        assert_eq!(test_config.get_ip_forwarding_state(), false);
    }

    #[test]
    fn test_set_ip_address() {
        let test_config = create_test_config_no_paths();
        let ipconfig = IpAddress {
            dhcp_client: Some(true),
            ip: Some("127.0.0.1".parse().unwrap()),
            prefix_length: Some(32),
        };
        let mut properties: lifmgr::LIFProperties =
            lifmgr::LIFProperties { enabled: true, dhcp: false, address: None };
        test_config.set_ip_address_config(&mut properties, &Some(&ipconfig));
        assert_eq!(properties.dhcp, true);
        assert_eq!(
            properties.address,
            Some(lifmgr::LifIpAddr { address: "127.0.0.1".parse().unwrap(), prefix: 32 })
        );
    }

    #[test]
    fn test_get_ip_address() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        let intfs = build_full_config().device.interfaces.unwrap();
        let subif = &intfs[0].interface.subinterfaces.as_ref().unwrap()[0];
        let expected_addr = &subif.ipv4.as_ref().unwrap().addresses[0];
        match test_config.get_ip_address(&intfs[0].interface) {
            Ok((Some(v4addr), None)) => {
                assert_eq!(*v4addr, *expected_addr);
            }
            Ok(e) => panic!("Got unexpected result pair: {:?}", e),
            Err(e) => panic!("Got unexpected result pair: {:?}", e),
        }
    }

    #[test]
    fn test_create_wan_properties() {
        let mut test_config = create_test_config_no_paths();
        test_config.device_config = Some(build_full_config());
        match test_config.create_wan_properties("test_device_id") {
            Ok(p) => {
                assert_eq!(p.enabled, true);
                assert_eq!(p.dhcp, true);
                assert_eq!(
                    p.address,
                    Some(lifmgr::LifIpAddr { address: "127.0.0.1".parse().unwrap(), prefix: 32 })
                );
            }
            Err(e) => panic!("Got unexpected result pair: {:?}", e),
        }
    }
}
