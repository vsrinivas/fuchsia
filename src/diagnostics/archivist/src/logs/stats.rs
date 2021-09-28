// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_data::{LogsData, Severity};
use fuchsia_inspect::{IntProperty, Node, NumericProperty, Property, UintProperty};
use fuchsia_inspect_derive::Inspect;

#[derive(Default, Inspect)]
pub struct LogStreamStats {
    last_timestamp: IntProperty,
    total: LogCounter,
    dropped: LogCounter,
    fatal: LogCounter,
    error: LogCounter,
    warn: LogCounter,
    info: LogCounter,
    debug: LogCounter,
    trace: LogCounter,

    inspect_node: Node,
}

impl LogStreamStats {
    pub fn increment_dropped(&self, msg: &LogsData) {
        self.dropped.count(msg);
    }

    pub fn ingest_message(&self, msg: &LogsData) {
        self.last_timestamp.set(msg.metadata.timestamp.into());
        self.total.count(msg);
        match msg.metadata.severity {
            Severity::Trace => self.trace.count(msg),
            Severity::Debug => self.debug.count(msg),
            Severity::Info => self.info.count(msg),
            Severity::Warn => self.warn.count(msg),
            Severity::Error => self.error.count(msg),
            Severity::Fatal => self.fatal.count(msg),
        }
    }
}

#[derive(Default, Inspect)]
struct LogCounter {
    number: UintProperty,
    bytes: UintProperty,

    inspect_node: Node,
}

impl LogCounter {
    fn count(&self, msg: &LogsData) {
        self.number.add(1);
        self.bytes.add(msg.metadata.size_bytes as u64);
    }
}
