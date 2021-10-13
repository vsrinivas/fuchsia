// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::task::Task;
use crate::types::*;

/// Parses and runs the features from the provided "program strvec."
pub fn run_features<'a>(entries: &'a Vec<String>, _task: &Task) -> Result<(), Errno> {
    for entry in entries {
        match entry.as_str() {
            "wayland" => {
                // TODO: Create a thread and start handling the wayland communication.
            }
            feature => {
                log::warn!("Unsupported feature: {:?}", feature);
            }
        }
    }
    Ok(())
}
