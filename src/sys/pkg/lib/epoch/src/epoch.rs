// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

/// Wrapper for serializing and deserializing the epoch.json file. For more context, see
/// [RFC-0071](https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0071_ota_backstop).
#[derive(Serialize, Deserialize, Debug, PartialEq, Clone)]
#[serde(tag = "version", deny_unknown_fields)]
#[allow(missing_docs)]
pub enum EpochFile {
    #[serde(rename = "1")]
    Version1 { epoch: u64 },
}

#[cfg(test)]
mod tests {
    use {super::*, proptest::prelude::*, serde_json::json};

    proptest! {
        #[test]
        fn test_json_serialize_roundtrip(epoch: u64) {
            // Generate json and show that it successfully deserializes into the wrapper object.
            let starting_json_value =
            json!({
                "version": "1",
                "epoch": epoch
            });

            let deserialized_object: EpochFile =
                serde_json::from_value(starting_json_value.clone())
                    .expect("json to deserialize");
            assert_eq!(deserialized_object, EpochFile::Version1 { epoch });

            // Serialize back into serde_json::Value object & show we get same json we started with.
            // Note: even though serialize generally means "convert to string", in this case we're
            // serializing to a serde_json::Value to ignore ordering when we check equality.
            let final_json_value =
                serde_json::to_value(&deserialized_object)
                    .expect("serialize to value");
            assert_eq!(final_json_value, starting_json_value);
        }
    }
}
