// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_gatt;
use fidl_gatt::{Characteristic as FidlCharacteristic, ServiceInfo};
use std::fmt;

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
        Service {
            info: info,
            characteristics: None,
        }
    }

    pub fn set_characteristics(&mut self, chrcs: Vec<FidlCharacteristic>) {
        self.characteristics = Some(chrcs.into_iter().map(|info| Characteristic(info)).collect());
    }
}

impl fmt::Display for Service {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "[service: id: {}, primary: {}, type: {}]",
            self.info.id, self.info.primary, self.info.type_
        )?;
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
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "[chara.: id: {}, type: {}, prop.: {}]",
            self.0.id,
            self.0.type_,
            props_to_string(self.0.properties)
        )?;
        if let Some(ref descrs) = self.0.descriptors {
            let mut i: i32 = 0;
            for ref descr in descrs {
                write!(
                    f,
                    "\n          {}: [descr.: id: {}, type: {}]",
                    i, descr.id, descr.type_
                )?;
                i += 1;
            }
        }
        Ok(())
    }
}

fn props_to_string(props: u32) -> String {
    let mut prop_strs = vec![];

    if props & fidl_gatt::PROPERTY_BROADCAST != 0 {
        prop_strs.push("broadcast");
    }
    if props & fidl_gatt::PROPERTY_READ != 0 {
        prop_strs.push("read");
    }
    if props & fidl_gatt::PROPERTY_WRITE_WITHOUT_RESPONSE != 0 {
        prop_strs.push("write (without response)");
    }
    if props & fidl_gatt::PROPERTY_WRITE != 0 {
        prop_strs.push("write");
    }
    if props & fidl_gatt::PROPERTY_NOTIFY != 0 {
        prop_strs.push("notify");
    }
    if props & fidl_gatt::PROPERTY_INDICATE != 0 {
        prop_strs.push("indicate");
    }
    if props & fidl_gatt::PROPERTY_AUTHENTICATED_SIGNED_WRITES != 0 {
        prop_strs.push("write (signed)");
    }
    if props & fidl_gatt::PROPERTY_RELIABLE_WRITE != 0 {
        prop_strs.push("write (reliable)");
    }
    if props & fidl_gatt::PROPERTY_WRITABLE_AUXILIARIES != 0 {
        prop_strs.push("writable aux.");
    }

    prop_strs.join(", ")
}
