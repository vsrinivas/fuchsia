// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_data::Severity;
use serde::Deserialize;

#[derive(Debug, Deserialize)]
pub struct TestSpec {
    pub name: String,
    pub kind: TestKind,
    pub logging: Option<LoggingSpec>,
    #[serde(default)]
    pub panics: bool,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum TestKind {
    Binary,
    Test,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct LoggingSpec {
    pub tags: Vec<String>,
    pub min_severity: Option<Severity>,
}
