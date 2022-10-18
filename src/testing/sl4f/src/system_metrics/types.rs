// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

#[derive(Deserialize, Debug)]
pub struct StartLoggingRequest {
    pub interval_ms: u32,
    pub duration_ms: u32,
}

#[derive(Deserialize, Debug)]
pub struct StartLoggingForeverRequest {
    pub interval_ms: u32,
}

#[derive(Serialize, Debug)]
pub enum CpuLoadLoggerResult {
    Success,
}
