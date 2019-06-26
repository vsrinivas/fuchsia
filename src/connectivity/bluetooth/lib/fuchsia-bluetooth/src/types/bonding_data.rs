// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::util, fidl_fuchsia_bluetooth_control as control};

#[derive(Clone, Debug, PartialEq)]
pub struct BondingData {
    pub identifier: String,
    pub local_address: String,
    pub name: Option<String>,
    pub le: Option<control::LeData>,
    pub bredr: Option<control::BredrData>,
}

impl From<control::BondingData> for BondingData {
    fn from(bd: control::BondingData) -> BondingData {
        BondingData {
            identifier: bd.identifier,
            local_address: bd.local_address,
            name: bd.name,
            le: bd.le.map(|le| *le),
            bredr: bd.bredr.map(|bredr| *bredr),
        }
    }
}

impl From<BondingData> for control::BondingData {
    fn from(bd: BondingData) -> control::BondingData {
        control::BondingData {
            identifier: bd.identifier,
            local_address: bd.local_address,
            name: bd.name,
            le: bd.le.map(|le| Box::new(le)),
            bredr: bd.bredr.map(|bredr| Box::new(bredr)),
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_bluetooth_control as control};

    fn new_bond() -> BondingData {
        let remote_key = control::RemoteKey {
            security_properties: control::SecurityProperties {
                authenticated: false,
                secure_connections: true,
                encryption_key_size: 0,
            },
            value: [0; 16],
        };

        let ltk = control::Ltk { key: remote_key.clone(), key_size: 0, ediv: 1, rand: 2 };

        BondingData {
            identifier: "id".into(),
            local_address: "addr".into(),
            name: Some("name".into()),
            le: Some(control::LeData {
                address: "le_addr".into(),
                address_type: control::AddressType::LeRandom,
                connection_parameters: Some(Box::new(control::LeConnectionParameters {
                    connection_interval: 0,
                    connection_latency: 1,
                    supervision_timeout: 2,
                })),
                services: vec![],
                ltk: Some(Box::new(ltk.clone())),
                irk: Some(Box::new(remote_key.clone())),
                csrk: Some(Box::new(remote_key.clone())),
            }),
            bredr: Some(control::BredrData {
                address: "bredr_addr".into(),
                piconet_leader: true,
                services: vec![],
                link_key: Some(Box::new(ltk.clone())),
            }),
        }
    }

    #[test]
    fn bonding_data_conversion() {
        let bond = new_bond();
        assert_eq!(bond, BondingData::from(control::BondingData::from(bond.clone())));
    }
}
