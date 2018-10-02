// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! These definitions are here to provide serde with the ability to derive
//! Serialize/Deserialize on remote types

#![allow(non_snake_case)]

use fidl_fuchsia_bluetooth_control::{AddressType, BondingData, Key, LeConnectionParameters, LeData, Ltk,
                                     SecurityProperties, OutputCapabilityType, InputCapabilityType};
use serde_derive::{Serialize, Deserialize};
use std::collections::HashMap;

#[derive(Serialize, Deserialize)]
#[serde(remote = "LeConnectionParameters")]
pub struct LeConnectionParametersDef {
    pub connection_interval: u16,
    pub connection_latency: u16,
    pub supervision_timeout: u16,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "InputCapabilityType")]
#[allow(dead_code)]
pub enum InputCapabilityTypeDef {
    None = 0,
    Confirmation = 1,
    Keyboard = 2,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "OutputCapabilityType")]
#[allow(dead_code)]
pub enum OutputCapabilityTypeDef {
    None = 0,
    Display = 1,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "Key")]
pub struct KeyDef {
    #[serde(with = "SecurityPropertiesDef")]
    pub security_properties: SecurityProperties,
    pub value: [u8; 16],
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "SecurityProperties")]
pub struct SecurityPropertiesDef {
    pub authenticated: bool,
    pub secure_connections: bool,
    pub encryption_key_size: u8,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "AddressType")]
#[allow(dead_code)]
pub enum AddressTypeDef {
    LePublic = 0,
    LeRandom = 1,
    Bredr = 2,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "Ltk")]
pub struct LtkDef {
    #[serde(with = "KeyDef")]
    pub key: Key,
    pub key_size: u8,
    pub ediv: u16,
    pub rand: u64,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "LeData")]
pub struct LeDataDef {
    pub address: String,
    #[serde(with = "AddressTypeDef")]
    pub address_type: AddressType,
    #[serde(with = "LeConnectionParametersWrapper")]
    pub connection_parameters: Option<Box<LeConnectionParameters>>,
    pub services: Vec<String>,
    #[serde(with = "LtkWrapper")]
    pub ltk: Option<Box<Ltk>>,
    #[serde(with = "KeyWrapper")]
    pub irk: Option<Box<Key>>,
    #[serde(with = "KeyWrapper")]
    pub csrk: Option<Box<Key>>,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "BondingData")]
pub struct BondingDataDef {
    pub identifier: String,
    pub local_address: String,
    pub name: Option<String>,
    #[serde(with = "LeDataWrapper")]
    pub le: Option<Box<LeData>>,
}

/// Wrap the Ser/De types to work with Option<Box<T>>
macro_rules! optboxify {
    ($mod:ident, $b:ident, $c:ident, $d:expr) => {
        mod $mod {
            use super::{$b, $c};
            use serde::{Deserialize, Deserializer};
            use serde::{Serialize, Serializer};

            pub fn serialize<S>(value: &Option<Box<$b>>, serializer: S) -> Result<S::Ok, S::Error>
            where
                S: Serializer,
            {
                #[derive(Serialize)]
                struct Wrapper<'a>(#[serde(with = $d)] &'a Box<$b>);
                value.as_ref().map(Wrapper).serialize(serializer)
            }

            pub fn deserialize<'de, D>(deserializer: D) -> Result<Option<Box<$b>>, D::Error>
            where
                D: Deserializer<'de>,
            {
                #[derive(Deserialize)]
                struct Wrapper(#[serde(with = $d)] $b);

                let helper = Option::deserialize(deserializer)?;
                Ok(helper.map(|Wrapper(external)| Box::new(external)))
            }
        }
    };
}

optboxify!(LeDataWrapper, LeData, LeDataDef, "LeDataDef");
optboxify!(KeyWrapper, Key, KeyDef, "KeyDef");
optboxify!(LtkWrapper, Ltk, LtkDef, "LtkDef");
optboxify!(
    LeConnectionParametersWrapper,
    LeConnectionParameters,
    LeConnectionParametersDef,
    "LeConnectionParametersDef"
);

#[derive(Debug, Serialize, Deserialize)]
pub struct BondMap(HashMap<String, VecBondingData>);

impl BondMap {
    pub fn new() -> Self {
        BondMap(HashMap::new())
    }

    #[allow(dead_code)]
    pub fn inner(&self) -> &HashMap<String, VecBondingData> {
        &self.0
    }

    pub fn iter_mut<'a>(&'a mut self) -> impl Iterator<Item = (&String, &mut VecBondingData)> {
        self.0.iter_mut()
    }

    pub fn inner_mut(&mut self) -> &mut HashMap<String, VecBondingData> {
        &mut self.0
    }
}

#[derive(Debug, Serialize, Deserialize)]
pub struct VecBondingData {
    #[serde(with = "VecBondData")]
    pub inner: Vec<BondingData>,
}

impl VecBondingData {
    pub fn iter_mut<'a>(
        &'a mut self,
    ) -> impl Iterator<Item = &'a mut BondingData> + ExactSizeIterator {
        self.inner.iter_mut()
    }
}

mod VecBondData {
    use super::{BondingData, BondingDataDef};
    use serde::Serializer;
    use serde::{Deserialize, Deserializer};

    pub fn deserialize<'de, D>(deserializer: D) -> Result<Vec<BondingData>, D::Error>
    where
        D: Deserializer<'de>,
    {
        #[derive(Deserialize)]
        struct Wrapper(#[serde(with = "BondingDataDef")] BondingData);

        let v = Vec::deserialize(deserializer)?;
        Ok(v.into_iter().map(|Wrapper(a)| a).collect())
    }

    pub fn serialize<S>(value: &Vec<BondingData>, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        #[derive(Serialize)]
        struct Wrapper<'a>(#[serde(with = "BondingDataDef")] &'a BondingData);

        serializer.collect_seq(value.iter().map(Wrapper))
    }
}
