// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl_fuchsia_test_workscheduler as fws,
    futures::{channel::*, lock::Mutex, sink::SinkExt, StreamExt},
    std::sync::Arc,
    test_utils_lib::injectors::ProtocolInjector,
};

#[derive(Debug, Eq, PartialEq)]
pub struct DispatchedEvent {
    work_id: String,
}

impl DispatchedEvent {
    pub fn new(work_id: String) -> Self {
        Self { work_id }
    }
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

    pub async fn wait_for_dispatched(&self) -> DispatchedEvent {
        let mut rx = self.dispatched_rx.lock().await;
        rx.next().await.unwrap()
    }
}

#[async_trait]
impl ProtocolInjector for WorkSchedulerDispatchReporter {
    type Marker = fws::WorkSchedulerDispatchReporterMarker;

    async fn serve(
        self: Arc<Self>,
        mut request_stream: fws::WorkSchedulerDispatchReporterRequestStream,
    ) -> Result<(), anyhow::Error> {
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

                    responder.send()?;
                    self.dispatched_tx.clone().send(DispatchedEvent::new(work_id)).await?;
                }
            }
        }
        Ok(())
    }
}
