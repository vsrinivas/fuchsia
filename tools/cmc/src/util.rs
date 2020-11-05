// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::Error,
    serde_json::Value,
    serde_json5,
    std::{convert::TryInto, fs, io::Read, path::PathBuf},
};

/// read a JSON or JSON5 file.
/// Attempts to parse as JSON first.
/// If this fails, attempts to parse as JSON5.
/// Parsing with serde_json5 is known to be much slower, so we try the faster
/// parser first.
pub fn json_or_json5_from_file(file: &PathBuf) -> Result<Value, Error> {
    let mut buffer = String::new();
    fs::File::open(&file)?.read_to_string(&mut buffer)?;

    serde_json::from_str(&buffer).or_else(|_| {
        // If JSON parsing fails, try JSON5 parsing (which is slower)
        serde_json5::from_str(&buffer).map_err(|e| {
            Error::parse(
                format!("Couldn't read {:#?} as JSON: {}", file, e),
                e.try_into().ok(),
                Some(file.as_path()),
            )
        })
    })
}
