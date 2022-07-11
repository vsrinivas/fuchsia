// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

// Analytical information about a capability.
#[derive(Debug, Serialize, Deserialize)]
pub struct Diagnosis {
    pub is_error: bool,
    pub report_type: String,
    pub capability: String,
    pub summary: Option<String>,
}

// Information about a component, including the status of its `Use` and `Expose` capabilities.
#[derive(Serialize, Deserialize)]
pub struct Analysis {
    pub url: String,
    pub instance_id: Option<String>,
    pub diagnoses: Vec<Diagnosis>,
}
