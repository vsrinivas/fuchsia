// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::message::Severity;
use diagnostics_data::LogsData;
use fuchsia_inspect::{IntProperty, Node, NumericProperty, Property, UintProperty};
use fuchsia_inspect_derive::Inspect;

#[derive(Default, Inspect)]
pub struct LogStreamStats {
    last_timestamp: IntProperty,
    total: UintProperty,
    dropped: UintProperty,
    fatal: UintProperty,
    error: UintProperty,
    warn: UintProperty,
    info: UintProperty,
    debug: UintProperty,
    trace: UintProperty,

    inspect_node: Node,
}

impl LogStreamStats {
    pub fn increment_dropped(&self) {
        self.dropped.add(1);
    }

    pub fn ingest_message(&self, msg: &LogsData) {
        self.last_timestamp.set(msg.metadata.timestamp.into());
        self.total.add(1);
        match msg.metadata.severity {
            Severity::Trace => self.trace.add(1),
            Severity::Debug => self.debug.add(1),
            Severity::Info => self.info.add(1),
            Severity::Warn => self.warn.add(1),
            Severity::Error => self.error.add(1),
            Severity::Fatal => self.fatal.add(1),
        }
    }
}
