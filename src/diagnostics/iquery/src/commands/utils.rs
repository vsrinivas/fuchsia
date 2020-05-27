// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::types::Error, serde_json};

/// Returns the moniker in a json response or an error if one is not found
pub fn get_moniker_from_result(result: &serde_json::Value) -> Result<String, Error> {
    Ok(result
        .get("moniker")
        .ok_or(Error::archive_missing_property("moniker"))?
        .as_str()
        .ok_or(Error::ArchiveInvalidJson)?
        .to_string())
}
