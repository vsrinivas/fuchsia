// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::logs::stored_message::StoredMessage;
use diagnostics_data::Severity;
use fuchsia_inspect::{IntProperty, Node, NumericProperty, Property, UintProperty};
use fuchsia_inspect_derive::Inspect;

#[derive(Debug, Default, Inspect)]
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
    pub fn increment_dropped(&self, msg: &StoredMessage) {
        self.dropped.count(msg);
    }

    pub fn ingest_message(&self, msg: &StoredMessage) {
        self.last_timestamp.set(msg.timestamp());
        self.total.count(msg);
        match msg.severity() {
            Severity::Trace => self.trace.count(msg),
            Severity::Debug => self.debug.count(msg),
            Severity::Info => self.info.count(msg),
            Severity::Warn => self.warn.count(msg),
            Severity::Error => self.error.count(msg),
            Severity::Fatal => self.fatal.count(msg),
        }
    }
}

#[derive(Debug, Default, Inspect)]
struct LogCounter {
    number: UintProperty,
    bytes: UintProperty,

    inspect_node: Node,
}

impl LogCounter {
    fn count(&self, msg: &StoredMessage) {
        self.number.add(1);
        self.bytes.add(msg.size() as u64);
    }
}
