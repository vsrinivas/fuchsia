// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_diagnostics::Severity;
use std::sync::Once;
use tracing::Level;
use tracing_log::LogTracer;

static START: Once = Once::new();

pub(crate) fn init(minimum_severity: Option<Severity>) {
    START.call_once(|| {
        let max_level = match minimum_severity {
            Some(Severity::Trace) => Level::TRACE,
            Some(Severity::Debug) => Level::DEBUG,
            None | Some(Severity::Info) => Level::INFO,
            Some(Severity::Warn) => Level::WARN,
            Some(Severity::Error) | Some(Severity::Fatal) => Level::ERROR,
        };
        tracing_subscriber::fmt().with_writer(std::io::stderr).with_max_level(max_level).init();
        let _ = LogTracer::init();
    })
}
