// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error, Result},
    chrono::Duration,
    ffx_daemon::target::{SshAddrFetcher, TargetAddr},
    ffx_list_args::Format,
    fidl_fuchsia_developer_bridge as bridge,
    std::cmp::max,
    std::convert::TryFrom,
    std::fmt::{self, Display, Write},
};

const NAME: &'static str = "NAME";
const TYPE: &'static str = "TYPE";
const STATE: &'static str = "STATE";
const ADDRS: &'static str = "ADDRS/IP";
const AGE: &'static str = "AGE";
const RCS: &'static str = "RCS";

const PADDING_SPACES: usize = 4;

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
        })
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
            t.push(SimpleTarget::try_from(target)?)
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
        let nodename = t.nodename.ok_or(anyhow!("must contain nodename"))?;
        let addrs = t.addresses.ok_or(anyhow!("must contain an address"))?;
        let addrs = addrs.iter().map(|a| TargetAddr::from(a)).collect::<Vec<_>>();

        Ok(Self(nodename, (&addrs).to_ssh_addr().ok_or(anyhow!("could not convert to ssh addr"))?))
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
            fn update(&mut self, target: &StringifiedTarget) {
                $(
                    self.$field = max(self.$field, target.$field.len());
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
            $(
                $field: String,
            )*
        }

        make_structs_and_support_functions!(@print_func $($field,)*);
    };

    (@print_func $nodename:ident, $last_field:ident, $($field:ident),* $(,)?) => {
        #[inline]
        fn format_fields(target: &StringifiedTarget, limits: &Limits, default_nodename: &str) -> String {
            let mut s = String::with_capacity(limits.capacity());
            write!(s, "{:width$}",
                   if target.$nodename == default_nodename {
                       format!("{}*", target.$nodename)
                   } else {
                       target.$nodename.clone()
                   },
                   width = limits.$nodename + PADDING_SPACES).unwrap();
            $(
                write!(s, "{:width$}", target.$field, width = limits.$field + PADDING_SPACES).unwrap();
            )*
            // Skips spaces on the end.
            write!(s, "{}", target.$last_field).unwrap();
            s
        }
    };
}

// Second field is printed last in this implementation, everything else is printed in order.
make_structs_and_support_functions!(nodename, rcs_state, target_type, target_state, addresses, age,);

#[derive(Debug, PartialEq, Eq)]
pub enum StringifyError {
    MissingNodename,
    MissingAddresses,
    MissingAge,
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
                .join(", ")
        )
    }

    fn from_age(a: u64) -> String {
        // TODO(awdavies): There's probably a better formatter out there.
        let duration = Duration::milliseconds(a as i64);
        let seconds = (duration - Duration::minutes(duration.num_minutes())).num_seconds();
        format!("{}m{}s", duration.num_minutes(), seconds)
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
        format!("{:?}", t)
    }
}

impl TryFrom<bridge::Target> for StringifiedTarget {
    type Error = StringifyError;

    fn try_from(target: bridge::Target) -> Result<Self, Self::Error> {
        Ok(Self {
            nodename: target.nodename.ok_or(StringifyError::MissingNodename)?,
            addresses: StringifiedTarget::from_addresses(
                target.addresses.ok_or(StringifyError::MissingAddresses)?,
            ),
            age: StringifiedTarget::from_age(target.age_ms.ok_or(StringifyError::MissingAge)?),
            rcs_state: StringifiedTarget::from_rcs_state(
                target.rcs_state.ok_or(StringifyError::MissingRcsState)?,
            ),
            target_type: StringifiedTarget::from_target_type(
                target.target_type.ok_or(StringifyError::MissingTargetType)?,
            ),
            target_state: StringifiedTarget::from_target_state(
                target.target_state.ok_or(StringifyError::MissingTargetState)?,
            ),
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
        let initial = vec![StringifiedTarget {
            nodename: NAME.to_string(),
            addresses: ADDRS.to_string(),
            age: AGE.to_string(),
            rcs_state: RCS.to_string(),
            target_type: TYPE.to_string(),
            target_state: STATE.to_string(),
        }];
        let mut limits = Limits::default();
        limits.update(&initial[0]);

        let acc = Self { targets: initial, limits };
        Ok(targets.drain(..).try_fold(acc, |mut a, t| {
            let s = StringifiedTarget::try_from(t)?;
            a.limits.update(&s);
            a.targets.push(s);
            Ok(a)
        })?)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address};

    fn make_valid_target() -> bridge::Target {
        bridge::Target {
            nodename: Some("fooberdoober".to_string()),
            addresses: Some(vec![
                bridge::TargetAddrInfo::Ip(bridge::TargetIp {
                    ip: IpAddress::Ipv6(Ipv6Address {
                        addr: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1],
                    }),
                    scope_id: 2,
                }),
                bridge::TargetAddrInfo::Ip(bridge::TargetIp {
                    ip: IpAddress::Ipv4(Ipv4Address { addr: [122, 24, 25, 25] }),
                    scope_id: 4,
                }),
            ]),
            age_ms: Some(62345), // 1m2s
            rcs_state: Some(bridge::RemoteControlState::Unknown),
            target_type: Some(bridge::TargetType::Unknown),
            target_state: Some(bridge::TargetState::Unknown),
            ..bridge::Target::empty()
        }
    }

    #[test]
    fn test_empty_formatter() {
        let formatter = TabularTargetFormatter::try_from(Vec::<bridge::Target>::new()).unwrap();
        let lines = formatter.lines(None);
        assert_eq!(lines.len(), 1);
        assert_eq!(lines[0].len(), 47); // Just some manual math.
        assert_eq!(&lines[0], "NAME    TYPE    STATE    ADDRS/IP    AGE    RCS");
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
                    scope_id: 2,
                })]),
                age_ms: Some(120345), // 2m3s
                rcs_state: Some(bridge::RemoteControlState::Unknown),
                target_type: Some(bridge::TargetType::Unknown),
                target_state: Some(bridge::TargetState::Unknown),
                ..bridge::Target::empty()
            },
        ])
        .unwrap();
        let lines = formatter.lines(Some("fooberdoober"));
        assert_eq!(lines.len(), 3);

        // TODO(awdavies): This can probably function better via golden files.
        assert_eq!(&lines[0],
                   "NAME            TYPE       STATE      ADDRS/IP                                           AGE     RCS");
        assert_eq!(
            &lines[1],
            "fooberdoober*   Unknown    Unknown    [101:101:101:101:101:101:101:101, 122.24.25.25]    1m2s    N"
        );
        assert_eq!(&lines[2], "lorberding      Unknown    Unknown    [fe80::101:101:101:101%2]                          2m0s    N");

        let lines = formatter.lines(None);
        assert_eq!(lines.len(), 3);
        assert_eq!(&lines[0],
                   "NAME            TYPE       STATE      ADDRS/IP                                           AGE     RCS");
        assert_eq!(
            &lines[1],
            "fooberdoober    Unknown    Unknown    [101:101:101:101:101:101:101:101, 122.24.25.25]    1m2s    N"
        );
        assert_eq!(&lines[2], "lorberding      Unknown    Unknown    [fe80::101:101:101:101%2]                          2m0s    N");
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
    fn test_stringified_target_missing_age() {
        let mut t = make_valid_target();
        t.age_ms = None;
        assert_eq!(StringifiedTarget::try_from(t), Err(StringifyError::MissingAge));
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
        assert_eq!(StringifiedTarget::try_from(t), Err(StringifyError::MissingNodename));
    }

    #[test]
    fn test_device_finder_format() {
        let formatter = Box::<dyn TargetFormatter>::try_from((
            Format::Simple,
            vec![make_valid_target(), make_valid_target()],
        ))
        .unwrap();
        let lines = formatter.lines(None);
        assert_eq!(lines[0], "101:101:101:101:101:101:101:101 fooberdoober");
        assert_eq!(lines[1], "101:101:101:101:101:101:101:101 fooberdoober");
    }
}
