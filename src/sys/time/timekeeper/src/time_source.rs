// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Error},
    async_trait::async_trait,
    fidl_fuchsia_component::{CreateChildArgs, RealmMarker},
    fidl_fuchsia_component_decl::{Child, ChildRef, CollectionRef, StartupMode},
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_time_external::{self as ftexternal, PushSourceProxy, Status, Urgency},
    fuchsia_component::client,
    fuchsia_zircon as zx,
    futures::{stream::Stream, FutureExt, TryFutureExt},
    std::{fmt::Debug, sync::Arc},
    tracing::info,
};

const TIMESOURCE_COLLECTION_NAME: &str = "timesource";
const PRIMARY_TIMESOURCE_NAME: &str = "primary";

/// A time sample received from a source of time.
#[derive(Debug, PartialEq, Clone, Copy)]
pub struct Sample {
    /// The UTC time.
    pub utc: zx::Time,
    /// The monotonic time at which the UTC was most valid.
    pub monotonic: zx::Time,
    /// The standard deviation of the UTC error.
    pub std_dev: zx::Duration,
}

#[cfg(test)]
impl Sample {
    /// Constructs a new `Sample`.
    pub fn new(utc: zx::Time, monotonic: zx::Time, std_dev: zx::Duration) -> Sample {
        Sample { utc, monotonic, std_dev }
    }
}

/// An event that may be observed from a source of time.
#[derive(Debug, PartialEq, Clone, Copy)]
pub enum Event {
    /// The status of the time source changed.
    StatusChange {
        /// The current status of the time source.
        status: Status,
    },
    /// The time source produced a new time sample.
    Sample(Sample),
}

impl From<Sample> for Event {
    fn from(sample: Sample) -> Event {
        Event::Sample(sample)
    }
}

/// One of the timesource API implementations.
#[derive(Debug)]
#[allow(dead_code)]
pub enum TimeSource {
    Push(BoxedPushSource),
    Pull(BoxedPullSource),
}

pub type BoxedPushSourceEventStream =
    Box<dyn Stream<Item = Result<Event, Error>> + Unpin + Send + Sync>;

pub type BoxedPushSource = Box<dyn PushSource>;
pub type BoxedPullSource = Box<dyn PullSource>;

/// Provides abstraction over `fuchsia.time.external.PushSource`.
/// https://fuchsia.dev/fuchsia-src/concepts/kernel/time/utc/architecture?hl=en#timekeeper
#[async_trait]
pub trait PushSource: Send + Sync + Debug {
    /// Attempts to launch the time source and return a stream of its time output and status
    /// change events.
    async fn watch(&self) -> Result<BoxedPushSourceEventStream, Error>;
}

/// Provides abstraction over `fuchsia.time.external.PullSource`.
/// https://fuchsia.dev/fuchsia-src/concepts/kernel/time/utc/architecture?hl=en#timekeeper
#[async_trait]
pub trait PullSource: Send + Sync + Debug {
    /// Attempts to start the timesource component and request a time sample. Component is
    /// unloaded after sample is returned in order to free system resources.
    async fn sample(&self, _urgency: &Urgency) -> Result<Sample, Error>;
}

/// Starts the component that provides one of timesouce FIDL APIs.
#[derive(Debug)]
pub struct TimeSourceLauncher {
    component_url: String,
}

impl TimeSourceLauncher {
    /// Creates new launcher.
    /// TODO(fxb/111889): Pass in the Role enum and use it to derive the time source name.
    pub fn new(component_url: String) -> Self {
        TimeSourceLauncher { component_url }
    }

    async fn launch(&self) -> Result<DirectoryProxy, Error> {
        info!("Launching TimeSource at {}", self.component_url);
        let realm = client::connect_to_protocol::<RealmMarker>()
            .context("failed to connect to fuchsia.component.Realm")?;

        let mut collection_ref = CollectionRef { name: String::from(TIMESOURCE_COLLECTION_NAME) };

        // Destroy the previously launched timesource.
        let mut child_ref = ChildRef {
            name: String::from(PRIMARY_TIMESOURCE_NAME),
            collection: Some(String::from(TIMESOURCE_COLLECTION_NAME)),
        };

        let _ = realm.destroy_child(&mut child_ref).await;

        let child_decl = Child {
            name: Some(String::from(PRIMARY_TIMESOURCE_NAME)),
            url: Some(self.component_url.clone()),
            startup: Some(StartupMode::Lazy),
            ..Child::EMPTY
        };

        realm
            .create_child(&mut collection_ref, child_decl, CreateChildArgs::EMPTY)
            .await
            .context("realm.create_child failed")?
            .map_err(|e| anyhow!("failed to create child: {:?}", e))?;

        Ok(client::open_childs_exposed_directory(
            String::from(PRIMARY_TIMESOURCE_NAME),
            Some(String::from(TIMESOURCE_COLLECTION_NAME)),
        )
        .await
        .context("failed to open exposed directory")?)
    }
}

impl Into<BoxedPushSource> for TimeSourceLauncher {
    fn into(self) -> BoxedPushSource {
        Box::new(PushSourceImpl { launcher: self })
    }
}
impl Into<BoxedPullSource> for TimeSourceLauncher {
    fn into(self) -> BoxedPullSource {
        Box::new(PullSourceImpl { launcher: self })
    }
}

/// Production implementation of the `PushSource` trait.
#[derive(Debug)]
pub struct PushSourceImpl {
    launcher: TimeSourceLauncher,
}

impl PushSourceImpl {
    /// Returns a stream of time output and status change events received using the supplied
    /// `PushSourceProxy`, retaining the optional `App` for the same lifetime.
    fn events_from_proxy(proxy: PushSourceProxy) -> BoxedPushSourceEventStream {
        let proxy = Arc::new(proxy);

        let status_stream = futures::stream::try_unfold(Arc::clone(&proxy), |proxy| {
            proxy
                .watch_status()
                .map_ok(move |status| Some((Event::StatusChange { status }, proxy)))
                .err_into()
        });

        let sample_stream = futures::stream::try_unfold(proxy, |proxy| {
            proxy.watch_sample().map(move |result| match result {
                Ok(sample) => match (sample.utc, sample.monotonic, sample.standard_deviation) {
                    (None, _, _) => Err(anyhow!("sample missing utc")),
                    (_, None, _) => Err(anyhow!("sample missing monotonic")),
                    (_, _, None) => Err(anyhow!("sample missing standard deviation")),
                    (Some(utc), Some(monotonic), Some(std_dev)) => Ok(Some((
                        Event::Sample(Sample {
                            utc: zx::Time::from_nanos(utc),
                            monotonic: zx::Time::from_nanos(monotonic),
                            std_dev: zx::Duration::from_nanos(std_dev),
                        }),
                        proxy,
                    ))),
                },
                Err(err) => Err(err.into()),
            })
        });

        Box::new(futures::stream::select(Box::pin(status_stream), Box::pin(sample_stream)))
    }
}

#[async_trait]
impl PushSource for PushSourceImpl {
    /// Attempts to connect to PushSource FIDL API to receive time samples and status updates.
    async fn watch(&self) -> Result<BoxedPushSourceEventStream, Error> {
        let directory = self.launcher.launch().await?;
        let proxy =
            client::connect_to_protocol_at_dir_root::<ftexternal::PushSourceMarker>(&directory)
                .context("failed to connect to the fuchsia.time.external.PushSource")?;

        Ok(PushSourceImpl::events_from_proxy(proxy))
    }
}

/// Production implementation of the `PushSource` trait.
#[allow(dead_code)]
#[derive(Debug)]
pub struct PullSourceImpl {
    launcher: TimeSourceLauncher,
}

#[allow(dead_code)]
impl PullSourceImpl {}

#[async_trait]
impl PullSource for PullSourceImpl {
    /// Attempts to start the timesource component and request a time sample. Component is
    /// unloaded after sample is returned in order to free system resources.
    async fn sample(&self, _urgency: &Urgency) -> Result<Sample, Error> {
        unimplemented!();
    }
}

#[cfg(test)]
use {
    futures::{stream, StreamExt},
    parking_lot::Mutex,
};

/// A time source that immediately produces a collections of events supplied at construction.
/// The time source may be launched multiple times and will return a different collection of events
/// on each launch. It will return pending after the last event in the last collection, and will
/// terminate the stream after the last event in all other collections. The time source will return
/// an error if asked to launch after the last collection of events has been returned.
#[cfg(test)]
pub struct FakePushTimeSource {
    /// The collections of events to return. The TimeSource will return pending after the last
    /// event in the last collection, and will terminate the stream after the last event in all
    /// other collections.
    collections: Mutex<Vec<Vec<Result<Event, Error>>>>,
}

#[cfg(test)]
impl From<FakePushTimeSource> for TimeSource {
    fn from(s: FakePushTimeSource) -> Self {
        TimeSource::Push(Box::new(s))
    }
}

#[cfg(test)]
impl FakePushTimeSource {
    /// Creates a new `FakeTimeSource` that produces the supplied single collection of successful
    /// events.
    pub fn events(events: Vec<Event>) -> Self {
        FakePushTimeSource {
            collections: Mutex::new(vec![events.into_iter().map(|evt| Ok(evt)).collect()]),
        }
    }

    /// Creates a new `FakeTimeSource` that produces the supplied collections of successful events.
    pub fn event_collections(event_collections: Vec<Vec<Event>>) -> Self {
        FakePushTimeSource {
            collections: Mutex::new(
                event_collections
                    .into_iter()
                    .map(|collection| collection.into_iter().map(|evt| Ok(evt)).collect())
                    .collect(),
            ),
        }
    }

    /// Creates a new `FakeTimeSource` that produces the supplied collections of results.
    pub fn result_collections(result_collections: Vec<Vec<Result<Event, Error>>>) -> Self {
        FakePushTimeSource { collections: Mutex::new(result_collections) }
    }

    /// Creates a new `FakeTimeSource` that always fails to launch.
    pub fn failing() -> Self {
        FakePushTimeSource { collections: Mutex::new(vec![]) }
    }
}

#[cfg(test)]
impl Debug for FakePushTimeSource {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str("FakeTimeSource")
    }
}

#[cfg(test)]
#[async_trait]
impl PushSource for FakePushTimeSource {
    async fn watch(&self) -> Result<BoxedPushSourceEventStream, Error> {
        let mut lock = self.collections.lock();
        if lock.is_empty() {
            return Err(anyhow!("FakeTimeSource sent all supplied event collections"));
        }
        let events = lock.remove(0);
        // Return a pending after the last event if this was the last collection.
        if lock.is_empty() {
            Ok(Box::new(Box::pin(stream::iter(events).chain(stream::pending()))))
        } else {
            Ok(Box::new(Box::pin(stream::iter(events))))
        }
    }
}

#[cfg(test)]
mod test {
    use {
        super::*, fidl::prelude::*, fuchsia_async as fasync, futures::stream::StreamExt,
        lazy_static::lazy_static,
    };

    const STATUS_1: Status = Status::Initializing;
    const SAMPLE_1_UTC_NANOS: i64 = 1234567;
    const SAMPLE_1_MONO_NANOS: i64 = 222;
    const SAMPLE_1_STD_DEV_NANOS: i64 = 8888;

    lazy_static! {
        static ref STATUS_EVENT_1: Event = Event::StatusChange { status: STATUS_1 };
        static ref SAMPLE_EVENT_1: Event = Event::from(Sample {
            utc: zx::Time::from_nanos(SAMPLE_1_UTC_NANOS),
            monotonic: zx::Time::from_nanos(SAMPLE_1_MONO_NANOS),
            std_dev: zx::Duration::from_nanos(SAMPLE_1_STD_DEV_NANOS),
        });
        static ref SAMPLE_EVENT_2: Event = Event::from(Sample {
            utc: zx::Time::from_nanos(12345678),
            monotonic: zx::Time::from_nanos(333),
            std_dev: zx::Duration::from_nanos(9999),
        });
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn single_event_set() -> Result<(), Error> {
        let fake =
            FakePushTimeSource::events(vec![*STATUS_EVENT_1, *SAMPLE_EVENT_1, *SAMPLE_EVENT_2]);
        let mut events = fake.watch().await.context("Fake should watch without error")?;
        assert_eq!(events.next().await.unwrap().unwrap(), *STATUS_EVENT_1);
        assert_eq!(events.next().await.unwrap().unwrap(), *SAMPLE_EVENT_1);
        assert_eq!(events.next().await.unwrap().unwrap(), *SAMPLE_EVENT_2);
        // Making another call should lead to a stall and hence panic. We don't test this to
        // avoid a degenerate test, but do in fake_no_events_then_pending.
        assert!(fake.watch().await.is_err());
        Ok(())
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn double_event_set() -> Result<(), Error> {
        let fake = FakePushTimeSource::event_collections(vec![
            vec![*STATUS_EVENT_1, *SAMPLE_EVENT_1],
            vec![*SAMPLE_EVENT_2],
        ]);
        let mut events = fake.watch().await.context("Fake should watch without error")?;
        assert_eq!(events.next().await.unwrap().unwrap(), *STATUS_EVENT_1);
        assert_eq!(events.next().await.unwrap().unwrap(), *SAMPLE_EVENT_1);
        assert!(events.next().await.is_none());
        let mut events = fake.watch().await.context("Fake should watch without error")?;
        assert_eq!(events.next().await.unwrap().unwrap(), *SAMPLE_EVENT_2);
        // Making another call should lead to a stall and hence panic. We don't test this to
        // avoid a degenerate test, but do in fake_no_events_then_pending.
        assert!(fake.watch().await.is_err());
        Ok(())
    }

    #[fuchsia::test(allow_stalls = false)]
    #[should_panic]
    async fn fake_no_events_then_pending() {
        let fake = FakePushTimeSource::events(vec![]);
        let mut events = fake.watch().await.unwrap();
        // Getting an event from the last collection should never complete, leading to a stall.
        events.next().await;
    }

    #[fuchsia::test]
    async fn fake_failing() -> Result<(), Error> {
        let fake = FakePushTimeSource::failing();
        let events = fake.watch().await.context("Fake should launch without error");
        assert!(events.is_err());
        Ok(())
    }

    #[fuchsia::test]
    fn new_push_time_source() {
        const COMPONENT_NAME: &str = "alfred";
        let time_source = TimeSourceLauncher::new(COMPONENT_NAME.to_string());
        assert_eq!(time_source.component_url, COMPONENT_NAME);
    }

    #[fuchsia::test]
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
                            standard_deviation: Some(SAMPLE_1_STD_DEV_NANOS),
                            ..ftexternal::TimeSample::EMPTY
                        };
                        responder.send(sample).unwrap();
                    }
                    _ => {}
                };
            }
        });

        let mut events = PushSourceImpl::events_from_proxy(proxy);
        // We expect to receive both events but the ordering is not deterministic.
        let event1 = events.next().await.unwrap().unwrap();
        let event2 = events.next().await.unwrap().unwrap();
        match event1 {
            Event::StatusChange { status: _ } => {
                assert_eq!(event1, *STATUS_EVENT_1);
                assert_eq!(event2, *SAMPLE_EVENT_1);
            }
            Event::Sample(_) => {
                assert_eq!(event1, *SAMPLE_EVENT_1);
                assert_eq!(event2, *STATUS_EVENT_1);
            }
        }
    }

    #[fuchsia::test]
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

        let mut events = PushSourceImpl::events_from_proxy(proxy);
        assert!(events.next().await.unwrap().is_err());
    }
}
