// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error, Result},
    ffx_daemon::target::TargetAddr,
    ffx_list_args::Format,
    fidl_fuchsia_developer_bridge as bridge,
    netext::IsLocalAddr,
    serde::Serialize,
    serde_json::json,
    std::cmp::max,
    std::convert::TryFrom,
    std::fmt::{self, Display, Write},
};

const NAME: &'static str = "NAME";
const SERIAL: &'static str = "SERIAL";
const TYPE: &'static str = "TYPE";
const STATE: &'static str = "STATE";
const ADDRS: &'static str = "ADDRS/IP";
const RCS: &'static str = "RCS";

const PADDING_SPACES: usize = 4;
/// A trait for returning a consistent SSH address.
///
/// Based on the structure from which the SSH address is coming, this will
/// return in order of priority:
/// -- The first local IPv6 address with a scope id.
/// -- The last local IPv4 address.
/// -- Any other address.
///
/// DEPRECATED: Migrate to using the ssh address target data.
pub trait SshAddrFetcher {
    fn to_ssh_addr(self) -> Option<TargetAddr>;
}

impl<'a, T: Copy + IntoIterator<Item = &'a TargetAddr>> SshAddrFetcher for &'a T {
    fn to_ssh_addr(self) -> Option<TargetAddr> {
        let mut res: Option<TargetAddr> = None;
        for addr in self.into_iter() {
            let is_valid_local_addr = addr.ip().is_local_addr()
                && (addr.ip().is_ipv4()
                    || !(addr.ip().is_link_local_addr() && addr.scope_id() == 0));

            if res.is_none() || is_valid_local_addr {
                res.replace(addr.clone());
            }
            if addr.ip().is_ipv6() && is_valid_local_addr {
                res.replace(addr.clone());
                break;
            }
        }
        res
    }
}

/// Simple trait for a target formatter.
pub trait TargetFormatter {
    fn lines(&self, default_nodename: Option<&str>) -> Vec<String>;
}

impl TryFrom<(Format, Vec<bridge::Target>)> for Box<dyn TargetFormatter> {
    type Error = Error;

    fn try_from(tup: (Format, Vec<bridge::Target>)) -> Result<Self> {
        let (format, targets) = tup;
        Ok(match format {
            Format::Tabular => Box::new(TabularTargetFormatter::try_from(targets)?),
            Format::Simple => Box::new(SimpleTargetFormatter::try_from(targets)?),
            Format::Addresses => Box::new(AddressesTargetFormatter::try_from(targets)?),
            Format::Json => Box::new(JsonTargetFormatter::try_from(targets)?),
        })
    }
}

pub struct AddressesTarget(TargetAddr);

impl TryFrom<bridge::Target> for AddressesTarget {
    type Error = Error;

    fn try_from(t: bridge::Target) -> Result<Self> {
        let addrs = t.addresses.ok_or(anyhow!("must contain an address"))?;
        let addrs = addrs.iter().map(|a| TargetAddr::from(a)).collect::<Vec<_>>();

        Ok(Self((&addrs).to_ssh_addr().ok_or(anyhow!("could not convert to ssh addr"))?))
    }
}

pub struct AddressesTargetFormatter {
    targets: Vec<AddressesTarget>,
}

impl TryFrom<Vec<bridge::Target>> for AddressesTargetFormatter {
    type Error = Error;

    fn try_from(mut targets: Vec<bridge::Target>) -> Result<Self> {
        let mut t = Vec::with_capacity(targets.len());
        for target in targets.drain(..) {
            if let Ok(addr_target) = AddressesTarget::try_from(target) {
                t.push(addr_target)
            }
        }
        Ok(Self { targets: t })
    }
}

impl TargetFormatter for AddressesTargetFormatter {
    fn lines(&self, _default_nodename: Option<&str>) -> Vec<String> {
        self.targets.iter().map(|t| format!("{}", t.0)).collect()
    }
}

pub struct SimpleTarget(String, TargetAddr);

pub struct SimpleTargetFormatter {
    targets: Vec<SimpleTarget>,
}

impl TryFrom<Vec<bridge::Target>> for SimpleTargetFormatter {
    type Error = Error;

    fn try_from(mut targets: Vec<bridge::Target>) -> Result<Self> {
        let mut t = Vec::with_capacity(targets.len());
        for target in targets.drain(..) {
            if let Ok(simple_target) = SimpleTarget::try_from(target) {
                t.push(simple_target)
            }
        }
        Ok(Self { targets: t })
    }
}

impl TargetFormatter for SimpleTargetFormatter {
    fn lines(&self, _default_nodename: Option<&str>) -> Vec<String> {
        self.targets.iter().map(|t| format!("{} {}", t.1, t.0)).collect()
    }
}

impl TryFrom<bridge::Target> for SimpleTarget {
    type Error = Error;

    fn try_from(t: bridge::Target) -> Result<Self> {
        let nodename = t.nodename.unwrap_or("".to_string());
        let addrs = t.addresses.ok_or(anyhow!("must contain an address"))?;
        let addrs = addrs.iter().map(|a| TargetAddr::from(a)).collect::<Vec<_>>();

        Ok(Self(nodename, (&addrs).to_ssh_addr().ok_or(anyhow!("could not convert to ssh addr"))?))
    }
}

pub struct JsonTargetFormatter {
    targets: Vec<JsonTarget>,
}

impl TryFrom<Vec<bridge::Target>> for JsonTargetFormatter {
    type Error = Error;

    fn try_from(mut targets: Vec<bridge::Target>) -> Result<Self> {
        let mut t = Vec::with_capacity(targets.len());
        for target in targets.drain(..) {
            if let Ok(string_target) = JsonTarget::try_from(target) {
                t.push(string_target)
            }
        }
        Ok(Self { targets: t })
    }
}

impl TargetFormatter for JsonTargetFormatter {
    fn lines(&self, _default_nodename: Option<&str>) -> Vec<String> {
        vec![serde_json::to_string(&self.targets).expect("should serialize")]
    }
}

#[derive(Debug, PartialEq, Eq)]
enum StringifiedField {
    String(String),
    Array(Vec<String>),
}

impl Default for StringifiedField {
    fn default() -> Self {
        StringifiedField::String(String::new())
    }
}

impl StringifiedField {
    fn len(&self) -> usize {
        match self {
            StringifiedField::String(_s) => 1,
            StringifiedField::Array(a) => a.len(),
        }
    }

    fn string_len(&self) -> usize {
        match self {
            StringifiedField::String(s) => s.len(),
            StringifiedField::Array(a) => a.iter().map(|s| s.len()).max().unwrap_or(0),
        }
    }

    fn at_index(&self, index: usize) -> Option<String> {
        match self {
            StringifiedField::String(s) => {
                if index == 0 {
                    Some(s.clone())
                } else {
                    None
                }
            }
            StringifiedField::Array(a) => a.get(index).cloned(),
        }
    }
}

// Convenience macro to make potential addition/removal of fields less likely
// to affect internal logic. Other functions that construct these targets will
// fail to compile if more fields are added.
macro_rules! make_structs_and_support_functions {
    ($( $field:ident ),+ $(,)?) => {
        #[derive(Default)]
        struct Limits {
            $(
                $field: usize,
            )*
        }

        impl Limits {
            fn update(&mut self, target: &mut StringifiedTarget) {
                $(
                    self.$field = max(self.$field, target.$field.string_len());
                    target.__longest_array = max(target.__longest_array, target.$field.len());
                )*
            }

            fn capacity(&self) -> usize {
                let mut result = 0;
                $(
                    result += self.$field + PADDING_SPACES;
                )*
                result
            }
        }

        #[derive(Debug, PartialEq, Eq)]
        struct StringifiedTarget {
            __longest_array: usize,
            $(
                $field: StringifiedField,
            )*
        }

        impl Default for StringifiedTarget {
            fn default() -> Self {
                Self {
                    __longest_array: 0,
                    $(
                        $field: StringifiedField::default(),
                    )*
                }
            }
        }

        #[derive(Serialize, Debug, PartialEq, Eq)]
        struct JsonTarget {
            $(
                $field: serde_json::Value,
            )*
        }

        make_structs_and_support_functions!(@print_func $($field,)*);
    };

    (@print_func $nodename:ident, $last_field:ident, $($field:ident),* $(,)?) => {
        #[inline]
        fn format_fields(target: &StringifiedTarget, limits: &Limits, default_nodename: &str) -> String {
            fn format_fields_(target: &StringifiedTarget, limits: &Limits, default_nodename: &str, index: usize) -> String {
                let mut s = String::with_capacity(limits.capacity());
                let nodename = match target.$nodename.at_index(index) {
                    Some(nodename) if nodename == default_nodename => format!("{}*", nodename),
                    Some(nodename) => nodename,
                    None => String::new(),
                };
                write!(s, "{:width$}", nodename, width = limits.$nodename + PADDING_SPACES).unwrap();
                $(
                    write!(s, "{:width$}", target.$field.at_index(index).unwrap_or_else(String::new), width = limits.$field + PADDING_SPACES).unwrap();
                )*
                // Skips spaces on the end.
                write!(s, "{}", target.$last_field.at_index(index).unwrap_or_else(String::new)).unwrap();
                s
            }

            (0..target.__longest_array).map(|i| format_fields_(target, limits, default_nodename, i)).collect::<Vec<_>>().join("\n")
        }
    };
}

// Second field is printed last in this implementation, everything else is printed in order.
make_structs_and_support_functions!(
    nodename,
    rcs_state,
    serial,
    target_type,
    target_state,
    addresses,
);

#[derive(Debug, PartialEq, Eq)]
pub enum StringifyError {
    MissingAddresses,
    MissingRcsState,
    MissingTargetType,
    MissingTargetState,
}

impl Display for StringifyError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "stringification error: {:?}", self)
    }
}

impl std::error::Error for StringifyError {}

impl StringifiedTarget {
    fn from_target_addr_info(a: bridge::TargetAddrInfo) -> String {
        format!("{}", TargetAddr::from(a))
    }

    fn from_addresses(mut v: Vec<bridge::TargetAddrInfo>) -> String {
        format!(
            "[{}]",
            v.drain(..)
                .map(|a| StringifiedTarget::from_target_addr_info(a))
                .collect::<Vec<_>>()
                .join(",; ")
        )
    }

    fn field_from_addresses(v: Vec<bridge::TargetAddrInfo>) -> StringifiedField {
        let all_addresses = StringifiedTarget::from_addresses(v);
        StringifiedField::Array(all_addresses.split(';').map(String::from).collect::<Vec<_>>())
    }

    fn from_rcs_state(r: bridge::RemoteControlState) -> String {
        match r {
            bridge::RemoteControlState::Down | bridge::RemoteControlState::Unknown => {
                "N".to_string()
            }
            bridge::RemoteControlState::Up => "Y".to_string(),
        }
    }

    fn from_target_type(t: bridge::TargetType) -> String {
        format!("{:?}", t)
    }

    fn from_target_state(t: bridge::TargetState) -> String {
        match t {
            bridge::TargetState::Unknown => "Unknown".to_string(),
            bridge::TargetState::Disconnected => "Disconnected".to_string(),
            bridge::TargetState::Product => "Product".to_string(),
            bridge::TargetState::Fastboot => "Fastboot".to_string(),
            bridge::TargetState::Zedboot => "Zedboot (R)".to_string(),
        }
    }
}

impl TryFrom<bridge::Target> for StringifiedTarget {
    type Error = StringifyError;

    fn try_from(target: bridge::Target) -> Result<Self, Self::Error> {
        let target_type = match (target.board_config.as_ref(), target.product_config.as_ref()) {
            (None, None) => StringifiedTarget::from_target_type(
                target.target_type.ok_or(StringifyError::MissingTargetType)?,
            ),
            (board, product) => format!(
                "{}.{}",
                product.unwrap_or(&"<unknown>".to_string()),
                board.unwrap_or(&"<unknown>".to_string())
            ),
        };
        Ok(Self {
            nodename: StringifiedField::String(target.nodename.unwrap_or("<unknown>".to_string())),
            serial: StringifiedField::String(
                target.serial_number.unwrap_or("<unknown>".to_string()),
            ),
            addresses: StringifiedTarget::field_from_addresses(
                target.addresses.ok_or(StringifyError::MissingAddresses)?,
            ),
            rcs_state: StringifiedField::String(StringifiedTarget::from_rcs_state(
                target.rcs_state.ok_or(StringifyError::MissingRcsState)?,
            )),
            target_type: StringifiedField::String(target_type),
            target_state: StringifiedField::String(StringifiedTarget::from_target_state(
                target.target_state.ok_or(StringifyError::MissingTargetState)?,
            )),
            ..Default::default()
        })
    }
}

impl TryFrom<bridge::Target> for JsonTarget {
    type Error = StringifyError;

    fn try_from(target: bridge::Target) -> Result<Self, Self::Error> {
        Ok(Self {
            nodename: json!(target.nodename.unwrap_or("<unknown>".to_string())),
            serial: json!(target.serial_number.unwrap_or("<unknown>".to_string())),
            addresses: json!(target
                .addresses
                .unwrap_or(vec![])
                .drain(..)
                .map(|a| StringifiedTarget::from_target_addr_info(a))
                .collect::<Vec<_>>()),
            rcs_state: json!(StringifiedTarget::from_rcs_state(
                target.rcs_state.ok_or(StringifyError::MissingRcsState)?,
            )),
            target_type: json!(StringifiedTarget::from_target_type(
                target.target_type.ok_or(StringifyError::MissingTargetType)?,
            )),
            target_state: json!(StringifiedTarget::from_target_state(
                target.target_state.ok_or(StringifyError::MissingTargetState)?,
            )),
        })
    }
}

pub struct TabularTargetFormatter {
    targets: Vec<StringifiedTarget>,
    limits: Limits,
}

impl TargetFormatter for TabularTargetFormatter {
    fn lines(&self, default_nodename: Option<&str>) -> Vec<String> {
        self.targets
            .iter()
            .map(|t| format_fields(t, &self.limits, default_nodename.unwrap_or("")))
            .collect()
    }
}

impl TryFrom<Vec<bridge::Target>> for TabularTargetFormatter {
    type Error = StringifyError;

    fn try_from(mut targets: Vec<bridge::Target>) -> Result<Self, Self::Error> {
        // First target is the table header in this case, since the formatting
        // for the table header is (for now) identical to the rest of the
        // targets
        let mut initial = vec![StringifiedTarget {
            nodename: StringifiedField::String(NAME.to_string()),
            serial: StringifiedField::String(SERIAL.to_string()),
            addresses: StringifiedField::String(ADDRS.to_string()),
            rcs_state: StringifiedField::String(RCS.to_string()),
            target_type: StringifiedField::String(TYPE.to_string()),
            target_state: StringifiedField::String(STATE.to_string()),
            ..Default::default()
        }];
        let mut limits = Limits::default();
        limits.update(&mut initial[0]);

        let acc = Self { targets: initial, limits };
        Ok(targets.drain(..).try_fold(acc, |mut a, t| {
            let mut s = StringifiedTarget::try_from(t)?;
            a.limits.update(&mut s);
            a.targets.push(s);
            Ok(a)
        })?)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address};
    use lazy_static::lazy_static;
    use std::net::{Ipv4Addr, SocketAddr, SocketAddrV4, SocketAddrV6};

    lazy_static! {
        static ref EMPTY_FORMATTER_GOLDEN: &'static str =
            include_str!("../test_data/target_formatter_empty_formatter_golden");
        static ref ONE_TARGET_WITH_DEFAULT_GOLDEN: &'static str =
            include_str!("../test_data/target_formatter_one_target_with_default_golden");
        static ref ONE_TARGET_NO_DEFAULT_GOLDEN: &'static str =
            include_str!("../test_data/target_formatter_one_target_no_default_golden");
        static ref EMPTY_NODENAME_WITH_DEFAULT_GOLDEN: &'static str =
            include_str!("../test_data/target_formatter_empty_nodename_with_default_golden");
        static ref EMPTY_NODENAME_NO_DEFAULT_GOLDEN: &'static str =
            include_str!("../test_data/target_formatter_empty_nodename_no_default_golden");
        static ref SIMPLE_FORMATTER_WITH_DEFAULT_GOLDEN: &'static str =
            include_str!("../test_data/target_formatter_simple_formatter_with_default_golden");
        static ref DEVICE_FINDER_FORMAT_GOLDEN: &'static str =
            include_str!("../test_data/target_formatter_device_finder_format_golden");
        static ref ADDRESSES_FORMAT_GOLDEN: &'static str =
            include_str!("../test_data/target_formatter_addresses_format_golden");
        static ref BUILD_CONFIG_FULL_GOLDEN: &'static str =
            include_str!("../test_data/target_formatter_build_config_full_golden");
        static ref BUILD_CONFIG_PRODUCT_MISSING_GOLDEN: &'static str =
            include_str!("../test_data/target_formatter_build_config_product_missing_golden");
        static ref BUILD_CONFIG_BOARD_MISSING_GOLDEN: &'static str =
            include_str!("../test_data/target_formatter_build_config_board_missing_golden");
    }

    fn make_valid_target() -> bridge::Target {
        bridge::Target {
            nodename: Some("fooberdoober".to_string()),
            addresses: Some(vec![
                bridge::TargetAddrInfo::Ip(bridge::TargetIp {
                    ip: IpAddress::Ipv6(Ipv6Address {
                        addr: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1],
                    }),
                    scope_id: 198,
                }),
                bridge::TargetAddrInfo::Ip(bridge::TargetIp {
                    ip: IpAddress::Ipv4(Ipv4Address { addr: [122, 24, 25, 25] }),
                    scope_id: 186,
                }),
            ]),
            rcs_state: Some(bridge::RemoteControlState::Unknown),
            target_type: Some(bridge::TargetType::Unknown),
            target_state: Some(bridge::TargetState::Unknown),
            ..bridge::Target::EMPTY
        }
    }

    #[test]
    fn test_empty_formatter() {
        let formatter = TabularTargetFormatter::try_from(Vec::<bridge::Target>::new()).unwrap();
        let lines = formatter.lines(None);
        assert_eq!(lines.len(), 1);
        assert_eq!(lines[0].len(), 50); // Just some manual math.
        assert_eq!(lines.join("\n"), EMPTY_FORMATTER_GOLDEN.to_string());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_formatter_one_target() {
        let formatter = TabularTargetFormatter::try_from(vec![
            make_valid_target(),
            bridge::Target {
                nodename: Some("lorberding".to_string()),
                addresses: Some(vec![bridge::TargetAddrInfo::Ip(bridge::TargetIp {
                    ip: IpAddress::Ipv6(Ipv6Address {
                        addr: [0xfe, 0x80, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1],
                    }),
                    scope_id: 137,
                })]),
                rcs_state: Some(bridge::RemoteControlState::Unknown),
                target_type: Some(bridge::TargetType::Unknown),
                target_state: Some(bridge::TargetState::Unknown),
                ..bridge::Target::EMPTY
            },
        ])
        .unwrap();
        let lines = formatter.lines(Some("fooberdoober"));
        assert_eq!(lines.len(), 3);
        assert_eq!(lines.join("\n"), ONE_TARGET_WITH_DEFAULT_GOLDEN.to_string());

        let lines = formatter.lines(None);
        assert_eq!(lines.len(), 3);
        assert_eq!(lines.join("\n"), ONE_TARGET_NO_DEFAULT_GOLDEN.to_string());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_formatter_empty_nodename() {
        let formatter = TabularTargetFormatter::try_from(vec![
            make_valid_target(),
            bridge::Target {
                nodename: None,
                addresses: Some(vec![bridge::TargetAddrInfo::Ip(bridge::TargetIp {
                    ip: IpAddress::Ipv6(Ipv6Address {
                        addr: [0xfe, 0x80, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1],
                    }),
                    scope_id: 137,
                })]),
                rcs_state: Some(bridge::RemoteControlState::Unknown),
                target_type: Some(bridge::TargetType::Unknown),
                target_state: Some(bridge::TargetState::Unknown),
                serial_number: Some("cereal".to_owned()),
                ..bridge::Target::EMPTY
            },
        ])
        .unwrap();
        let lines = formatter.lines(Some("fooberdoober"));
        assert_eq!(lines.len(), 3);
        assert_eq!(lines.join("\n"), EMPTY_NODENAME_WITH_DEFAULT_GOLDEN.to_string());

        let lines = formatter.lines(None);
        assert_eq!(lines.len(), 3);
        assert_eq!(lines.join("\n"), EMPTY_NODENAME_NO_DEFAULT_GOLDEN.to_string());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_simple_formatter() {
        let formatter = SimpleTargetFormatter::try_from(vec![
            make_valid_target(),
            bridge::Target {
                nodename: None,
                addresses: Some(vec![bridge::TargetAddrInfo::Ip(bridge::TargetIp {
                    ip: IpAddress::Ipv6(Ipv6Address {
                        addr: [0xfe, 0x80, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1],
                    }),
                    scope_id: 137,
                })]),
                rcs_state: Some(bridge::RemoteControlState::Unknown),
                target_type: Some(bridge::TargetType::Unknown),
                target_state: Some(bridge::TargetState::Unknown),
                ..bridge::Target::EMPTY
            },
        ])
        .unwrap();
        let lines = formatter.lines(Some("fooberdoober"));
        assert_eq!(lines.len(), 2);
        assert_eq!(lines.join("\n"), SIMPLE_FORMATTER_WITH_DEFAULT_GOLDEN.to_string());

        let lines = formatter.lines(None);
        assert_eq!(lines.len(), 2);
        assert_eq!(lines.join("\n"), SIMPLE_FORMATTER_WITH_DEFAULT_GOLDEN.to_string());
    }

    #[test]
    fn test_stringified_target_missing_state() {
        let mut t = make_valid_target();
        t.target_state = None;
        assert_eq!(StringifiedTarget::try_from(t), Err(StringifyError::MissingTargetState));
    }

    #[test]
    fn test_stringified_target_missing_target_type() {
        let mut t = make_valid_target();
        t.target_type = None;
        assert_eq!(StringifiedTarget::try_from(t), Err(StringifyError::MissingTargetType));
    }

    #[test]
    fn test_stringified_target_missing_rcs_state() {
        let mut t = make_valid_target();
        t.rcs_state = None;
        assert_eq!(StringifiedTarget::try_from(t), Err(StringifyError::MissingRcsState));
    }

    #[test]
    fn test_stringified_target_missing_addresses() {
        let mut t = make_valid_target();
        t.addresses = None;
        assert_eq!(StringifiedTarget::try_from(t), Err(StringifyError::MissingAddresses));
    }

    #[test]
    fn test_stringified_target_missing_nodename() {
        let mut t = make_valid_target();
        t.nodename = None;
        assert!(StringifiedTarget::try_from(t).is_ok());
    }

    #[test]
    fn test_device_finder_format() {
        let formatter = Box::<dyn TargetFormatter>::try_from((
            Format::Simple,
            vec![make_valid_target(), make_valid_target()],
        ))
        .unwrap();
        let lines = formatter.lines(None);
        assert_eq!(lines.join("\n"), DEVICE_FINDER_FORMAT_GOLDEN.to_string());
    }

    #[test]
    fn test_addresses_format() {
        let formatter = Box::<dyn TargetFormatter>::try_from((
            Format::Addresses,
            vec![make_valid_target(), make_valid_target()],
        ))
        .unwrap();
        let lines = formatter.lines(None);
        assert_eq!(lines.join("\n"), ADDRESSES_FORMAT_GOLDEN.to_string());
    }

    #[test]
    fn test_build_config_full() {
        let b = String::from("board");
        let p = String::from("default");
        let mut t = make_valid_target();
        t.board_config = Some(b);
        t.product_config = Some(p);
        let formatter = TabularTargetFormatter::try_from(vec![t]).unwrap();
        let lines = formatter.lines(None);
        assert_eq!(lines.join("\n"), BUILD_CONFIG_FULL_GOLDEN.to_string());
    }

    #[test]
    fn test_build_config_product_missing() {
        let b = String::from("x64");
        let mut t = make_valid_target();
        t.board_config = Some(b);
        t.product_config = None;
        let formatter = TabularTargetFormatter::try_from(vec![t]).unwrap();
        let lines = formatter.lines(None);
        assert_eq!(lines.join("\n"), BUILD_CONFIG_PRODUCT_MISSING_GOLDEN.to_string());
    }

    #[test]
    fn test_build_config_board_missing() {
        let p = String::from("foo");
        let mut t = make_valid_target();
        t.board_config = None;
        t.product_config = Some(p);
        let formatter = TabularTargetFormatter::try_from(vec![t]).unwrap();
        let lines = formatter.lines(None);
        assert_eq!(lines.join("\n"), BUILD_CONFIG_BOARD_MISSING_GOLDEN.to_string());
    }

    #[test]
    fn test_to_ssh_addr() {
        let sockets = vec![
            SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::new(127, 0, 0, 1), 0)),
            SocketAddr::V6(SocketAddrV6::new("f111::3".parse().unwrap(), 0, 0, 0)),
            SocketAddr::V6(SocketAddrV6::new("fe80::1".parse().unwrap(), 0, 0, 0)),
            SocketAddr::V6(SocketAddrV6::new("fe80::2".parse().unwrap(), 0, 0, 1)),
            SocketAddr::V6(SocketAddrV6::new("fe80::3".parse().unwrap(), 0, 0, 0)),
        ];
        let addrs = sockets.iter().map(|s| TargetAddr::from(*s)).collect::<Vec<_>>();
        assert_eq!((&addrs).to_ssh_addr(), Some(addrs[3]));

        let sockets = vec![
            SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::new(127, 0, 0, 1), 0)),
            SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::new(129, 0, 0, 1), 0)),
        ];
        let addrs = sockets.iter().map(|s| TargetAddr::from(*s)).collect::<Vec<_>>();
        assert_eq!((&addrs).to_ssh_addr(), Some(addrs[0]));

        let addrs = Vec::<TargetAddr>::new();
        assert_eq!((&addrs).to_ssh_addr(), None);
    }

    #[test]
    fn test_stringified_product_state() {
        let mut t = make_valid_target();
        t.target_state = Some(bridge::TargetState::Product);
        assert!(StringifiedTarget::try_from(t).is_ok());
    }

    #[test]
    fn test_stringified_fastboot_state() {
        let mut t = make_valid_target();
        t.target_state = Some(bridge::TargetState::Fastboot);
        assert!(StringifiedTarget::try_from(t).is_ok());
    }

    #[test]
    fn test_stringified_unknown_state() {
        let mut t = make_valid_target();
        t.target_state = Some(bridge::TargetState::Unknown);
        assert!(StringifiedTarget::try_from(t).is_ok());
    }

    #[test]
    fn test_stringified_disconnected_state() {
        let mut t = make_valid_target();
        t.target_state = Some(bridge::TargetState::Disconnected);
        assert!(StringifiedTarget::try_from(t).is_ok());
    }
}
