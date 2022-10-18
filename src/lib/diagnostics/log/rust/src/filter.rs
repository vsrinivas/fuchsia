// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use diagnostics_log_encoding::{Severity, SeverityExt};
use fidl_fuchsia_diagnostics::Interest;
use fidl_fuchsia_diagnostics_stream::Record;
use fidl_fuchsia_logger::LogSinkProxy;
use parking_lot::{Mutex, RwLock};
use std::{future::Future, sync::Arc};
use tracing::{subscriber::Subscriber, Metadata};
use tracing_subscriber::layer::{Context, Layer};

use crate::OnInterestChanged;

pub(crate) struct InterestFilter {
    min_severity: Arc<RwLock<Severity>>,
    listener: Arc<Mutex<Option<Box<dyn OnInterestChanged + Send + Sync + 'static>>>>,
}

impl InterestFilter {
    /// Constructs a new `InterestFilter` and a future which should be polled to listen
    /// to changes in the LogSink's interest.
    pub fn new(proxy: LogSinkProxy, interest: Interest) -> (Self, impl Future<Output = ()>) {
        let default_severity = interest.min_severity.unwrap_or(Severity::Info);
        let min_severity = Arc::new(RwLock::new(default_severity));
        let listener = Arc::new(Mutex::new(None));
        let filter = Self { min_severity: min_severity.clone(), listener: listener.clone() };
        (filter, Self::listen_to_interest_changes(listener, default_severity, min_severity, proxy))
    }

    /// Sets the interest listener.
    pub fn set_interest_listener<T>(&self, listener: T)
    where
        T: OnInterestChanged + Send + Sync + 'static,
    {
        *self.listener.lock() = Some(Box::new(listener));
    }

    async fn listen_to_interest_changes(
        listener: Arc<Mutex<Option<Box<dyn OnInterestChanged + Send + Sync>>>>,
        default_severity: Severity,
        min_severity: Arc<RwLock<Severity>>,
        proxy: LogSinkProxy,
    ) {
        while let Ok(Ok(interest)) = proxy.wait_for_interest_change().await {
            *min_severity.write() = interest.min_severity.unwrap_or(default_severity);
            if let Some(callback) = &*listener.lock() {
                callback.on_changed(&*min_severity.read());
            }
        }
    }

    #[allow(unused)] // TODO(fxbug.dev/62858) remove attribute
    pub fn enabled_for_testing(&self, _file: &str, _line: u32, record: &Record) -> bool {
        record.severity >= *self.min_severity.read()
    }
}

impl<S: Subscriber> Layer<S> for InterestFilter {
    /// Always returns `sometimes` so that we can later change the filter on the fly.
    fn register_callsite(
        &self,
        _metadata: &'static Metadata<'static>,
    ) -> tracing::subscriber::Interest {
        tracing::subscriber::Interest::sometimes()
    }

    fn enabled(&self, metadata: &Metadata<'_>, _ctx: Context<'_, S>) -> bool {
        metadata.severity() >= *self.min_severity.read()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_logger::{LogSinkMarker, LogSinkRequest, LogSinkRequestStream};
    use fuchsia_async as fasync;
    use futures::{channel::mpsc, StreamExt, TryStreamExt};
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

    struct InterestChangedListener(mpsc::UnboundedSender<()>);

    impl OnInterestChanged for InterestChangedListener {
        fn on_changed(&self, _: &Severity) {
            self.0.unbounded_send(()).unwrap();
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn default_filter_is_info_when_unspecified() {
        let (proxy, _requests) = create_proxy_and_stream::<LogSinkMarker>().unwrap();
        let (filter, _on_changes) = InterestFilter::new(proxy, Interest::EMPTY);
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

    async fn send_interest_change(stream: &mut LogSinkRequestStream, severity: Option<Severity>) {
        match stream.try_next().await {
            Ok(Some(LogSinkRequest::WaitForInterestChange { responder })) => {
                responder
                    .send(&mut Ok(Interest { min_severity: severity, ..Interest::EMPTY }))
                    .expect("send response");
            }
            other => panic!("Expected WaitForInterestChange but got {:?}", other),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn default_filter_on_interest_changed() {
        let (proxy, mut requests) = create_proxy_and_stream::<LogSinkMarker>().unwrap();
        let (filter, on_changes) = InterestFilter::new(
            proxy,
            Interest { min_severity: Some(Severity::Warn), ..Interest::EMPTY },
        );
        let (send, mut recv) = mpsc::unbounded();
        filter.set_interest_listener(InterestChangedListener(send));
        let _on_changes_task = fasync::Task::spawn(on_changes);
        let observed = Arc::new(Mutex::new(SeverityCount::default()));
        tracing::subscriber::set_global_default(
            Registry::default().with(SeverityTracker { counts: observed.clone() }).with(filter),
        )
        .unwrap();

        // After overriding to info, filtering is at info level. The mpsc channel is used to
        // get a signal as to when the filter has processed the update.
        send_interest_change(&mut requests, Some(Severity::Info)).await;
        let _ = recv.next().await;

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

        // After resetting to default, filtering is at warn level.
        send_interest_change(&mut requests, None).await;
        let _ = recv.next().await;

        error!("oops");
        expected.num_error += 1;
        assert_eq!(&*observed.lock().unwrap(), &expected);

        warn!("maybe");
        expected.num_warn += 1;
        assert_eq!(&*observed.lock().unwrap(), &expected);

        info!("ok");
        assert_eq!(&*observed.lock().unwrap(), &expected, "should not increment counters");

        debug!("hint");
        assert_eq!(&*observed.lock().unwrap(), &expected, "should not increment counters");

        trace!("spew");
        assert_eq!(&*observed.lock().unwrap(), &expected, "should not increment counters");
    }
}
