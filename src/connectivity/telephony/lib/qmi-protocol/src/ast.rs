// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Derived from https://www.telit.com/wp-content/uploads/2017/09/Telit_LM940_QMI_Command_Reference_Guide_r1.pdf

use failure::{format_err, Error};
use serde::de::{self, Deserialize, Deserializer};
use serde_derive::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Serialize, Deserialize)]
pub struct SubParam {
    pub param: String,
    pub size: u16,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct TLV {
    pub param: String,
    #[serde(deserialize_with = "from_hex_str")]
    pub id: u16,
    pub size: Option<u16>,
    #[serde(default)]
    pub optional: bool,
    #[serde(default, rename = "subparams")]
    pub sub_params: Vec<SubParam>,
}

impl TLV {
    pub fn has_sub_params(&self) -> bool {
        !self.sub_params.is_empty()
    }
}

#[derive(Debug, Serialize, Deserialize)]
pub struct Structure {
    #[serde(rename = "type")]
    ty: String,
    pub transaction_len: u8,
    // more custom things as we explore the space of QMI services
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ResultCode {
    #[serde(rename = "type")]
    ty: String,
    pub transaction_len: u8,
    // more custom things as we explore the space of QMI services
}

#[derive(Debug, Serialize, Deserialize)]
pub struct Ast {
    #[serde(default, deserialize_with = "structure_map")]
    pub structures: HashMap<String, Structure>,
    #[serde(default)]
    pub services: Vec<Service>,
    #[serde(default, deserialize_with = "result_map")]
    pub results: HashMap<String, HashMap<String, u16>>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct Service {
    pub name: String,
    #[serde(rename = "type", deserialize_with = "from_hex_str")]
    pub ty: u16,
    pub message_structure: String,
    pub messages: Vec<Message>,
}

impl Service {
    pub fn get_messages<'a>(&'a self) -> impl Iterator<Item = &'a Message> {
        self.messages.iter()
    }
}

#[derive(Debug, Serialize, Deserialize)]
pub struct Message {
    pub name: String,
    #[serde(rename = "type", deserialize_with = "from_hex_str")]
    pub ty: u16,
    pub version: String,
    pub request: Vec<TLV>,
    pub response: Vec<TLV>,
}

impl Message {
    pub fn get_request_fields<'a>(&'a self) -> impl Iterator<Item = &'a TLV> {
        self.request.iter()
    }

    pub fn get_response_fields<'a>(&'a self) -> impl Iterator<Item = &'a TLV> {
        self.response.iter()
    }
}

#[derive(Debug)]
pub struct ServiceSet {
    services: Vec<Service>,
    structures: HashMap<String, Structure>,
    pub results: HashMap<String, HashMap<String, u16>>,
}

impl ServiceSet {
    pub fn new() -> Self {
        ServiceSet { structures: HashMap::new(), results: HashMap::new(), services: vec![] }
    }

    pub fn get_structure(&self, key: &String) -> Result<&Structure, Error> {
        if let Some(s) = self.structures.get(key) {
            return Ok(s);
        }
        Err(format_err!("unknown structure type used: {}", key))
    }

    pub fn parse_service_file<W: std::io::Read>(&mut self, mut svc_file: W) -> Result<(), Error> {
        let mut contents = String::new();
        svc_file.read_to_string(&mut contents)?;
        let ast: Ast = serde_json::from_str(contents.as_str())?;

        self.results.extend(ast.results);
        self.structures.extend(ast.structures);
        self.services.extend(ast.services);
        Ok(())
    }

    pub fn get_services<'a>(&'a self) -> impl Iterator<Item = &'a Service> {
        self.services.iter()
    }
}

pub fn result_map<'de, D>(
    deserializer: D,
) -> Result<HashMap<String, HashMap<String, u16>>, D::Error>
where
    D: Deserializer<'de>,
{
    let mut map = HashMap::new();
    for item in HashMap::<String, HashMap<String, String>>::deserialize(deserializer)? {
        let mut inner_map = HashMap::new();
        for (code, s) in item.1 {
            assert_eq!(&s[0..2], "0x");
            inner_map.insert(code, u16::from_str_radix(&s[2..], 16).map_err(de::Error::custom)?);
        }
        map.insert(item.0.clone(), inner_map);
    }
    Ok(map)
}

pub fn structure_map<'de, D>(deserializer: D) -> Result<HashMap<String, Structure>, D::Error>
where
    D: Deserializer<'de>,
{
    let mut map = HashMap::new();
    for item in Vec::<Structure>::deserialize(deserializer)? {
        map.insert(item.ty.clone(), item);
    }
    Ok(map)
}

fn from_hex_str<'de, D>(deserializer: D) -> Result<u16, D::Error>
where
    D: Deserializer<'de>,
{
    let s = String::deserialize(deserializer)?;
    assert_eq!(&s[0..2], "0x");
    u16::from_str_radix(&s[2..], 16).map_err(de::Error::custom)
}
