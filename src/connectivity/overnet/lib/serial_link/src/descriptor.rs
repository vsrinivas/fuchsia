// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handle parsing of serial descriptors - strings that describe how to configure serial comms

pub use fidl_fuchsia_hardware_serial::{CharacterWidth, Config, FlowControl, Parity, StopWidth};
use std::{collections::HashSet, path::PathBuf};

/// Describes one desired serial link
#[derive(Debug, PartialEq)]
pub enum Descriptor {
    /// Use the kernel debug facility
    #[cfg(target_os = "fuchsia")]
    Debug,
    /// Pipe stdin, stdout
    #[cfg(not(target_os = "fuchsia"))]
    StdioPipe,
    /// Connect to a specific device
    Device {
        /// Path to the device
        path: PathBuf,
        /// Configuration for the device
        config: Config,
    },
}

impl std::fmt::Display for Descriptor {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            #[cfg(target_os = "fuchsia")]
            Descriptor::Debug => f.write_str("debug"),
            #[cfg(not(target_os = "fuchsia"))]
            Descriptor::StdioPipe => f.write_str("-"),
            Descriptor::Device { path, config } => write!(
                f,
                "{}:b={}:s={}:c={}:p={}:f={}",
                path.to_string_lossy(),
                config.baud_rate,
                match config.stop_width {
                    StopWidth::Bits1 => 1,
                    StopWidth::Bits2 => 2,
                },
                match config.character_width {
                    CharacterWidth::Bits5 => 5,
                    CharacterWidth::Bits6 => 6,
                    CharacterWidth::Bits7 => 7,
                    CharacterWidth::Bits8 => 8,
                },
                match config.parity {
                    Parity::None => "none",
                    Parity::Even => "even",
                    Parity::Odd => "odd",
                },
                match config.control_flow {
                    FlowControl::None => "none",
                    FlowControl::CtsRts => "cts_rts",
                }
            ),
        }
    }
}

const DEFAULT_CONFIG: Config = Config {
    baud_rate: 9600,
    character_width: CharacterWidth::Bits8,
    control_flow: FlowControl::None,
    parity: Parity::None,
    stop_width: StopWidth::Bits1,
};

#[derive(Clone, Copy, Debug)]
enum PathEnumerationMethod {
    #[cfg(test)]
    Mocked,
    Real,
}

#[cfg(target_os = "fuchsia")]
async fn platform_enumerate_paths() -> Result<Vec<PathBuf>, std::io::Error> {
    std::fs::read_dir("/dev/class/serial")?.map(|dir| Ok(dir?.path())).collect()
}

#[cfg(target_os = "linux")]
async fn platform_enumerate_paths() -> Result<Vec<PathBuf>, std::io::Error> {
    use {
        async_std::fs::{read_dir, read_to_string},
        futures::{lock::Mutex, prelude::*},
        std::os::unix::fs::MetadataExt,
    };

    fn split_major_minor(v: u64) -> (u32, u32) {
        (((v >> 8) & 0xff) as u32, (v & 0xff) as u32)
    }
    let device_numbers = Mutex::new(HashSet::new());
    read_dir("/sys/class/tty")
        .await?
        .try_for_each_concurrent(None, |dir| {
            let device_numbers = &device_numbers;
            async move {
                let mut device = dir.path().clone();
                device.push("device/driver");
                if device.exists().await {
                    let mut dev = dir.path().clone();
                    dev.push("dev");
                    let dev = read_to_string(dev).await?;
                    let mut dev = dev.trim().split(':');
                    let major: u32 = dev
                        .next()
                        .ok_or_else(|| {
                            std::io::Error::new(std::io::ErrorKind::Other, "No major index")
                        })?
                        .parse()
                        .map_err(|_| {
                            std::io::Error::new(
                                std::io::ErrorKind::Other,
                                "Couldn't parse major index",
                            )
                        })?;
                    let minor: u32 = dev
                        .next()
                        .ok_or_else(|| {
                            std::io::Error::new(std::io::ErrorKind::Other, "No minor index")
                        })?
                        .parse()
                        .map_err(|_| {
                            std::io::Error::new(
                                std::io::ErrorKind::Other,
                                "Couldn't parse minor index",
                            )
                        })?;
                    if dev.next() != None {
                        return Err(std::io::Error::new(
                            std::io::ErrorKind::Other,
                            "Trailing bytes in device description",
                        ));
                    }
                    device_numbers.lock().await.insert((major, minor));
                }
                Ok(())
            }
        })
        .await?;
    let paths = Mutex::new(Vec::new());
    let device_numbers = device_numbers.lock().await;
    read_dir("/dev")
        .await?
        .try_for_each_concurrent(None, |dir| {
            let paths = &paths;
            let device_numbers = &device_numbers;
            async move {
                let rdev = dir.metadata().await?.rdev();
                if rdev != 0 {
                    if device_numbers.contains(&split_major_minor(rdev)) {
                        paths.lock().await.push(dir.path());
                    }
                }
                Ok(())
            }
        })
        .await?;
    let mut paths = paths.lock().await;
    paths.sort();
    Ok(std::mem::replace(&mut *paths, Vec::new()).into_iter().map(|p| p.into()).collect())
}

#[cfg(not(target_os = "linux"))]
#[cfg(not(target_os = "fuchsia"))]
async fn platform_enumerate_paths() -> Result<Vec<PathBuf>, std::io::Error> {
    Ok(vec![])
}

async fn enumerate_paths(
    path_enumeration_method: PathEnumerationMethod,
) -> Result<Vec<PathBuf>, std::io::Error> {
    match path_enumeration_method {
        #[cfg(test)]
        PathEnumerationMethod::Mocked => {
            Ok(["/dev/ttyS0", "/dev/ttyS1", "/dev/ttyS2", "/dev/ttyS3"]
                .iter()
                .map(Into::into)
                .collect())
        }
        PathEnumerationMethod::Real => platform_enumerate_paths().await,
    }
}

fn existing_config<'a>(out: &'a mut Vec<Descriptor>, path: &PathBuf) -> Option<&'a mut Config> {
    for other in out.iter_mut() {
        if let Descriptor::Device { path: other_path, config } = other {
            if path == other_path {
                return Some(config);
            }
        }
    }
    return None;
}

/// Parse a descriptor string
pub async fn parse(descriptor: &str) -> Result<Vec<Descriptor>, ParseError> {
    parse_impl(descriptor, PathEnumerationMethod::Real).await
}

async fn parse_impl(
    descriptor: &str,
    path_enumeration_method: PathEnumerationMethod,
) -> Result<Vec<Descriptor>, ParseError> {
    let mut seen_paths = HashSet::new();
    let mut out = Vec::new();
    let mut seen_all = false;
    for desc in descriptor.split(',') {
        match desc.trim() {
            "none" => (),
            #[cfg(target_os = "fuchsia")]
            "debug" => {
                if out.iter().find(|&x| *x == Descriptor::Debug).is_none() {
                    out.push(Descriptor::Debug)
                }
            }
            #[cfg(not(target_os = "fuchsia"))]
            "-" => {
                if out.iter().find(|&x| *x == Descriptor::StdioPipe).is_none() {
                    out.push(Descriptor::StdioPipe)
                }
            }
            desc => {
                let mut config = Config { ..DEFAULT_CONFIG };
                let mut config_it = desc.split(':');
                let desc = config_it.next().unwrap().trim();
                for config_el in config_it {
                    if let Some(eq_pos) = config_el.find('=') {
                        let (key, value) = config_el.split_at(eq_pos);
                        let key = key.trim();
                        let mut value_chrs = value.chars();
                        value_chrs.next();
                        let value = value_chrs.as_str().trim();
                        let bad = || {
                            Err(ParseError::BadConfigValue {
                                path: desc.to_string(),
                                key: key.to_string(),
                                value: value.to_string(),
                            })
                        };
                        match key {
                            "b" | "baud" | "baud_rate" => {
                                config.baud_rate = value.parse().map_err(|_| bad()).unwrap()
                            }
                            "s" | "stop" | "stop_bits" => {
                                match value.parse().map_err(|_| bad()).unwrap() {
                                    1 => config.stop_width = StopWidth::Bits1,
                                    2 => config.stop_width = StopWidth::Bits2,
                                    _ => return bad(),
                                }
                            }
                            "c" | "cw" | "character_width" => {
                                match value.parse().map_err(|_| bad()).unwrap() {
                                    7 => config.character_width = CharacterWidth::Bits7,
                                    8 => config.character_width = CharacterWidth::Bits8,
                                    _ => return bad(),
                                }
                            }
                            "p" | "parity" => match value {
                                "none" | "n" | "0" => config.parity = Parity::None,
                                "even" | "e" => config.parity = Parity::Even,
                                "odd" | "o" => config.parity = Parity::Odd,
                                _ => return bad(),
                            },
                            "f" | "fc" | "cf" | "flow" | "flow_control" | "control_flow" => {
                                match value {
                                    "none" | "n" | "0" => config.control_flow = FlowControl::None,
                                    "ctsrts" | "cts_rts" => {
                                        config.control_flow = FlowControl::CtsRts
                                    }
                                    _ => return bad(),
                                }
                            }
                            _ => {
                                return Err(ParseError::UnknownConfigKey {
                                    path: desc.to_string(),
                                    config: config_el.to_string(),
                                })
                            }
                        }
                    } else {
                        return Err(ParseError::ExpectedEquals {
                            path: desc.to_string(),
                            config: config_el.to_string(),
                        });
                    }
                }
                match desc {
                    "all" => {
                        if seen_all {
                            return Err(ParseError::MultipleAllDescriptors);
                        }
                        seen_all = true;
                        for path in enumerate_paths(path_enumeration_method).await?.into_iter() {
                            if existing_config(&mut out, &path).is_none() {
                                out.push(Descriptor::Device { path, config })
                            }
                        }
                    }
                    path if desc.chars().next() == Some('/') => {
                        if !seen_paths.insert(path) {
                            return Err(ParseError::DuplicateDevicePath(path.to_string()));
                        }
                        let path: PathBuf = path.into();
                        if let Some(existing) = existing_config(&mut out, &path) {
                            *existing = config;
                        } else {
                            out.push(Descriptor::Device { path, config });
                        }
                    }
                    _ => {
                        return Err(ParseError::UnknownDescriptorType(desc.to_string()));
                    }
                }
            }
        }
    }
    Ok(out)
}

/// Parse error
#[derive(Debug, thiserror::Error)]
pub enum ParseError {
    /// Descriptor type could not be determined
    #[error("unknown descriptor type: '{0}'")]
    UnknownDescriptorType(String),
    /// Configuration fragments are of the form key=value
    #[error("no '=' in config '{config}' for device '{path}'")]
    ExpectedEquals {
        /// Path of the device being configured
        path: String,
        /// Configuration element that caused the failure
        config: String,
    },
    /// Configuration value could not be parsed
    #[error("config value could not be parsed")]
    BadConfigValue {
        /// Path of the device being configured
        path: String,
        /// Configuration element that caused the failure
        key: String,
        /// Value that caused the failure
        value: String,
    },
    /// Unknown configuration key
    #[error("unknown configuration key in config '{config}' for device '{path}'")]
    UnknownConfigKey {
        /// Path of the device being configured
        path: String,
        /// Configuration element that caused the failure
        config: String,
    },
    /// Two descriptors had the same path
    #[error("two configurations for the same device path {0}")]
    DuplicateDevicePath(String),
    /// Some I/O error occurred during parsing
    #[error("io error {0}")]
    IO(#[from] std::io::Error),
    /// Multiple all descriptors found
    #[error("multiple all descriptors found")]
    MultipleAllDescriptors,
}

#[cfg(test)]
mod tests {
    use super::{
        CharacterWidth, Config, Descriptor, FlowControl, Parity, ParseError, PathEnumerationMethod,
        StopWidth, DEFAULT_CONFIG,
    };
    use futures::executor::block_on;

    fn parse(descriptor: &str) -> Result<Vec<Descriptor>, ParseError> {
        block_on(super::parse_impl(descriptor, PathEnumerationMethod::Mocked))
    }

    #[test]
    fn debug_desc() {
        match parse("debug") {
            #[cfg(target_os = "fuchsia")]
            Ok(v) => assert_eq!(v, vec![Descriptor::Debug]),
            #[cfg(not(target_os = "fuchsia"))]
            Err(ParseError::UnknownDescriptorType(path)) => assert_eq!(path, "debug"),
            r => panic!("unexpected result: {:?}", r),
        }
    }

    #[test]
    fn stdio_desc() {
        match parse("-") {
            #[cfg(not(target_os = "fuchsia"))]
            Ok(v) => assert_eq!(v, vec![Descriptor::StdioPipe]),
            #[cfg(target_os = "fuchsia")]
            Err(ParseError::UnknownDescriptorType(path)) => assert_eq!(path, "-"),
            r => panic!("unexpected result: {:?}", r),
        }
    }

    #[test]
    fn simple_dev_desc() {
        assert_eq!(
            parse("/dev/ttyS0").unwrap(),
            vec![Descriptor::Device { path: "/dev/ttyS0".into(), config: DEFAULT_CONFIG }]
        );
    }

    #[test]
    fn devs_with_config() {
        assert_eq!(
            parse("/dev/ttyS0:b=115200").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { baud_rate: 115200, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0: b = 115200 ").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { baud_rate: 115200, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:baud=115200").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { baud_rate: 115200, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:baud_rate=115200").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { baud_rate: 115200, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:s=2").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { stop_width: StopWidth::Bits2, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:stop=2").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { stop_width: StopWidth::Bits2, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:stop_bits=2").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { stop_width: StopWidth::Bits2, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:c=7").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { character_width: CharacterWidth::Bits7, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:cw=7").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { character_width: CharacterWidth::Bits7, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:character_width=7").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { character_width: CharacterWidth::Bits7, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:p=none").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { parity: Parity::None, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:p=even").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { parity: Parity::Even, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:p=odd").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { parity: Parity::Odd, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:parity=odd").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { parity: Parity::Odd, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:f=ctsrts").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { control_flow: FlowControl::CtsRts, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:f=cts_rts").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { control_flow: FlowControl::CtsRts, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:fc=ctsrts").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { control_flow: FlowControl::CtsRts, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:cf=ctsrts").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { control_flow: FlowControl::CtsRts, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:flow=ctsrts").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { control_flow: FlowControl::CtsRts, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:flow_control=ctsrts").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { control_flow: FlowControl::CtsRts, ..DEFAULT_CONFIG }
            }]
        );
        assert_eq!(
            parse("/dev/ttyS0:control_flow=ctsrts").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config { control_flow: FlowControl::CtsRts, ..DEFAULT_CONFIG }
            }]
        );
    }

    #[test]
    fn devs_with_multi_config() {
        assert_eq!(
            parse("/dev/ttyS0:p=odd : f=ctsrts:s=2").unwrap(),
            vec![Descriptor::Device {
                path: "/dev/ttyS0".into(),
                config: Config {
                    parity: Parity::Odd,
                    control_flow: FlowControl::CtsRts,
                    stop_width: StopWidth::Bits2,
                    ..DEFAULT_CONFIG
                }
            }]
        )
    }

    #[test]
    fn multiple_devices() {
        assert_eq!(
            parse("/dev/ttyS0:p=odd, /dev/ttyS1:p=even").unwrap(),
            vec![
                Descriptor::Device {
                    path: "/dev/ttyS0".into(),
                    config: Config { parity: Parity::Odd, ..DEFAULT_CONFIG }
                },
                Descriptor::Device {
                    path: "/dev/ttyS1".into(),
                    config: Config { parity: Parity::Even, ..DEFAULT_CONFIG }
                }
            ]
        );
        match parse("/dev/ttyS1:p=odd,/dev/ttyS1:p=even") {
            Err(ParseError::DuplicateDevicePath(path)) => assert_eq!(path, "/dev/ttyS1"),
            r => panic!("unexpected result: {:?}", r),
        }
    }

    #[test]
    fn all_devices() {
        assert_eq!(
            parse("all").unwrap(),
            vec![
                Descriptor::Device { path: "/dev/ttyS0".into(), config: DEFAULT_CONFIG },
                Descriptor::Device { path: "/dev/ttyS1".into(), config: DEFAULT_CONFIG },
                Descriptor::Device { path: "/dev/ttyS2".into(), config: DEFAULT_CONFIG },
                Descriptor::Device { path: "/dev/ttyS3".into(), config: DEFAULT_CONFIG },
            ]
        );
    }

    #[test]
    fn all_devices_with_overrides() {
        assert_eq!(
            parse("/dev/ttyS0:p=odd, all:p=none, /dev/ttyS1:p=even").unwrap(),
            vec![
                Descriptor::Device {
                    path: "/dev/ttyS0".into(),
                    config: Config { parity: Parity::Odd, ..DEFAULT_CONFIG }
                },
                Descriptor::Device {
                    path: "/dev/ttyS1".into(),
                    config: Config { parity: Parity::Even, ..DEFAULT_CONFIG }
                },
                Descriptor::Device {
                    path: "/dev/ttyS2".into(),
                    config: Config { parity: Parity::None, ..DEFAULT_CONFIG }
                },
                Descriptor::Device {
                    path: "/dev/ttyS3".into(),
                    config: Config { parity: Parity::None, ..DEFAULT_CONFIG }
                },
            ]
        );
    }

    #[test]
    fn platform_enumerate_paths_succeeds() {
        block_on(super::platform_enumerate_paths()).unwrap();
    }
}
