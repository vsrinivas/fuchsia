// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use arc_swap::ArcSwap;
use diagnostics_log_encoding::{Severity, SeverityExt};
use fidl_fuchsia_diagnostics_stream::Record;
use fidl_fuchsia_logger::{LogSinkEvent, LogSinkEventStream};
use futures::StreamExt;
use std::{future::Future, sync::Arc};
use tracing::{
    subscriber::{Interest, Subscriber},
    Metadata,
};
use tracing_subscriber::layer::{Context, Layer};

pub(crate) struct InterestFilter {
    min_severity: ArcSwap<Severity>,
}

impl InterestFilter {
    /// Constructs a new `InterestFilter` and a future which should be polled to listen
    /// to changes in the LogSink's interest.
    pub fn new(events: LogSinkEventStream) -> (Self, impl Future<Output = ()>) {
        let min_severity = ArcSwap::new(Arc::new(Severity::Info));
        let filter = Self { min_severity: min_severity.clone() };
        (filter, Self::listen_to_interest_changes(min_severity, events))
    }

    async fn listen_to_interest_changes(
        min_severity: ArcSwap<Severity>,
        mut events: LogSinkEventStream,
    ) {
        while let Some(Ok(LogSinkEvent::OnInterestChanged { interest })) = events.next().await {
            let new_severity = Arc::new(interest.min_severity.unwrap_or(Severity::Info));
            min_severity.store(new_severity);
        }
    }

    #[allow(unused)] // TODO(fxbug.dev/62858) remove attribute
    fn enabled_for_testing(&self, _file: &str, _line: u32, record: &Record) -> bool {
        record.severity >= **self.min_severity.load()
    }
}

impl<S: Subscriber> Layer<S> for InterestFilter {
    /// Always returns `sometimes` so that we can later change the filter on the fly.
    fn register_callsite(&self, _metadata: &'static Metadata<'static>) -> Interest {
        Interest::sometimes()
    }

    fn enabled(&self, metadata: &Metadata<'_>, _ctx: Context<'_, S>) -> bool {
        metadata.severity() >= **self.min_severity.load()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_logger::LogSinkMarker;
    use std::sync::Mutex;
    use tracing::{debug, error, info, trace, warn, Event};
    use tracing_subscriber::{layer::SubscriberExt, Registry};

    struct SeverityTracker {
        counts: Arc<Mutex<SeverityCount>>,
    }

    #[derive(Debug, Default, Eq, PartialEq)]
    struct SeverityCount {
        num_trace: u64,
        num_debug: u64,
        num_info: u64,
        num_warn: u64,
        num_error: u64,
    }

    impl<S: Subscriber> Layer<S> for SeverityTracker {
        fn on_event(&self, event: &Event<'_>, _cx: Context<'_, S>) {
            let mut count = self.counts.lock().unwrap();
            let to_increment = match event.metadata().severity() {
                Severity::Trace => &mut count.num_trace,
                Severity::Debug => &mut count.num_debug,
                Severity::Info => &mut count.num_info,
                Severity::Warn => &mut count.num_warn,
                Severity::Error => &mut count.num_error,
                Severity::Fatal => unreachable!("tracing crate doesn't have a fatal level"),
            };
            *to_increment += 1;
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn default_filter_is_info() {
        let (proxy, _requests) = create_proxy_and_stream::<LogSinkMarker>().unwrap();
        let (filter, _on_changes) = InterestFilter::new(proxy.take_event_stream());
        let observed = Arc::new(Mutex::new(SeverityCount::default()));
        tracing::subscriber::set_global_default(
            Registry::default().with(SeverityTracker { counts: observed.clone() }).with(filter),
        )
        .unwrap();
        let mut expected = SeverityCount::default();

        error!("oops");
        expected.num_error += 1;
        assert_eq!(&*observed.lock().unwrap(), &expected);

        warn!("maybe");
        expected.num_warn += 1;
        assert_eq!(&*observed.lock().unwrap(), &expected);

        info!("ok");
        expected.num_info += 1;
        assert_eq!(&*observed.lock().unwrap(), &expected);

        debug!("hint");
        assert_eq!(&*observed.lock().unwrap(), &expected, "should not increment counters");

        trace!("spew");
        assert_eq!(&*observed.lock().unwrap(), &expected, "should not increment counters");
    }
}
