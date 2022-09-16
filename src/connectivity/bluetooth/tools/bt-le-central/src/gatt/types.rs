// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth_gatt2::{
        self as fidl_gatt, Characteristic as FidlCharacteristic, ServiceInfo, ServiceKind,
    },
    fuchsia_bluetooth::assigned_numbers::{
        find_characteristic_number, find_descriptor_number, find_service_uuid,
    },
    fuchsia_bluetooth::types::Uuid,
    std::fmt,
};

// TODO(armansito): Make these objects stateful so that GATT operations can be performed through
// them. Later, move these into a public bluetooth crate as a developer API.

/// Service
pub struct Service {
    pub info: ServiceInfo,

    // The characteristics of this service. None, if not yet discovered.
    characteristics: Option<Vec<Characteristic>>,
}

impl Service {
    pub fn new(info: ServiceInfo) -> Service {
        Service { info: info, characteristics: None }
    }

    pub fn set_characteristics(&mut self, chrcs: Vec<FidlCharacteristic>) {
        self.characteristics = Some(chrcs.into_iter().map(|info| Characteristic(info)).collect());
    }
}

impl fmt::Display for Service {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let type_pretty = match &self.info.type_ {
            Some(fidl_uuid) => {
                let uuid = Uuid::from(fidl_uuid).to_string();
                find_service_uuid(&uuid).map(|an| String::from(an.name)).unwrap_or(uuid)
            }
            None => String::from("(unknown type)"),
        };
        let kind_pretty = match self.info.kind {
            Some(ServiceKind::Primary) => "primary",
            Some(ServiceKind::Secondary) => "secondary",
            None => "(unknown kind)",
        };
        let id = self.info.handle.map_or(0u64, |h| h.value);
        write!(f, "[{} service: id: {}, type: {}]", kind_pretty, id, type_pretty)?;
        if let Some(ref chrcs) = self.characteristics {
            let mut i: i32 = 0;
            for ref chrc in chrcs {
                write!(f, "\n      {}: {}", i, chrc)?;
                i += 1;
            }
        }
        Ok(())
    }
}

struct Characteristic(FidlCharacteristic);

impl fmt::Display for Characteristic {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let type_pretty = match &self.0.type_ {
            Some(fidl_uuid) => {
                let uuid = Uuid::from(fidl_uuid).to_string();
                find_characteristic_number(&uuid).map(|an| String::from(an.name)).unwrap_or(uuid)
            }
            None => String::from("(unknown)"),
        };
        let id = &self.0.handle.map_or(0u64, |h| h.value);
        let props_pretty =
            self.0.properties.map_or(String::from("unknown"), |p| props_to_string(p));
        write!(f, "[chara.: id: {}, type: {}, prop.: {}]", id, type_pretty, props_pretty)?;
        if let Some(ref descrs) = self.0.descriptors {
            let mut i: i32 = 0;
            for ref descr in descrs {
                let type_pretty = match &descr.type_ {
                    Some(fidl_uuid) => {
                        let uuid = Uuid::from(fidl_uuid).to_string();
                        find_descriptor_number(&uuid)
                            .map(|an| String::from(an.name))
                            .unwrap_or(uuid)
                    }
                    None => String::from("unknown"),
                };
                let id = &descr.handle.map_or(0u64, |h| h.value);
                write!(f, "\n          {}: [descr.: id: {}, type: {}]", i, id, type_pretty)?;
                i += 1;
            }
        }
        Ok(())
    }
}

fn props_to_string(props: fidl_gatt::CharacteristicPropertyBits) -> String {
    let mut prop_strs = vec![];

    if props.contains(fidl_gatt::CharacteristicPropertyBits::BROADCAST) {
        prop_strs.push("broadcast");
    }
    if props.contains(fidl_gatt::CharacteristicPropertyBits::READ) {
        prop_strs.push("read");
    }
    if props.contains(fidl_gatt::CharacteristicPropertyBits::WRITE_WITHOUT_RESPONSE) {
        prop_strs.push("write (without response)");
    }
    if props.contains(fidl_gatt::CharacteristicPropertyBits::WRITE) {
        prop_strs.push("write");
    }
    if props.contains(fidl_gatt::CharacteristicPropertyBits::NOTIFY) {
        prop_strs.push("notify");
    }
    if props.contains(fidl_gatt::CharacteristicPropertyBits::INDICATE) {
        prop_strs.push("indicate");
    }
    if props.contains(fidl_gatt::CharacteristicPropertyBits::AUTHENTICATED_SIGNED_WRITES) {
        prop_strs.push("write (signed)");
    }
    if props.contains(fidl_gatt::CharacteristicPropertyBits::RELIABLE_WRITE) {
        prop_strs.push("write (reliable)");
    }
    if props.contains(fidl_gatt::CharacteristicPropertyBits::WRITABLE_AUXILIARIES) {
        prop_strs.push("writable aux.");
    }

    prop_strs.join(", ")
}
