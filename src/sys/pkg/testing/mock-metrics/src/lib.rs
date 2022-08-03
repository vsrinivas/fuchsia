// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_metrics::{self as fidl, MetricEvent},
    fuchsia_async as fasync,
    futures::TryStreamExt as _,
    parking_lot::Mutex,
    std::{sync::Arc, time::Duration},
};

struct MockMetricEventLogger {
    cobalt_events: Mutex<Vec<MetricEvent>>,
}

impl MockMetricEventLogger {
    fn new() -> Self {
        Self { cobalt_events: Mutex::new(vec![]) }
    }

    async fn run_logger(self: Arc<Self>, mut stream: fidl::MetricEventLoggerRequestStream) {
        while let Some(event) = stream.try_next().await.unwrap() {
            match event {
                fidl::MetricEventLoggerRequest::LogMetricEvents { events, responder } => {
                    self.cobalt_events.lock().extend(events);
                    let _ = responder.send(&mut Ok(()));
                }
                _ => {
                    panic!("unhandled MetricEventLogger method {:?}", event);
                }
            }
        }
    }
}

pub struct MockMetricEventLoggerFactory {
    loggers: Mutex<Vec<Arc<MockMetricEventLogger>>>,
}

impl MockMetricEventLoggerFactory {
    pub fn new() -> Self {
        Self { loggers: Mutex::new(vec![]) }
    }

    pub async fn run_logger_factory(
        self: Arc<Self>,
        mut stream: fidl::MetricEventLoggerFactoryRequestStream,
    ) {
        while let Some(event) = stream.try_next().await.unwrap() {
            match event {
                fidl::MetricEventLoggerFactoryRequest::CreateMetricEventLogger {
                    project_spec,
                    logger,
                    responder,
                } => {
                    assert_eq!(
                        project_spec.project_id,
                        Some(cobalt_sw_delivery_registry::PROJECT_ID)
                    );
                    let mock_logger = Arc::new(MockMetricEventLogger::new());
                    self.loggers.lock().push(mock_logger.clone());
                    fasync::Task::spawn(mock_logger.run_logger(logger.into_stream().unwrap()))
                        .detach();
                    let _ = responder.send(&mut Ok(()));
                }
                _ => {
                    panic!("unhandled MetricEventLoggerFactory method: {:?}", event);
                }
            }
        }
    }

    pub async fn wait_for_at_least_n_events_with_metric_id(
        &self,
        n: usize,
        id: u32,
    ) -> Vec<MetricEvent> {
        loop {
            let events: Vec<MetricEvent> = self
                .loggers
                .lock()
                .iter()
                .flat_map(|logger| logger.cobalt_events.lock().clone())
                .filter(|MetricEvent { metric_id, .. }| *metric_id == id)
                .collect();
            if events.len() >= n {
                return events;
            }
            fasync::Timer::new(Duration::from_millis(10)).await;
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        ::fidl::{encoding::Decodable as _, endpoints::create_proxy_and_stream},
        fidl_fuchsia_metrics::ProjectSpec,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_mock_metrics() {
        let mock = Arc::new(MockMetricEventLoggerFactory::new());
        let (factory, stream) =
            create_proxy_and_stream::<fidl::MetricEventLoggerFactoryMarker>().unwrap();

        let task = fasync::Task::spawn(Arc::clone(&mock).run_logger_factory(stream));

        let (logger, server_end) = ::fidl::endpoints::create_proxy().unwrap();
        factory
            .create_metric_event_logger(
                ProjectSpec {
                    project_id: Some(cobalt_sw_delivery_registry::PROJECT_ID),
                    ..ProjectSpec::EMPTY
                },
                server_end,
            )
            .await
            .unwrap()
            .unwrap();
        drop(factory);
        task.await;

        let mut event = MetricEvent { metric_id: 42, ..MetricEvent::new_empty() };
        logger.log_metric_events(&mut std::iter::once(&mut event)).await.unwrap().unwrap();
        let events = mock.wait_for_at_least_n_events_with_metric_id(1, 42).await;
        assert_eq!(events, vec![event]);
    }
}
