// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryFrom;

use crate::error::Error;
use crate::types::ModelId;

// TODO(fxbug.dev/97159): Load this from the structured configuration library.
#[derive(Clone, Debug, PartialEq)]
pub struct Config {
    pub model_id: ModelId,
    pub firmware_revision: String,
}

impl Config {
    // TODO(fxbug.dev/97159): Load component config from the structured configuration library.
    pub fn load() -> Result<Self, Error> {
        Ok(Self::example_config())
    }

    pub fn example_config() -> Self {
        Self {
            model_id: ModelId::try_from(1).expect("valid ID"),
            firmware_revision: "1.0.0".to_string(),
        }
    }
}
