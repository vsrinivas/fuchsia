// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

#[derive(Deserialize, Debug)]
pub struct StartLoggingRequest {
    pub sampling_interval_ms: u32,
    pub statistics_interval_ms: Option<u32>,
    pub duration_ms: u32,
}

#[derive(Deserialize, Debug)]
pub struct StartLoggingForeverRequest {
    pub sampling_interval_ms: u32,
    pub statistics_interval_ms: Option<u32>,
}

#[derive(Serialize, Debug)]
pub enum MetricsLoggerResult {
    Success,
}
