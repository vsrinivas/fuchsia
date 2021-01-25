// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    diagnostics_data::{LogsData, Timestamp},
    serde::{Deserialize, Serialize},
};

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub enum EventType {
    LoggingStarted,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub enum LogData {
    TargetLog(LogsData),
    MalformedTargetLog(String),
    FfxEvent(EventType),
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct LogEntry {
    pub data: LogData,
    pub timestamp: Timestamp,
    pub version: u64,
}
