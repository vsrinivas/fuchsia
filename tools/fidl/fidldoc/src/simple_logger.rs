// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use log::{Level, Metadata, Record};

pub struct SimpleLogger;

impl log::Log for SimpleLogger {
    fn enabled(&self, metadata: &Metadata) -> bool {
        metadata.level() <= Level::Debug
    }

    fn log(&self, record: &Record) {
        if self.enabled(record.metadata()) {
            println!("{} - {}", record.level(), record.args());
        }
    }

    fn flush(&self) {}
}

#[cfg(test)]
mod test {
    use super::*;
    use log::{Log, MetadataBuilder};

    #[test]
    fn enabled_test() {
        let logger = SimpleLogger {};

        let debug_metadata = MetadataBuilder::new().level(Level::Debug).target("fidldoc").build();
        assert!(logger.enabled(&debug_metadata));

        let trace_metadata = MetadataBuilder::new().level(Level::Trace).target("fidldoc").build();
        assert!(!logger.enabled(&trace_metadata));
    }
}
