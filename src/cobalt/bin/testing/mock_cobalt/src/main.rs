// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fidl_fuchsia_cobalt::{
        self as cobalt, CobaltEvent, LoggerFactoryRequest::CreateLoggerFromProjectName,
    },
    fidl_fuchsia_cobalt_test as cobalt_test, fuchsia_async as fasync,
    fuchsia_cobalt::CobaltEventExt,
    fuchsia_syslog::{self as syslog, fx_log_info},
    futures::{lock::Mutex, StreamExt, TryStreamExt},
    std::{collections::HashMap, sync::Arc},
};

/// MAX_QUERY_LENGTH is used as a usize in this component
const MAX_QUERY_LENGTH: usize = cobalt_test::MAX_QUERY_LENGTH as usize;

#[derive(Default)]
// Does not record StartTimer, EndTimer, and LogCustomEvent requests
struct EventsLog {
    log_event: Vec<CobaltEvent>,
    log_event_count: Vec<CobaltEvent>,
    log_elapsed_time: Vec<CobaltEvent>,
    log_frame_rate: Vec<CobaltEvent>,
    log_memory_usage: Vec<CobaltEvent>,
    log_string: Vec<CobaltEvent>,
    log_int_histogram: Vec<CobaltEvent>,
    log_cobalt_event: Vec<CobaltEvent>,
    log_cobalt_events: Vec<CobaltEvent>,
}

type EventsLogHandle = Arc<Mutex<EventsLog>>;

type LoggersHandle = Arc<Mutex<HashMap<String, EventsLogHandle>>>;

/// Create a new Logger. Accepts all `project_name` values.
async fn run_cobalt_service(
    mut stream: cobalt::LoggerFactoryRequestStream,
    loggers: LoggersHandle,
) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        if let CreateLoggerFromProjectName { project_name, logger, responder, release_stage: _ } =
            event
        {
            let log = loggers
                .lock()
                .await
                .entry(project_name)
                .or_insert_with(EventsLogHandle::default)
                .clone();
            fasync::spawn_local(handle_cobalt_logger(logger.into_stream()?, log));
            responder.send(cobalt::Status::Ok)?;
        } else {
            unimplemented!(
                "Logger factory request of type {:?} not supported by mock cobalt service",
                event
            );
        }
    }
    Ok(())
}

/// Accepts all incoming log requests and records them in an in-memory store
async fn handle_cobalt_logger(mut stream: cobalt::LoggerRequestStream, log: EventsLogHandle) {
    use cobalt::LoggerRequest::*;
    while let Some(Ok(event)) = stream.next().await {
        let mut log = log.lock().await;
        match event {
            LogEvent { metric_id, event_code, responder } => {
                log.log_event
                    .push(CobaltEvent::builder(metric_id).with_event_code(event_code).as_event());
                let _ = responder.send(cobalt::Status::Ok);
            }
            LogEventCount {
                metric_id,
                event_code,
                component,
                period_duration_micros,
                count,
                responder,
            } => {
                log.log_event_count.push(
                    CobaltEvent::builder(metric_id)
                        .with_event_code(event_code)
                        .with_component(component)
                        .as_count_event(period_duration_micros, count),
                );
                let _ = responder.send(cobalt::Status::Ok);
            }
            LogElapsedTime { metric_id, event_code, component, elapsed_micros, responder } => {
                log.log_elapsed_time.push(
                    CobaltEvent::builder(metric_id)
                        .with_event_code(event_code)
                        .with_component(component)
                        .as_elapsed_time(elapsed_micros),
                );
                let _ = responder.send(cobalt::Status::Ok);
            }
            LogFrameRate { metric_id, event_code, component, fps, responder } => {
                log.log_frame_rate.push(
                    CobaltEvent::builder(metric_id)
                        .with_event_code(event_code)
                        .with_component(component)
                        .as_frame_rate(fps),
                );
                let _ = responder.send(cobalt::Status::Ok);
            }
            LogMemoryUsage { metric_id, event_code, component, bytes, responder } => {
                log.log_memory_usage.push(
                    CobaltEvent::builder(metric_id)
                        .with_event_code(event_code)
                        .with_component(component)
                        .as_memory_usage(bytes),
                );
                let _ = responder.send(cobalt::Status::Ok);
            }
            LogString { metric_id, s, responder } => {
                log.log_string.push(CobaltEvent::builder(metric_id).as_string_event(s));
                let _ = responder.send(cobalt::Status::Ok);
            }
            LogIntHistogram { metric_id, event_code, component, histogram, responder } => {
                log.log_int_histogram.push(
                    CobaltEvent::builder(metric_id)
                        .with_event_code(event_code)
                        .with_component(component)
                        .as_int_histogram(histogram),
                );
                let _ = responder.send(cobalt::Status::Ok);
            }
            LogCobaltEvent { event, responder } => {
                log.log_cobalt_event.push(event);
                let _ = responder.send(cobalt::Status::Ok);
            }
            LogCobaltEvents { mut events, responder } => {
                log.log_cobalt_events.append(&mut events);
                let _ = responder.send(cobalt::Status::Ok);
            }
            e => unimplemented!("Event {:?} is not supported by the mock cobalt server", e),
        }
    }
}

/// Handles requests to query the state of the mock.
async fn run_cobalt_query_service(
    mut stream: cobalt_test::LoggerQuerierRequestStream,
    loggers: LoggersHandle,
) -> Result<(), Error> {
    use cobalt_test::LogMethod::*;

    while let Some(event) = stream.try_next().await? {
        match event {
            cobalt_test::LoggerQuerierRequest::QueryLogger { project_name, method, responder } => {
                if let Some(log) = loggers.lock().await.get(&project_name) {
                    let mut log = log.lock().await;
                    let events = match method {
                        LogEvent => &mut log.log_event,
                        LogEventCount => &mut log.log_event_count,
                        LogElapsedTime => &mut log.log_elapsed_time,
                        LogFrameRate => &mut log.log_frame_rate,
                        LogMemoryUsage => &mut log.log_memory_usage,
                        LogString => &mut log.log_string,
                        LogIntHistogram => &mut log.log_int_histogram,
                        LogCobaltEvent => &mut log.log_cobalt_event,
                        LogCobaltEvents => &mut log.log_cobalt_events,
                    };
                    let more = events.len() > cobalt_test::MAX_QUERY_LENGTH as usize;
                    let events = events.iter().take(MAX_QUERY_LENGTH).map(Clone::clone).collect();
                    responder.send(&mut Ok((events, more)))?;
                } else {
                    responder.send(&mut Err(cobalt_test::QueryError::LoggerNotFound))?;
                }
            }
            cobalt_test::LoggerQuerierRequest::ResetLogger {
                project_name,
                method,
                control_handle: _,
            } => {
                if let Some(log) = loggers.lock().await.get(&project_name) {
                    let mut log = log.lock().await;
                    match method {
                        LogEvent => log.log_event.clear(),
                        LogEventCount => log.log_event_count.clear(),
                        LogElapsedTime => log.log_elapsed_time.clear(),
                        LogFrameRate => log.log_frame_rate.clear(),
                        LogMemoryUsage => log.log_memory_usage.clear(),
                        LogString => log.log_string.clear(),
                        LogIntHistogram => log.log_int_histogram.clear(),
                        LogCobaltEvent => log.log_cobalt_event.clear(),
                        LogCobaltEvents => log.log_cobalt_events.clear(),
                    }
                }
            }
        }
    }
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["mock-cobalt"]).expect("Can't init logger");
    fx_log_info!("Starting mock cobalt service...");

    let loggers = LoggersHandle::default();
    let loggers_copy = loggers.clone();

    let mut fs = fuchsia_component::server::ServiceFs::new_local();
    fs.dir("svc")
        .add_fidl_service(move |stream| {
            let loggers = loggers.clone();
            fasync::spawn_local(async move {
                run_cobalt_service(stream, loggers).await.expect("failed to run cobalt service");
            })
        })
        .add_fidl_service(move |stream| {
            let loggers = loggers_copy.clone();
            fasync::spawn_local(async move {
                run_cobalt_query_service(stream, loggers)
                    .await
                    .expect("failed to run cobalt query service");
            })
        });
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::{create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_cobalt::*;
    use fidl_fuchsia_cobalt_test::{LogMethod, LoggerQuerierMarker, QueryError};
    use fuchsia_async as fasync;
    use futures::FutureExt;

    #[fasync::run_until_stalled(test)]
    async fn mock_logger_factory() {
        let loggers = LoggersHandle::default();

        let (factory_proxy, factory_stream) = create_proxy_and_stream::<LoggerFactoryMarker>()
            .expect("create logger factroy proxy and stream to succeed");
        let (_logger_proxy, server) =
            create_proxy::<LoggerMarker>().expect("create logger proxy and server end to succeed");

        fasync::spawn_local(run_cobalt_service(factory_stream, loggers.clone()).map(|_| ()));

        assert!(loggers.lock().await.is_empty());

        factory_proxy
            .create_logger_from_project_name("foo", ReleaseStage::Ga, server)
            .await
            .expect("create_logger_from_project_name fidl call to succeed");

        assert!(loggers.lock().await.get("foo").is_some());
    }

    #[fasync::run_until_stalled(test)]
    async fn mock_logger_and_query_interface_single_event() {
        let loggers = LoggersHandle::default();

        // Create channels
        let (factory_proxy, factory_stream) = create_proxy_and_stream::<LoggerFactoryMarker>()
            .expect("create logger factroy proxy and stream to succeed");
        let (logger_proxy, server) =
            create_proxy::<LoggerMarker>().expect("create logger proxy and server end to succeed");
        let (querier_proxy, query_stream) = create_proxy_and_stream::<LoggerQuerierMarker>()
            .expect("create logger querier proxy and stream to succeed");

        // Spawn service handlers. Any failures in the services spawned here will trigger panics
        // via expect method calls below.
        fasync::spawn_local(run_cobalt_service(factory_stream, loggers.clone()).map(|_| ()));
        fasync::spawn_local(run_cobalt_query_service(query_stream, loggers.clone()).map(|_| ()));

        factory_proxy
            .create_logger_from_project_name("foo", ReleaseStage::Ga, server)
            .await
            .expect("create_logger_from_project_name fidl call to succeed");

        // Log a single event
        logger_proxy.log_event(1, 2).await.expect("log_event fidl call to succeed");
        assert_eq!(
            Ok((vec![CobaltEvent::builder(1).with_event_code(2).as_event()], false)),
            querier_proxy
                .query_logger("foo", LogMethod::LogEvent)
                .await
                .expect("log_event fidl call to succeed")
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn mock_logger_and_query_interface_multiple_events() {
        let loggers = LoggersHandle::default();

        // Create channels
        let (factory_proxy, factory_stream) = create_proxy_and_stream::<LoggerFactoryMarker>()
            .expect("create logger factroy proxy and stream to succeed");
        let (logger_proxy, server) =
            create_proxy::<LoggerMarker>().expect("create logger proxy and server end to succeed");
        let (querier_proxy, query_stream) = create_proxy_and_stream::<LoggerQuerierMarker>()
            .expect("create logger querier proxy and stream to succeed");

        // Spawn service handlers. Any failures in the services spawned here will trigger panics
        // via expect method calls below.
        fasync::spawn_local(run_cobalt_service(factory_stream, loggers.clone()).map(|_| ()));
        fasync::spawn_local(run_cobalt_query_service(query_stream, loggers.clone()).map(|_| ()));

        factory_proxy
            .create_logger_from_project_name("foo", ReleaseStage::Ga, server)
            .await
            .expect("create_logger_from_project_name fidl call to succeed");

        // Log 1 more than the maximum number of events that can be stored and assert that
        // `more` flag is true on logger query request
        for i in 0..(MAX_QUERY_LENGTH as u32 + 1) {
            logger_proxy
                .log_event(i, i + 1)
                .await
                .expect("repeated log_event fidl call to succeed");
        }
        let (events, more) = querier_proxy
            .query_logger("foo", LogMethod::LogEvent)
            .await
            .expect("query_logger fidl call to succeed")
            .expect("logger to exist and have recorded events");
        assert_eq!(CobaltEvent::builder(0).with_event_code(1).as_event(), events[0]);
        assert_eq!(MAX_QUERY_LENGTH, events.len());
        assert!(more);
    }

    #[fasync::run_until_stalled(test)]
    async fn mock_query_interface_no_logger_error() {
        let loggers = LoggersHandle::default();

        // Create channel
        let (querier_proxy, query_stream) = create_proxy_and_stream::<LoggerQuerierMarker>()
            .expect("create logger querier proxy and stream to succeed");

        // Spawn service handler. Any failures in the service spawned here will trigger panics
        // via expect method calls below.
        fasync::spawn_local(run_cobalt_query_service(query_stream, loggers.clone()).map(|_| ()));

        // Assert on initial state
        assert_eq!(
            Err(QueryError::LoggerNotFound),
            querier_proxy
                .query_logger("foo", LogMethod::LogEvent)
                .await
                .expect("query_logger fidl call to succeed")
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn mock_logger_logger_type_tracking() -> Result<(), fidl::Error> {
        let loggers = LoggersHandle::default();

        let (factory_proxy, factory_stream) = create_proxy_and_stream::<LoggerFactoryMarker>()
            .expect("create logger factroy proxy and stream to succeed");
        let (logger_proxy, server) =
            create_proxy::<LoggerMarker>().expect("create logger proxy and server end to succeed");

        fasync::spawn_local(run_cobalt_service(factory_stream, loggers.clone()).map(|_| ()));
        let project_name = "foo";

        factory_proxy
            .create_logger_from_project_name(project_name, ReleaseStage::Ga, server)
            .await
            .expect("create_logger_from_project_name fidl call to succeed");

        let metric_id = 1;
        let event_code = 2;
        let component_name = "component";
        let period_duration_micros = 0;
        let count = 3;
        let frame_rate: f32 = 59.9;

        logger_proxy.log_event(metric_id, event_code).await?;
        logger_proxy
            .log_event_count(metric_id, event_code, component_name, period_duration_micros, count)
            .await?;
        logger_proxy
            .log_elapsed_time(metric_id, event_code, component_name, period_duration_micros)
            .await?;
        logger_proxy.log_memory_usage(metric_id, event_code, component_name, count).await?;
        logger_proxy.log_frame_rate(metric_id, event_code, component_name, frame_rate).await?;
        logger_proxy.log_string(metric_id, component_name).await?;
        logger_proxy
            .log_int_histogram(metric_id, event_code, component_name, &mut vec![].into_iter())
            .await?;
        logger_proxy
            .log_cobalt_event(&mut cobalt::CobaltEvent {
                metric_id,
                event_codes: vec![event_code],
                component: Some(component_name.to_string()),
                payload: cobalt::EventPayload::Event(cobalt::Event {}),
            })
            .await?;
        logger_proxy.log_cobalt_events(&mut vec![].into_iter()).await?;
        let log = loggers.lock().await;
        let log = log.get(project_name).expect("project should have been created");
        let log = log.lock().await;
        assert_eq!(log.log_event.len(), 1);
        assert_eq!(log.log_event_count.len(), 1);
        assert_eq!(log.log_elapsed_time.len(), 1);
        assert_eq!(log.log_memory_usage.len(), 1);
        assert_eq!(log.log_frame_rate.len(), 1);
        assert_eq!(log.log_string.len(), 1);
        assert_eq!(log.log_int_histogram.len(), 1);
        assert_eq!(log.log_cobalt_event.len(), 1);
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn mock_query_interface_reset_state() {
        let loggers = LoggersHandle::default();

        // Create channels
        let (factory_proxy, factory_stream) = create_proxy_and_stream::<LoggerFactoryMarker>()
            .expect("create logger factroy proxy and stream to succeed");
        let (logger_proxy, server) =
            create_proxy::<LoggerMarker>().expect("create logger proxy and server end to succeed");
        let (querier_proxy, query_stream) = create_proxy_and_stream::<LoggerQuerierMarker>()
            .expect("create logger querier proxy and stream to succeed");

        // Spawn service handlers. Any failures in the services spawned here will trigger panics
        // via expect method calls below.
        fasync::spawn_local(run_cobalt_service(factory_stream, loggers.clone()).map(|_| ()));
        fasync::spawn_local(run_cobalt_query_service(query_stream, loggers.clone()).map(|_| ()));

        factory_proxy
            .create_logger_from_project_name("foo", ReleaseStage::Ga, server)
            .await
            .expect("create_logger_from_project_name fidl call to succeed");

        // Log a single event
        logger_proxy.log_event(1, 2).await.expect("log_event fidl call to succeed");
        assert_eq!(
            Ok((vec![CobaltEvent::builder(1).with_event_code(2).as_event()], false)),
            querier_proxy
                .query_logger("foo", LogMethod::LogEvent)
                .await
                .expect("log_event fidl call to succeed")
        );

        // Clear logger state
        querier_proxy
            .reset_logger("foo", LogMethod::LogEvent)
            .expect("reset_logger fidl call to succeed");

        assert_eq!(
            Ok((vec![], false)),
            querier_proxy
                .query_logger("foo", LogMethod::LogEvent)
                .await
                .expect("query_logger fidl call to succeed")
        );
    }
}
