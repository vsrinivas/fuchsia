// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    diagnostics_data::{LogsData, Timestamp},
    serde::{Deserialize, Serialize},
    std::time::SystemTime,
};

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub enum EventType {
    LoggingStarted,
    TargetDisconnected,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub enum LogData {
    TargetLog(LogsData),
    SymbolizedTargetLog(LogsData, String),
    MalformedTargetLog(String),
    FfxEvent(EventType),
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct LogEntry {
    pub data: LogData,
    pub timestamp: Timestamp,
    pub version: u64,
}

impl LogEntry {
    pub fn new(data: LogData) -> Result<Self> {
        Ok(LogEntry {
            data: data,
            version: 1,
            timestamp: Timestamp::from(
                SystemTime::now()
                    .duration_since(SystemTime::UNIX_EPOCH)
                    .context("system time before Unix epoch")?
                    .as_nanos() as i64,
            ),
        })
    }
}

impl From<LogsData> for LogData {
    fn from(data: LogsData) -> Self {
        Self::TargetLog(data)
    }
}
