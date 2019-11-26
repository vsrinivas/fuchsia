// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error;
use serde_derive::{Deserialize, Serialize};
use serde_json::Value;
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

// TODO(cgibson): VLANs and IRBs.
// TODO(cgibson): ACLs.
// TODO(cgibson): WLAN.
// TODO(cgibson): Need to figure out versioning. Having "unknown" fields and "flatten"'ing themn
// into an `extras` field might be an interesting experiment.
#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct DeviceConfig {
    pub device: Option<Device>,
}

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Device {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub interfaces: Option<Vec<Interfaces>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub acls: Option<Acls>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub services: Option<Services>,
}

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Interfaces {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub interface: Option<Interface>,
}

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Interface {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub config: Option<InterfaceConfig>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub oper_state: Option<OperState>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub subinterfaces: Option<Vec<Subinterfaces>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub switched_vlan: Option<SwitchedVlan>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub routed_vlan: Option<RoutedVlan>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ethernet: Option<Ethernet>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tcp_offload: Option<bool>,
}

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct InterfaceConfig {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub r#type: Option<InterfaceType>,
}

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Subinterfaces {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub subinterface: Option<Subinterface>,
}

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Subinterface {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub config: Option<SubinterfaceConfig>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ipv4: Option<IpAddressConfig>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ipv6: Option<IpAddressConfig>,
}

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct SubinterfaceConfig {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub admin_state: Option<AdminState>,
}

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct IpAddressConfig {
    pub addresses: Option<Vec<IpAddress>>,
}

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
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

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct SwitchedVlan {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub interface_mode: Option<InterfaceMode>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub access_vlan: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub trunk_vlans: Option<Vec<u32>>,
}

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct RoutedVlan {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub vlan_id: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ipv4: Option<IpAddressConfig>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ipv6: Option<IpAddressConfig>,
}

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Ethernet {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub config: Option<EthernetConfig>,
}

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
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

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Acls {}

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Services {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dhcp_server: Option<DhcpServer>,
}

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct DhcpServer {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dhcp_pool: Option<DhcpPool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub static_ip_allocations: Option<Vec<StaticIpAllocations>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub interfaces: Option<String>,
}

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct DhcpPool {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub start: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub end: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub lease_time: Option<String>,
}

#[derive(Serialize, Deserialize, Debug, Default, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct StaticIpAllocations {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub device_name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub mac_address: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub ip_address: Option<String>,
}

#[derive(Debug, PartialEq)]
struct DeviceConfigPaths {
    user_config_path: PathBuf,
    factory_config_path: PathBuf,
    device_schema_path: PathBuf,
}

/// Converts a Valico JSON SchemaError to a string.
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

#[derive(Debug, PartialEq)]
pub struct Config {
    config: Option<DeviceConfig>,
    startup_path: Option<PathBuf>,
    paths: DeviceConfigPaths,
}

impl Config {
    /// Creates a new Config object with the given user, factory, and device schema paths.
    pub fn new<P: Into<PathBuf>>(user_path: P, factory_path: P, device_schema: P) -> Config {
        Config {
            config: None,
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
    /// If a configuration has not been read yet then returns None.
    pub fn startup_path(&self) -> Option<&Path> {
        if let Some(p) = &self.startup_path {
            return Some(p.as_path());
        }
        None
    }

    /// Loads the relevant configuration file.
    ///
    /// This method tries to load the user configuration file. If the user config file does not
    /// exist yet (e.g. OOBE, FDR, etc), fallback to trying to load the factory configuration
    /// file.
    ///
    /// If this method successfully returns, then there should now be a validated configuration
    /// available.
    pub async fn load_config(&mut self) -> error::Result<()> {
        let loaded_config;
        let loaded_path;
        match self.try_load_config(&self.user_config_path()) {
            Ok(c) => {
                loaded_config = c;
                loaded_path = self.paths.user_config_path.to_path_buf();
            }
            Err(e) => {
                warn!("Failed to load user config: {}", e);
                loaded_config = self.try_load_config(&self.factory_config_path())?;
                loaded_path = self.paths.factory_config_path.to_path_buf();
            }
        }
        match self.is_valid_config(&loaded_config) {
            Ok(_) => {
                self.config = Some(serde_json::from_value(loaded_config).map_err(|e| {
                    error::NetworkManager::CONFIG(error::Config::FailedToDeserializeConfig {
                        path: String::from(loaded_path.to_string_lossy()),
                        error: e.to_string(),
                    })
                })?);
                self.startup_path = Some(loaded_path);
                Ok(())
            }
            Err(e) => {
                error!("Config validation failed: {}", e);
                Err(e)
            }
        }
    }

    /// Tries to load the given configuration file.
    fn try_load_config(&self, config_path: &Path) -> error::Result<Value> {
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

    /// Validates an in-memory deserialized configuration.
    fn is_valid_config(&self, config: &Value) -> error::Result<()> {
        info!("Validating config against the device schema");
        let device_schema = self.try_load_config(&self.device_schema_path())?;
        let mut scope = json_schema::Scope::new();
        let compiled_device_schema =
            scope.compile_and_return(device_schema, false).map_err(|e| {
                error!("Failed to validate schema: {:?}", e);
                error::NetworkManager::CONFIG(error::Config::FailedToValidateConfig {
                    path: String::from(self.device_schema_path().to_string_lossy()),
                    error: schema_error(e),
                })
            })?;
        let result = compiled_device_schema.validate(config);
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
                path: String::from(self.device_schema_path().to_string_lossy()),
                error: err_msgs.join(", "),
            }));
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::error::NetworkManager::CONFIG;
    use std::fs;

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
        assert_eq!(test_config.startup_path(), None);
    }

    #[test]
    fn test_try_load_config() {
        let test_config = Config::new("/test", "/test", "/test");

        // Missing config should raise ConfigNotFound.
        let doesntexist = String::from("/doesntexist");
        match test_config.try_load_config(Path::new(&doesntexist)) {
            Err(CONFIG(error::Config::ConfigNotFound { path })) => {
                assert_eq!(doesntexist, path);
            }
            Ok(r) => panic!("Unexpected 'Ok' result: {}", r),
            Err(e) => panic!("Got unexpected error result: {}", e),
        }

        // An invalid config should fail to deserialize.
        let invalid_empty = String::from("/pkg/data/invalid_empty.json");
        match test_config.try_load_config(Path::new(&invalid_empty)) {
            Err(CONFIG(error::Config::FailedToDeserializeConfig { path, error: _ })) => {
                assert_eq!(invalid_empty, path);
            }
            Ok(r) => panic!("Unexpected 'Ok' result: {}", r),
            Err(e) => panic!("Got unexpected error result: {}", e),
        }

        // A valid config should deserialize successfully.
        let valid_empty = String::from("/pkg/data/valid_empty.json");
        let contents = fs::read_to_string(&valid_empty)
            .expect(format!("Failed to open testdata file: {}", valid_empty).as_str());

        let expected_config: Value;
        match serde_json::from_str(&contents) {
            Ok(j) => expected_config = j,
            Err(e) => panic!("Got unexpected error result: {}", e),
        }

        // The serde_json::Value's should match.
        match test_config.try_load_config(Path::new(&valid_empty)) {
            Ok(j) => {
                assert_eq!(expected_config, j);
            }
            Err(e) => panic!("Got unexpected error result: {}", e),
        }
    }

    #[test]
    fn test_is_valid_config() {
        let device_schema_path = "/pkg/data/device_schema.json";
        let valid_factory_path = "/pkg/data/valid_factory_config.json";
        let test_config = Config::new("", valid_factory_path, device_schema_path);

        // Loads and deserializes the expected config.
        let valid_config = String::from("/pkg/data/valid_factory_config.json");
        let contents = fs::read_to_string(&valid_config)
            .expect(format!("Failed to open testdata file: {}", valid_config).as_ref());
        let expected_config: Value;
        match serde_json::from_str(&contents) {
            Ok(j) => expected_config = j,
            Err(e) => panic!("Got unexpected error result: {}", e),
        }

        match test_config.is_valid_config(&expected_config) {
            Ok(_) => (),
            Err(e) => panic!("Got unexpected error result: {}", e),
        }
    }

    #[test]
    fn test_toulouse_factory_config_deserializes() {
        let config_path = "/pkg/data/toulouse_factory_config.json";
        let mut contents = String::new();
        let mut f = File::open(config_path).unwrap();
        f.read_to_string(&mut contents).unwrap();

        // Makes sure that the Toulouse factory configuration can be deserialized.
        let _deserialized_config: DeviceConfig = serde_json::from_str(&contents)
            .expect(format!("Failed to deserialized {}", config_path).as_ref());
    }
}
