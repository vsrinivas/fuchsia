// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    breakpoint_system_client::Injector,
    fidl_fuchsia_test_workscheduler as fws,
    fuchsia_async::{Time, Timer},
    futures::{
        channel::*,
        future::{select_all, BoxFuture},
        lock::Mutex,
        sink::SinkExt,
        StreamExt,
    },
    std::{
        convert::TryInto,
        error::Error,
        fmt::{self as fmt, Display, Formatter},
        sync::Arc,
        time::Duration,
    },
};

#[derive(Debug)]
pub struct Timeout(Duration);

#[derive(Debug, Eq, PartialEq)]
pub struct DispatchedEvent {
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
    pub fn new(work_id: String) -> Self {
        Self { work_id }
    }
}

enum DispatchTimeout {
    Dispatched(DispatchedEvent),
    Timeout(Timeout),
}

pub struct WorkSchedulerDispatchReporter {
    dispatched_tx: mpsc::Sender<DispatchedEvent>,
    dispatched_rx: Mutex<mpsc::Receiver<DispatchedEvent>>,
}

impl WorkSchedulerDispatchReporter {
    pub fn new() -> Arc<Self> {
        let (tx, rx) = mpsc::channel(0);
        Arc::new(Self { dispatched_tx: tx, dispatched_rx: Mutex::new(rx) })
    }

    pub async fn wait_for_dispatched(&self, timeout: Duration) -> Result<DispatchedEvent, Timeout> {
        let timer = Box::pin(Self::wait_for(timeout)) as BoxFuture<'_, DispatchTimeout>;
        let dispatched_event = Box::pin(self.get_dispatched()) as BoxFuture<'_, DispatchTimeout>;
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
}

#[async_trait]
impl Injector for WorkSchedulerDispatchReporter {
    type Marker = fws::WorkSchedulerDispatchReporterMarker;

    async fn serve(
        self: Arc<Self>,
        mut request_stream: fws::WorkSchedulerDispatchReporterRequestStream,
    ) {
        while let Some(Ok(request)) = request_stream.next().await {
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
                    self.dispatched_tx.clone().send(DispatchedEvent::new(work_id)).await.unwrap();
                }
            }
        }
    }
}
