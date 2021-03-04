// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::history::History, serde::Serialize};

/// Wrapper for serializing the epoch.json file.
#[derive(Serialize)]
#[cfg_attr(test, derive(Debug, PartialEq))]
#[serde(tag = "version", deny_unknown_fields)]
pub enum Epoch {
    #[serde(rename = "1")]
    Version1 { epoch: u64 },
}

impl From<History> for Epoch {
    fn from(history: History) -> Self {
        Self::Version1 { epoch: history.epoch }
    }
}

#[cfg(test)]
mod test {
    use {super::*, proptest::prelude::*, serde_json::json};

    proptest! {
        #[test]
        fn serialize(epoch: u64) {
            assert_eq!(
                serde_json::to_value(Epoch::Version1 { epoch }).unwrap(),
                json!({
                    "version": "1",
                    "epoch": epoch,
                })
            )
        }

        #[test]
        fn history_to_epoch(epoch: u64) {
            assert_eq!(Epoch::from(History{epoch}), Epoch::Version1{epoch})
        }
    }
}
