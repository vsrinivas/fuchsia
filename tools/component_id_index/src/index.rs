// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use anyhow::Context;

use serde::{Deserialize, Serialize};
use std::fmt;
use std::fs;
use std::str;

#[derive(Serialize, Deserialize, Debug, PartialEq, Clone)]
pub struct AppmgrMoniker {
    pub url: String,
    pub realm_path: Vec<String>,
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Clone)]
pub struct InstanceIdEntry {
    pub instance_id: Option<String>,
    pub appmgr_moniker: AppmgrMoniker,
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Clone)]
pub struct Index {
    pub appmgr_restrict_isolated_persistent_storage: Option<bool>,
    pub instances: Vec<InstanceIdEntry>,
}

impl Index {
    pub fn from_file(path: &str) -> anyhow::Result<Index> {
        let contents = fs::read_to_string(path).context("unable to read file")?;
        serde_json5::from_str::<Index>(&contents).context("unable to parse to json5")
    }
}

pub fn gen_instance_id(rng: &mut impl rand::Rng) -> String {
    // generate random 256bits into a byte array
    let mut num: [u8; 256 / 8] = [0; 256 / 8];
    rng.fill_bytes(&mut num);
    // turn the byte array into a lower-cased hex string.
    num.iter().map(|byte| format!("{:02x}", byte)).collect::<Vec<String>>().join("")
}

pub fn is_valid_instance_id(id: &str) -> bool {
    // An instance ID is a lower-cased hex string of 256-bits.
    // 256 bits in base16 = 64 chars (1 char to represent 4 bits)
    id.len() == 64 && id.chars().all(|ch| (ch.is_numeric() || ch.is_lowercase()) && ch.is_digit(16))
}

// GenerateInstanceIds wraps a list of index entries which have missing instance IDs and provides a
// fmt::Display impl which suggests random instance IDs to use.
#[derive(Debug, PartialEq)]
pub struct GenerateInstanceIds(pub Vec<InstanceIdEntry>);

impl GenerateInstanceIds {
    pub fn new(rng: &mut impl rand::Rng, entries: Vec<InstanceIdEntry>) -> GenerateInstanceIds {
        GenerateInstanceIds(
            (0..entries.len())
                .map(|i| {
                    let mut with_id = entries[i].clone();
                    with_id.instance_id = Some(gen_instance_id(rng));
                    with_id
                })
                .collect::<Vec<InstanceIdEntry>>(),
        )
    }
}

impl fmt::Display for GenerateInstanceIds {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f,
               "Some entries are missing `instance_id` fields. Here are some generated IDs for you:\n{}\n\nSee https://fuchsia.dev/fuchsia-src/development/components/component_id_index#defining_an_index for more details.",
               serde_json::to_string_pretty(&self.0).unwrap())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use proptest::prelude::*;
    use rand::*;

    fn gen_index(num_instances: u32) -> Index {
        Index {
            appmgr_restrict_isolated_persistent_storage: None,
            instances: (0..num_instances)
                .map(|i| InstanceIdEntry {
                    instance_id: Some(gen_instance_id(&mut rand::thread_rng())),
                    appmgr_moniker: AppmgrMoniker {
                        url: format!(
                            "fuchsia-pkg://example.com/fake_pkg#meta/fake_component_{}.cmx",
                            i
                        ),
                        realm_path: vec!["root".to_string(), "child".to_string(), i.to_string()],
                    },
                })
                .collect(),
        }
    }

    #[test]
    fn formatting_for_missing_ids_error() {
        let mut index = gen_index(2);
        index.instances[0].instance_id =
            Some("0000000000000000000000000000000000000000000000000000000000000000".to_string());
        index.instances[1].instance_id =
            Some("1111111111111111111111111111111111111111111111111111111111111111".to_string());
        let actual = format!("{}", GenerateInstanceIds(index.instances.clone()));
        assert_eq!(
            r#"Some entries are missing `instance_id` fields. Here are some generated IDs for you:
[
  {
    "instance_id": "0000000000000000000000000000000000000000000000000000000000000000",
    "appmgr_moniker": {
      "url": "fuchsia-pkg://example.com/fake_pkg#meta/fake_component_0.cmx",
      "realm_path": [
        "root",
        "child",
        "0"
      ]
    }
  },
  {
    "instance_id": "1111111111111111111111111111111111111111111111111111111111111111",
    "appmgr_moniker": {
      "url": "fuchsia-pkg://example.com/fake_pkg#meta/fake_component_1.cmx",
      "realm_path": [
        "root",
        "child",
        "1"
      ]
    }
  }
]

See https://fuchsia.dev/fuchsia-src/development/components/component_id_index#defining_an_index for more details."#,
            &actual
        );
    }

    #[test]
    fn unique_gen_instance_id() {
        let seed = rand::thread_rng().next_u64();
        println!("using seed {}", seed);
        let mut rng = rand::rngs::StdRng::seed_from_u64(seed);
        let mut prev_id = gen_instance_id(&mut rng);
        for _i in 0..40 {
            let id = gen_instance_id(&mut rng);
            assert!(prev_id != id);
            prev_id = id;
        }
    }

    #[test]
    fn valid_gen_instance_id() {
        let seed = rand::thread_rng().next_u64();
        println!("using seed {}", seed);
        let mut rng = rand::rngs::StdRng::seed_from_u64(seed);
        for _i in 0..40 {
            assert!(is_valid_instance_id(&gen_instance_id(&mut rng)));
        }
    }

    proptest! {
        #[test]
        fn valid_instance_id(id in "[a-f0-9]{64}") {
            prop_assert_eq!(true, is_valid_instance_id(&id));
        }
    }

    #[test]
    fn invalid_instance_id() {
        // Invalid lengths
        assert!(!is_valid_instance_id("8c90d44863ff67586cf6961081feba4f760decab8bbbee376a3bfbc77"));
        assert!(!is_valid_instance_id("8c90d44863ff67586cf6961081feba4f760decab8bbbee376a"));
        assert!(!is_valid_instance_id("8c90d44863ff67586cf6961081"));
        // upper case chars are invalid
        assert!(!is_valid_instance_id(
            "8C90D44863FF67586CF6961081FEBA4F760DECAB8BBBEE376A3BFBC77B351280"
        ));
        // hex chars only
        assert!(!is_valid_instance_id(
            "8x90d44863ff67586cf6961081feba4f760decab8bbbee376a3bfbc77b351280"
        ));
        assert!(!is_valid_instance_id(
            "8;90d44863ff67586cf6961081feba4f760decab8bbbee376a3bfbc77b351280"
        ));
    }
}
