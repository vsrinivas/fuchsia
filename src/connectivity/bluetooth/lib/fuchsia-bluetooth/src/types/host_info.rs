// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        inspect::{DebugExt, InspectData, Inspectable, IsInspectable, ToProperty},
        types::{Address, HostId},
    },
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth::Bool,
    fidl_fuchsia_bluetooth_control as fctrl, fidl_fuchsia_bluetooth_sys as fsys,
    fuchsia_inspect::{self as inspect, Property},
    std::{convert::TryFrom, fmt},
};

/// `HostInfo` contains informational parameters and state for a bt-host device.
#[derive(Clone, Debug, PartialEq)]
pub struct HostInfo {
    /// Uniquely identifies a host on the current system.
    pub id: HostId,

    /// The Bluetooth technologies that are supported by this adapter.
    pub technology: fsys::TechnologyType,

    /// The identity address.
    pub address: Address,

    /// Indicates whether or not this is the active host. The system has one active host which
    /// handles all Bluetooth procedures.
    pub active: bool,

    /// The local name of this host. This is the name that is visible to other devices when this
    /// host is in the discoverable mode. Not present if the local device name is unknown.
    pub local_name: Option<String>,

    /// Whether or not the local adapter is currently discoverable over BR/EDR and
    /// LE physical channels.
    pub discoverable: bool,

    /// Whether or not device discovery is currently being performed.
    pub discovering: bool,
}

impl TryFrom<&fsys::HostInfo> for HostInfo {
    type Error = Error;
    fn try_from(src: &fsys::HostInfo) -> Result<HostInfo, Self::Error> {
        Ok(HostInfo {
            id: HostId::from(src.id.ok_or(format_err!("HostInfo.id is mandatory!"))?),
            technology: src.technology.ok_or(format_err!("HostInfo.technology is mandatory!"))?,
            address: src.address.ok_or(format_err!("HostInfo.address is mandatory!"))?.into(),
            active: src.active.unwrap_or(false),
            local_name: src.local_name.clone(),
            discoverable: src.discoverable.unwrap_or(false),
            discovering: src.discovering.unwrap_or(false),
        })
    }
}

impl TryFrom<fsys::HostInfo> for HostInfo {
    type Error = Error;
    fn try_from(src: fsys::HostInfo) -> Result<HostInfo, Self::Error> {
        HostInfo::try_from(&src)
    }
}

impl From<&HostInfo> for fsys::HostInfo {
    fn from(src: &HostInfo) -> fsys::HostInfo {
        fsys::HostInfo {
            id: Some(src.id.into()),
            technology: Some(src.technology),
            address: Some(src.address.into()),
            active: Some(src.active),
            local_name: src.local_name.clone(),
            discoverable: Some(src.discoverable),
            discovering: Some(src.discovering),
        }
    }
}

impl From<HostInfo> for fsys::HostInfo {
    fn from(src: HostInfo) -> fsys::HostInfo {
        fsys::HostInfo::from(&src)
    }
}

// TODO(fxbug.dev/36378): Remove this conversion function when we no longer need to support
// fuchsia.bluetooth.control.
impl From<HostInfo> for fctrl::AdapterInfo {
    fn from(src: HostInfo) -> fctrl::AdapterInfo {
        fctrl::AdapterInfo {
            identifier: src.id.to_string(),
            technology: match src.technology {
                fsys::TechnologyType::LowEnergy => fctrl::TechnologyType::LowEnergy,
                fsys::TechnologyType::Classic => fctrl::TechnologyType::Classic,
                fsys::TechnologyType::DualMode => fctrl::TechnologyType::DualMode,
            },
            address: src.address.to_string(),
            state: Some(Box::new(fctrl::AdapterState {
                local_name: src.local_name,
                discoverable: Some(Box::new(Bool { value: src.discoverable })),
                discovering: Some(Box::new(Bool { value: src.discovering })),
                local_service_uuids: None,
            })),
        }
    }
}

impl fmt::Display for HostInfo {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(fmt, "HostInfo:")?;
        writeln!(fmt, "\tidentifier:\t{}", self.id.to_string())?;
        writeln!(fmt, "\taddress:\t{}", self.address)?;
        writeln!(fmt, "\ttechnology:\t{:?}", self.technology)?;
        writeln!(
            fmt,
            "\tlocal name:\t{}",
            self.local_name.as_ref().unwrap_or(&"(unknown)".to_string())
        )?;
        writeln!(fmt, "\tdiscoverable:\t{}", self.discoverable)?;
        writeln!(fmt, "\tdiscovering:\t{}", self.discovering)
    }
}

impl Inspectable<HostInfo> {
    pub fn update(&mut self, info: HostInfo) {
        self.inspect.update(&info);
        self.inner = info;
    }
}

pub struct HostInfoInspect {
    inspect: inspect::Node,
    identifier: inspect::UintProperty,
    technology: inspect::StringProperty,
    active: inspect::UintProperty,
    discoverable: inspect::UintProperty,
    discovering: inspect::UintProperty,
}

impl HostInfoInspect {
    fn update(&mut self, info: &HostInfo) {
        self.identifier.set(info.id.0);
        self.technology.set(&info.technology.debug());
        self.active.set(info.active.to_property());
        self.discoverable.set(info.discoverable.to_property());
        self.discovering.set(info.discovering.to_property());
    }
}

impl IsInspectable for HostInfo {
    type I = HostInfoInspect;
}

impl InspectData<HostInfo> for HostInfoInspect {
    fn new(info: &HostInfo, inspect: inspect::Node) -> HostInfoInspect {
        HostInfoInspect {
            identifier: inspect.create_uint("identifier", info.id.0),
            technology: inspect.create_string("technology", info.technology.debug()),
            active: inspect.create_uint("active", info.active.to_property()),
            discoverable: inspect.create_uint("discoverable", info.discoverable.to_property()),
            discovering: inspect.create_uint("discovering", info.discovering.to_property()),
            inspect,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        fidl_fuchsia_bluetooth as fbt,
        fuchsia_inspect::{self as inspect, assert_inspect_tree},
    };

    fn empty_info() -> fsys::HostInfo {
        fsys::HostInfo {
            id: None,
            technology: None,
            address: None,
            active: None,
            local_name: None,
            discoverable: None,
            discovering: None,
        }
    }

    #[test]
    fn from_fidl_id_not_present() {
        let info = HostInfo::try_from(empty_info());
        assert!(info.is_err());
    }

    #[test]
    fn from_fidl_technology_not_present() {
        let info = fsys::HostInfo { id: Some(fbt::HostId { value: 1 }), ..empty_info() };
        let info = HostInfo::try_from(info);
        assert!(info.is_err());
    }

    #[test]
    fn from_fidl_address_not_present() {
        let info = fsys::HostInfo {
            id: Some(fbt::HostId { value: 1 }),
            technology: Some(fsys::TechnologyType::LowEnergy),
            ..empty_info()
        };
        let info = HostInfo::try_from(info);
        assert!(info.is_err());
    }

    #[test]
    fn from_fidl_optional_fields_not_present() {
        let info = fsys::HostInfo {
            id: Some(fbt::HostId { value: 1 }),
            technology: Some(fsys::TechnologyType::LowEnergy),
            address: Some(fbt::Address {
                type_: fbt::AddressType::Public,
                bytes: [1, 2, 3, 4, 5, 6],
            }),
            active: None,
            local_name: None,
            discoverable: None,
            discovering: None,
        };
        let expected = HostInfo {
            id: HostId(1),
            technology: fsys::TechnologyType::LowEnergy,
            address: Address::Public([1, 2, 3, 4, 5, 6]),
            active: false,
            local_name: None,
            discoverable: false,
            discovering: false,
        };

        let info = HostInfo::try_from(info).expect("expected successful conversion");
        assert_eq!(expected, info);
    }

    #[test]
    fn from_fidl_optional_fields_present() {
        let info = fsys::HostInfo {
            id: Some(fbt::HostId { value: 1 }),
            technology: Some(fsys::TechnologyType::LowEnergy),
            address: Some(fbt::Address {
                type_: fbt::AddressType::Public,
                bytes: [1, 2, 3, 4, 5, 6],
            }),
            active: Some(true),
            local_name: Some("name".to_string()),
            discoverable: Some(false),
            discovering: Some(true),
        };
        let expected = HostInfo {
            id: HostId(1),
            technology: fsys::TechnologyType::LowEnergy,
            address: Address::Public([1, 2, 3, 4, 5, 6]),
            active: true,
            local_name: Some("name".to_string()),
            discoverable: false,
            discovering: true,
        };

        let info = HostInfo::try_from(info).expect("expected successful conversion");
        assert_eq!(expected, info);
    }

    #[test]
    fn to_fidl() {
        let info = HostInfo {
            id: HostId(1),
            technology: fsys::TechnologyType::LowEnergy,
            address: Address::Public([1, 2, 3, 4, 5, 6]),
            active: false,
            local_name: Some("name".to_string()),
            discoverable: false,
            discovering: false,
        };
        let expected = fsys::HostInfo {
            id: Some(fbt::HostId { value: 1 }),
            technology: Some(fsys::TechnologyType::LowEnergy),
            address: Some(fbt::Address {
                type_: fbt::AddressType::Public,
                bytes: [1, 2, 3, 4, 5, 6],
            }),
            active: Some(false),
            local_name: Some("name".to_string()),
            discoverable: Some(false),
            discovering: Some(false),
        };

        assert_eq!(expected, info.into());
    }

    #[test]
    fn to_fidl_adapter_info() {
        let info = HostInfo {
            id: HostId(1),
            technology: fsys::TechnologyType::LowEnergy,
            address: Address::Public([1, 2, 3, 4, 5, 6]),
            active: false,
            local_name: Some("name".to_string()),
            discoverable: false,
            discovering: false,
        };
        let expected = fctrl::AdapterInfo {
            identifier: "0000000000000001".to_string(),
            technology: fctrl::TechnologyType::LowEnergy,
            address: "06:05:04:03:02:01".to_string(),
            state: Some(Box::new(fctrl::AdapterState {
                local_name: Some("name".to_string()),
                discoverable: Some(Box::new(Bool { value: false })),
                discovering: Some(Box::new(Bool { value: false })),
                local_service_uuids: None,
            })),
        };

        assert_eq!(expected, info.into());
    }

    #[test]
    fn inspect() {
        let inspector = inspect::Inspector::new();
        let node = inspector.root().create_child("info");
        let info = HostInfo {
            id: HostId(1),
            technology: fsys::TechnologyType::LowEnergy,
            address: Address::Public([1, 2, 3, 4, 5, 6]),
            active: false,
            local_name: Some("name".to_string()),
            discoverable: false,
            discovering: true,
        };
        let mut info = Inspectable::new(info, node);
        assert_inspect_tree!(inspector, root: {
            info: contains {
                identifier: 1u64,
                technology: "LowEnergy",
                active: 0u64,
                discoverable: 0u64,
                discovering: 1u64
            }
        });

        info.update(HostInfo {
            id: HostId(1),
            technology: fsys::TechnologyType::LowEnergy,
            address: Address::Public([1, 2, 3, 4, 5, 6]),
            active: true,
            local_name: Some("foo".to_string()),
            discoverable: true,
            discovering: true,
        });
        assert_inspect_tree!(inspector, root: {
            info: contains {
                identifier: 1u64,
                technology: "LowEnergy",
                active: 1u64,
                discoverable: 1u64,
                discovering: 1u64
            }
        });
    }
}
