// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, diagnostics_log::Publisher, fidl::endpoints::Proxy, fidl_fuchsia_io as fio,
    fidl_fuchsia_logger::LogSinkProxy, fuchsia_async::Task, std::path::Path,
};

pub struct ScopedLogger {
    publisher: Publisher,
    _interest_listener: Task<()>,
}

impl ScopedLogger {
    pub fn from_directory(dir: &fio::DirectoryProxy, path: &str) -> Result<Self, Error> {
        let log_sink_node = fuchsia_fs::open_node(
            dir,
            &Path::new(path.trim_start_matches("/")),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_SERVICE,
        )?;
        let sink = LogSinkProxy::from_channel(log_sink_node.into_channel().unwrap());
        let publish_opts = Default::default();
        let (publisher, interest_listener) = Publisher::new_with_proxy(sink, publish_opts)?;
        Ok(Self { publisher, _interest_listener: Task::spawn(interest_listener) })
    }
}

impl tracing::Subscriber for ScopedLogger {
    fn enabled(&self, metadata: &tracing::Metadata<'_>) -> bool {
        self.publisher.enabled(metadata)
    }

    fn new_span(&self, span: &tracing::span::Attributes<'_>) -> tracing::span::Id {
        self.publisher.new_span(span)
    }

    fn record(&self, span: &tracing::span::Id, values: &tracing::span::Record<'_>) {
        self.publisher.record(span, values)
    }

    fn record_follows_from(&self, span: &tracing::span::Id, follows: &tracing::span::Id) {
        self.publisher.record_follows_from(span, follows)
    }

    fn event(&self, event: &tracing::Event<'_>) {
        self.publisher.event(event)
    }

    fn enter(&self, span: &tracing::span::Id) {
        self.publisher.enter(span)
    }

    fn exit(&self, span: &tracing::span::Id) {
        self.publisher.exit(span)
    }
}
