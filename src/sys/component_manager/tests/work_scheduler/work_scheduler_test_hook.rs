// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO Follow 2018 idioms
#![allow(elided_lifetimes_in_paths)]

use {
    component_manager_lib::{
        capability::{CapabilityProvider, CapabilitySource, FrameworkCapability},
        model::{
            error::ModelError,
            hooks::{Event, EventPayload, Hook},
            moniker::AbsoluteMoniker,
            realm::Realm,
        },
    },
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_test_workscheduler as fws,
    fuchsia_async::{self as fasync, Time, Timer},
    fuchsia_zircon as zx,
    futures::{
        channel::*,
        future::{select_all, BoxFuture},
        lock::Mutex,
        sink::SinkExt,
        StreamExt,
    },
    lazy_static::lazy_static,
    std::{
        convert::TryInto,
        error::Error,
        fmt::{self as fmt, Display, Formatter},
        sync::Arc,
        time::Duration,
    },
};

lazy_static! {
    pub static ref REPORT_SERVICE: cm_rust::CapabilityPath =
        "/svc/fuchsia.test.workscheduler.WorkSchedulerDispatchReporter".try_into().unwrap();
}

#[derive(Debug)]
pub struct Timeout(Duration);

#[derive(Debug, Eq, PartialEq)]
pub struct DispatchedEvent {
    abs_moniker: AbsoluteMoniker,
    work_id: String,
}

impl Display for Timeout {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "Operation timed out (timeout={})", self.0.as_nanos())
    }
}

impl Error for Timeout {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        None
    }
}

impl DispatchedEvent {
    pub fn new(abs_moniker: AbsoluteMoniker, work_id: String) -> Self {
        Self { abs_moniker, work_id }
    }
}

enum DispatchTimeout {
    Dispatched(DispatchedEvent),
    Timeout(Timeout),
}

pub struct WorkSchedulerTestHook {
    dispatched_tx: Mutex<mpsc::Sender<DispatchedEvent>>,
    dispatched_rx: Mutex<mpsc::Receiver<DispatchedEvent>>,
}

impl WorkSchedulerTestHook {
    pub fn new() -> Self {
        let (tx, rx) = mpsc::channel(0);
        Self { dispatched_tx: Mutex::new(tx), dispatched_rx: Mutex::new(rx) }
    }

    pub async fn wait_for_dispatched(&self, timeout: Duration) -> Result<DispatchedEvent, Timeout> {
        let timer = Box::pin(Self::wait_for(timeout)) as BoxFuture<DispatchTimeout>;
        let dispatched_event = Box::pin(self.get_dispatched()) as BoxFuture<DispatchTimeout>;
        let (result, _, _) = select_all(vec![timer, dispatched_event]).await;
        match result {
            DispatchTimeout::Dispatched(dispatched_event) => Ok(dispatched_event),
            DispatchTimeout::Timeout(timeout) => Err(timeout),
        }
    }

    async fn wait_for(timeout: Duration) -> DispatchTimeout {
        let now = Time::now().into_nanos();
        let delta: i64 = timeout.as_nanos().try_into().unwrap();
        let timer = Timer::new(Time::from_nanos(now + delta));
        let _ = timer.await;
        DispatchTimeout::Timeout(Timeout(timeout))
    }

    async fn get_dispatched(&self) -> DispatchTimeout {
        let mut rx = self.dispatched_rx.lock().await;
        DispatchTimeout::Dispatched(rx.next().await.unwrap())
    }

    async fn on_do_work_called(&self, dispatched_event: DispatchedEvent) {
        let mut tx = self.dispatched_tx.lock().await;
        tx.send(dispatched_event).await.unwrap()
    }

    async fn on_route_scoped_framework_capability_async(
        self: Arc<Self>,
        realm: Arc<Realm>,
        capability: &FrameworkCapability,
        capability_provider: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        match (&capability_provider, capability) {
            (None, FrameworkCapability::ServiceProtocol(capability_path))
                if *capability_path == *REPORT_SERVICE =>
            {
                Ok(Some(Box::new(WorkSchedulerTestCapabilityProvider::new(
                    realm.abs_moniker.clone(),
                    self.clone(),
                )) as Box<dyn CapabilityProvider>))
            }
            _ => Ok(capability_provider),
        }
    }
}

impl Hook for WorkSchedulerTestHook {
    fn on(self: Arc<Self>, event: &Event) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async move {
            if let EventPayload::RouteCapability {
                source: CapabilitySource::Framework { capability, scope_realm: Some(scope_realm) },
                capability_provider,
            } = &event.payload
            {
                let mut capability_provider = capability_provider.lock().await;
                *capability_provider = self
                    .on_route_scoped_framework_capability_async(
                        scope_realm.clone(),
                        &capability,
                        capability_provider.take(),
                    )
                    .await?;
            }
            Ok(())
        })
    }
}

struct WorkSchedulerTestCapabilityProvider {
    abs_moniker: AbsoluteMoniker,
    hook: Arc<WorkSchedulerTestHook>,
}

impl WorkSchedulerTestCapabilityProvider {
    pub fn new(abs_moniker: AbsoluteMoniker, hook: Arc<WorkSchedulerTestHook>) -> Self {
        Self { abs_moniker, hook }
    }

    pub async fn open_async(&self, server_end: zx::Channel) -> Result<(), ModelError> {
        let mut stream = ServerEnd::<fws::WorkSchedulerDispatchReporterMarker>::new(server_end)
            .into_stream()
            .expect("could not convert channel into stream");
        let abs_moniker = self.abs_moniker.clone();
        let hook = self.hook.clone();
        fasync::spawn(async move {
            while let Some(Ok(request)) = stream.next().await {
                match request {
                    fws::WorkSchedulerDispatchReporterRequest::OnDoWorkCalled {
                        work_id,
                        responder,
                    } => {
                        // Complete the exchange with the client before notifying the integration
                        // test of the report. This prevents the client from behing hung up on by
                        // the integration test if asserting that the report has arrived is the last
                        // step before the integration test completes.

                        // TODO(markdittmer): Do something with on_do_work_called errors.
                        responder.send().unwrap();

                        let _ = hook
                            .on_do_work_called(DispatchedEvent::new(abs_moniker.clone(), work_id))
                            .await;
                    }
                }
            }
        });
        Ok(())
    }
}

impl CapabilityProvider for WorkSchedulerTestCapabilityProvider {
    fn open(
        &self,
        _flags: u32,
        _open_mode: u32,
        _relative_path: String,
        server_chan: zx::Channel,
    ) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(self.open_async(server_chan))
    }
}
