// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Error},
    async_utils::hanging_get::client::GeneratedFutureStream,
    fidl_fuchsia_time_external::{self as ftexternal, PushSourceProxy, Status},
    fuchsia_async::futures::Stream,
    fuchsia_component::client::{launch, launcher, App},
    fuchsia_zircon as zx,
    futures::{stream::Select, FutureExt, StreamExt, TryFutureExt},
    std::{pin::Pin, sync::Arc},
};

/// An event that may be observed from a source of time.
#[derive(Debug, PartialEq, Clone, Copy)]
pub enum Event {
    /// The status of the time source changed.
    StatusChange {
        /// The current status of the time source.
        status: Status,
    },
    /// The time source produced a new time sample.
    TimeSample {
        /// The UTC time.
        utc: zx::Time,
        /// The monotonic time at which the UTC was most valid.
        monotonic: zx::Time,
    },
}

/// A definition of a time source that may subsequently be launched to create a stream of update
/// events.
pub trait TimeSource {
    /// The type of `Stream` produced when launching the `TimeSource`.
    type EventStream: Stream<Item = Result<Event, Error>> + Unpin;

    /// Attempts to launch the time source and return a stream of its time output and status
    /// change events.
    fn launch(self) -> Result<Self::EventStream, Error>;
}

/// A time source that communicates using the `fuchsia.time.external.PushSource` protocol.
pub struct PushTimeSource {
    /// The fully qualified name of the component to launch.
    component: String,
}

/// The `Stream` of events produced by a `PushTimeSource`
type PushTimeSourceEventStream = Select<
    Pin<Box<dyn Stream<Item = Result<Event, Error>> + Send>>,
    Pin<Box<dyn Stream<Item = Result<Event, Error>> + Send>>,
>;

#[allow(unused)]
impl PushTimeSource {
    /// Creates a new `PushTimeSource` using the supplied component name.
    pub fn new(component: String) -> Self {
        PushTimeSource { component }
    }

    /// Returns a stream of time output and status change events recieved using the supplied
    /// `PushSourceProxy`, retaining the optional `App` for the same lifetime.
    fn events_from_proxy(app: Option<App>, proxy: PushSourceProxy) -> PushTimeSourceEventStream {
        // Store the App in a tuple with the PushSourceProxy to ensure it remains in scope.
        let app_and_proxy = Arc::new((app, proxy));
        let app_and_proxy_clone = Arc::clone(&app_and_proxy);

        let status_stream = GeneratedFutureStream::new(Box::new(move || {
            Some(
                app_and_proxy
                    .1
                    .watch_status()
                    .map_ok(|status| Event::StatusChange { status })
                    .err_into(),
            )
        }));
        let sample_stream = GeneratedFutureStream::new(Box::new(move || {
            Some(app_and_proxy_clone.1.watch_sample().map(|result| match result {
                Ok(sample) => match (sample.utc, sample.monotonic) {
                    (None, _) => Err(anyhow!("sample missing utc")),
                    (_, None) => Err(anyhow!("sample missing monotonic")),
                    (Some(utc), Some(monotonic)) => Ok(Event::TimeSample {
                        utc: zx::Time::from_nanos(utc),
                        monotonic: zx::Time::from_nanos(monotonic),
                    }),
                },
                Err(err) => Err(err.into()),
            }))
        }));

        futures::stream::select(status_stream.boxed(), sample_stream.boxed())
    }
}

impl TimeSource for PushTimeSource {
    type EventStream = PushTimeSourceEventStream;

    fn launch(self) -> Result<Self::EventStream, Error> {
        let launcher = launcher().context("starting launcher")?;
        let app = launch(&launcher, self.component.clone(), None)
            .context(format!("launching push source {}", self.component))?;
        let proxy = app.connect_to_service::<ftexternal::PushSourceMarker>()?;
        Ok(PushTimeSource::events_from_proxy(Some(app), proxy))
    }
}

#[cfg(test)]
use futures::stream;

/// A time source that immediately produces a fixed list of events supplied at construction.
#[cfg(test)]
pub struct FakeTimeSource {
    /// The error to return on launch, if any.
    launch_err: Option<Error>,
    /// The events to return.
    events: Vec<Result<Event, Error>>,
    /// Whether to return pending after the last event. If false the stream will end.
    pending: bool,
}

#[cfg(test)]
impl FakeTimeSource {
    /// Creates a new `FakeTimeSource` that produces the supplied events then returns pending.
    pub fn events_then_pending(events: Vec<Event>) -> Self {
        FakeTimeSource {
            launch_err: None,
            events: events.into_iter().map(|evt| Ok(evt)).collect(),
            pending: true,
        }
    }

    /// Creates a new `FakeTimeSource` that produces the supplied events then return EOFs.
    pub fn events_then_terminate(events: Vec<Event>) -> Self {
        FakeTimeSource {
            launch_err: None,
            events: events.into_iter().map(|evt| Ok(evt)).collect(),
            pending: false,
        }
    }

    /// Creates a new `FakeTimeSource` that will return an error when told to launch.
    pub fn failing() -> Self {
        FakeTimeSource {
            launch_err: Some(anyhow!("FakeTimeSource set to fail")),
            events: vec![],
            pending: true,
        }
    }
}

#[cfg(test)]
impl TimeSource for FakeTimeSource {
    type EventStream = Pin<Box<dyn Stream<Item = Result<Event, Error>> + Send>>;

    fn launch(self) -> Result<Self::EventStream, Error> {
        if let Some(err) = self.launch_err {
            return Err(err);
        }
        if self.pending {
            Ok(stream::iter(self.events).chain(stream::pending()).boxed())
        } else {
            Ok(stream::iter(self.events).boxed())
        }
    }
}

#[cfg(test)]
mod test {
    use {super::*, fuchsia_async as fasync, lazy_static::lazy_static};

    const STATUS_1: Status = Status::Initializing;
    const SAMPLE_1_UTC_NANOS: i64 = 1234567;
    const SAMPLE_1_MONO_NANOS: i64 = 222;

    lazy_static! {
        static ref STATUS_EVENT_1: Event = Event::StatusChange { status: STATUS_1 };
        static ref SAMPLE_EVENT_1: Event = Event::TimeSample {
            utc: zx::Time::from_nanos(SAMPLE_1_UTC_NANOS),
            monotonic: zx::Time::from_nanos(SAMPLE_1_MONO_NANOS)
        };
        static ref SAMPLE_EVENT_2: Event = Event::TimeSample {
            utc: zx::Time::from_nanos(12345678),
            monotonic: zx::Time::from_nanos(333)
        };
    }

    #[fasync::run_until_stalled(test)]
    async fn fake_events_then_terminate() -> Result<(), Error> {
        let fake = FakeTimeSource::events_then_terminate(vec![
            *STATUS_EVENT_1,
            *SAMPLE_EVENT_1,
            *SAMPLE_EVENT_2,
        ]);
        let mut events = fake.launch().context("Fake should launch without error")?;
        assert_eq!(events.next().await.unwrap().unwrap(), *STATUS_EVENT_1);
        assert_eq!(events.next().await.unwrap().unwrap(), *SAMPLE_EVENT_1);
        assert_eq!(events.next().await.unwrap().unwrap(), *SAMPLE_EVENT_2);
        assert!(events.next().await.is_none());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn fake_events_then_pending() -> Result<(), Error> {
        let fake = FakeTimeSource::events_then_pending(vec![*STATUS_EVENT_1, *SAMPLE_EVENT_2]);
        let mut events = fake.launch().context("Fake should launch without error")?;
        assert_eq!(events.next().await.unwrap().unwrap(), *STATUS_EVENT_1);
        assert_eq!(events.next().await.unwrap().unwrap(), *SAMPLE_EVENT_2);
        // Making another call should lead to a stall and hence panic. We don't test this to
        // avoid a degenerate test, but do in fake_no_events_then_pending.
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    #[should_panic]
    async fn fake_no_events_then_pending() {
        let fake = FakeTimeSource::events_then_pending(vec![]);
        let mut events = fake.launch().unwrap();
        // Getting an event should never complete, leading to a stall.
        events.next().await;
    }

    #[test]
    fn fake_failing() {
        let fake = FakeTimeSource::failing();
        assert!(fake.launch().is_err());
    }

    #[test]
    fn new_push_time_source() {
        const COMPONENT_NAME: &str = "alfred";
        let time_source = PushTimeSource::new(COMPONENT_NAME.to_string());
        assert_eq!(time_source.component, COMPONENT_NAME);
    }

    #[fasync::run_singlethreaded(test)]
    async fn push_time_source_events() {
        let (proxy, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<ftexternal::PushSourceMarker>().unwrap();

        let _task = fasync::Task::spawn(async move {
            while let Some(Ok(request)) = requests.next().await {
                match request {
                    ftexternal::PushSourceRequest::WatchStatus { responder, .. } => {
                        responder.send(STATUS_1).unwrap();
                    }
                    ftexternal::PushSourceRequest::WatchSample { responder, .. } => {
                        let sample = ftexternal::TimeSample {
                            utc: Some(SAMPLE_1_UTC_NANOS),
                            monotonic: Some(SAMPLE_1_MONO_NANOS),
                        };
                        responder.send(sample).unwrap();
                    }
                    _ => {}
                };
            }
        });

        let mut events = PushTimeSource::events_from_proxy(None, proxy);
        // Note we rely on the ordering of stream::Select to return the first status before the
        // first sample.
        assert_eq!(events.next().await.unwrap().unwrap(), *STATUS_EVENT_1);
        assert_eq!(events.next().await.unwrap().unwrap(), *SAMPLE_EVENT_1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn push_time_source_failure() {
        let (proxy, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<ftexternal::PushSourceMarker>().unwrap();

        let _task = fasync::Task::spawn(async move {
            while let Some(Ok(request)) = requests.next().await {
                // Close the channel on the first watch status request.
                match request {
                    ftexternal::PushSourceRequest::WatchStatus { responder, .. } => {
                        responder.control_handle().shutdown_with_epitaph(zx::Status::BAD_STATE);
                    }
                    _ => {}
                };
            }
        });

        let mut events = PushTimeSource::events_from_proxy(None, proxy);
        assert!(events.next().await.unwrap().is_err());
    }
}
