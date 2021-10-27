// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ffx_emulator_start_args::StartCommand;
use serde::Deserialize;
use std::fs::File;
use std::io::BufReader;

// TODO(fxbug.dev/84803): This will need to move to the parsing library once we have a schema.
// Note: this struct is a holding place for inputs from device manifest files, which are defined
// by a fixed schema in //build/sdk/meta. Any changes to one must be reflected in the other.
#[derive(Debug, Default, Deserialize)]
pub struct DeviceSpec {
    pub audio: bool,
    pub image_size: String,
    pub pointing_device: String,
    pub ram_mb: usize,
    pub window_height: usize,
    pub window_width: usize,
    pub port_map: Option<PortMap>,
}

#[derive(Debug, Default, Deserialize)]
pub struct PortMap {
    pub ports: Vec<MappedPort>,
}

#[derive(Debug, Default, Deserialize)]
pub struct MappedPort {
    protocol: Option<Protocol>,
    version: Option<IPVersion>,
    host: Option<String>,
    host_port: u16,
    guest: Option<String>,
    guest_port: u16,
}

#[derive(Debug, Deserialize)]
enum Protocol {
    TCP,
    UDP,
}
impl Default for Protocol {
    fn default() -> Self {
        Protocol::TCP
    }
}

#[derive(Debug, Deserialize)]
enum IPVersion {
    V4,
    V6,
}
impl Default for IPVersion {
    fn default() -> Self {
        IPVersion::V4
    }
}

fn default_audio() -> bool {
    true
}

fn default_window_height() -> usize {
    800
}

fn default_window_width() -> usize {
    1280
}

// Note the value for ram should match the defaults used in `fx emu` (//tools/devshell/emu)
// and in `fx qemu` (//zircon/scripts/run-zircon).
fn default_ram_mb() -> usize {
    8192
}

fn default_image_size() -> String {
    "2G".to_string()
}

fn default_pointing_device() -> String {
    "touch".to_string()
}

fn ssh_port(port: u16) -> MappedPort {
    MappedPort {
        protocol: Some(Protocol::TCP),
        version: Some(IPVersion::V4),
        host: None,
        host_port: port,
        guest: None,
        guest_port: 22,
    }
}

fn default_port_map() -> Option<PortMap> {
    Some(PortMap { ports: vec![ssh_port(8022)] })
}

impl DeviceSpec {
    pub fn default() -> DeviceSpec {
        DeviceSpec {
            audio: default_audio(),
            image_size: default_image_size(),
            pointing_device: default_pointing_device(),
            ram_mb: default_ram_mb(),
            window_height: default_window_height(),
            window_width: default_window_width(),
            port_map: default_port_map(),
        }
    }

    fn get_values_from_flags(&mut self, cmd: &StartCommand) {
        self.audio = cmd.audio.or(Some(self.audio)).unwrap();
        self.image_size = cmd.image_size.as_ref().unwrap_or(&self.image_size).to_string();
        self.pointing_device =
            cmd.pointing_device.as_ref().unwrap_or(&self.pointing_device).to_string();
        self.ram_mb = cmd.ram_mb.or(Some(self.ram_mb)).unwrap();
        self.window_height = cmd.window_height.or(Some(self.window_height)).unwrap();
        self.window_width = cmd.window_width.or(Some(self.window_width)).unwrap();
    }

    pub fn from_manifest(cmd: &StartCommand) -> Result<DeviceSpec, anyhow::Error> {
        let mut spec = match &cmd.device_spec {
            None => DeviceSpec::default(),
            Some(path) => {
                // Open the file in read-only mode with buffer.
                let file = File::open(path)?;
                let reader = BufReader::new(file);

                // Read the JSON contents of the file as an instance of `DeviceSpec`.
                serde_json::from_reader(reader)?
            }
        };
        spec.get_values_from_flags(cmd);
        return Ok(spec);
    }
}

impl PortMap {
    pub fn get_ssh_port(&self) -> Option<u16> {
        let result = None;
        for port in &self.ports {
            if port.guest_port == 22 {
                return Some(port.host_port);
            }
        }
        result
    }

    pub fn add_ssh_port(&mut self, port: u16) {
        self.ports.push(ssh_port(port));
    }

    pub fn to_string(&self) -> String {
        let mut result = "".to_string();
        let mut optional_comma = "";
        for mapped_port in &self.ports {
            let version_prefix;
            let open_bracket;
            let close_bracket;
            match mapped_port.version {
                Some(IPVersion::V6) => {
                    version_prefix = "ipv6-";
                    open_bracket = "[";
                    close_bracket = "]";
                }
                Some(IPVersion::V4) | None => {
                    version_prefix = "";
                    open_bracket = "";
                    close_bracket = "";
                }
            }
            result = format!(
                "{}{}{}hostfwd={}:{}:{}-{}:{}",
                result,
                optional_comma,
                version_prefix,
                match mapped_port.protocol {
                    Some(Protocol::UDP) => "udp",
                    Some(Protocol::TCP) | None => "tcp",
                },
                mapped_port
                    .host
                    .as_ref()
                    .map_or("".to_string(), |v| format!("{}{}{}", open_bracket, v, close_bracket)),
                mapped_port.host_port,
                mapped_port
                    .guest
                    .as_ref()
                    .map_or("".to_string(), |v| format!("{}{}{}", open_bracket, v, close_bracket)),
                mapped_port.guest_port,
            );
            optional_comma = ",";
        }
        result
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use regex::Regex;

    #[test]
    fn test_convert_start_cmd_to_device_spec() {
        let start_command = &StartCommand {
            audio: Some(false),
            image_size: Some("512M".to_string()),
            pointing_device: Some("mouse".to_string()),
            ram_mb: Some(16392),
            window_height: Some(480),
            window_width: Some(640),
            ..Default::default()
        };
        let mut device_spec: DeviceSpec = DeviceSpec::default();
        assert_eq!(device_spec.audio, default_audio());
        assert_eq!(device_spec.image_size, default_image_size());
        assert_eq!(device_spec.pointing_device, default_pointing_device());
        assert_eq!(device_spec.ram_mb, default_ram_mb());
        assert_eq!(device_spec.window_height, default_window_height());
        assert_eq!(device_spec.window_width, default_window_width());

        device_spec.get_values_from_flags(start_command);
        assert_eq!(device_spec.audio, false);
        assert_eq!(device_spec.image_size, "512M");
        assert_eq!(device_spec.pointing_device, "mouse");
        assert_eq!(device_spec.ram_mb, 16392);
        assert_eq!(device_spec.window_height, 480);
        assert_eq!(device_spec.window_width, 640);
    }

    #[test]
    fn test_to_string() {
        let mut portmap = PortMap { ..Default::default() };
        // No ports => no output
        assert_eq!(portmap.to_string(), "");

        // test default v4 syntax
        portmap.add_ssh_port(23456);
        assert_eq!(portmap.to_string(), "hostfwd=tcp::23456-:22");
        // switch to udp
        portmap.ports[0].protocol = Some(Protocol::UDP);
        assert_eq!(portmap.to_string(), "hostfwd=udp::23456-:22");
        // with host addresses
        portmap.ports[0].host = Some("127.0.0.1".to_string());
        portmap.ports[0].guest = Some("192.168.1.15".to_string());
        assert_eq!(portmap.to_string(), "hostfwd=udp:127.0.0.1:23456-192.168.1.15:22");

        // test v6 syntax
        let mut portmap = PortMap {
            ports: {
                vec![MappedPort {
                    protocol: Some(Protocol::TCP),
                    version: Some(IPVersion::V6),
                    host: None,
                    host_port: 123,
                    guest: None,
                    guest_port: 234,
                }]
            },
        };
        assert_eq!(portmap.to_string(), "ipv6-hostfwd=tcp::123-:234");
        // with host addresses
        portmap.ports[0].host = Some("::1".to_string());
        portmap.ports[0].guest = Some("fe80::abc:1230:4567:aaaa%en0".to_string());
        assert_eq!(
            portmap.to_string(),
            "ipv6-hostfwd=tcp:[::1]:123-[fe80::abc:1230:4567:aaaa%en0]:234"
        );
        // switch to udp
        portmap.ports[0].protocol = Some(Protocol::UDP);
        assert_eq!(
            portmap.to_string(),
            "ipv6-hostfwd=udp:[::1]:123-[fe80::abc:1230:4567:aaaa%en0]:234"
        );

        // test multiple ports
        let portmap = PortMap {
            ports: {
                vec![
                    MappedPort { host_port: 123, guest_port: 234, ..Default::default() },
                    MappedPort { host_port: 456, guest_port: 567, ..Default::default() },
                    MappedPort { host_port: 789, guest_port: 890, ..Default::default() },
                ]
            },
        };
        // order doesn't matter, but they should all be present
        let re1 = Regex::new(r"^|,hostfwd=tcp::123-:234,|$").unwrap();
        let re2 = Regex::new(r"^|,hostfwd=tcp::456-:567,|$").unwrap();
        let re3 = Regex::new(r"^|,hostfwd=tcp::789-:890,|$").unwrap();
        let port_map = portmap.to_string();
        assert!(re1.is_match(&port_map));
        assert!(re2.is_match(&port_map));
        assert!(re3.is_match(&port_map));

        // test mixed cases
        let portmap = PortMap {
            ports: {
                vec![
                    MappedPort {
                        version: Some(IPVersion::V4),
                        protocol: Some(Protocol::TCP),
                        host_port: 123,
                        guest: Some("10.0.2.15".to_string()),
                        guest_port: 234,
                        ..Default::default()
                    },
                    MappedPort {
                        version: Some(IPVersion::V6),
                        protocol: Some(Protocol::TCP),
                        host: Some("::1".to_string()),
                        host_port: 456,
                        guest_port: 567,
                        ..Default::default()
                    },
                    MappedPort {
                        version: Some(IPVersion::V4),
                        protocol: Some(Protocol::UDP),
                        host_port: 789,
                        guest_port: 890,
                        ..Default::default()
                    },
                ]
            },
        };
        // order doesn't matter, but they should all be present
        let re1 = Regex::new(r"^|,hostfwd=tcp::123-10.0.2.15:234,|$").unwrap();
        let re2 = Regex::new(r"^|,ipv6-hostfwd=tcp:[::1]:456-:567,|$").unwrap();
        let re3 = Regex::new(r"^|,hostfwd=udp::789-:890,|$").unwrap();
        let port_map = portmap.to_string();
        assert!(re1.is_match(&port_map));
        assert!(re2.is_match(&port_map));
        assert!(re3.is_match(&port_map));
    }
}
