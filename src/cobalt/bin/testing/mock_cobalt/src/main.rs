// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fidl_fuchsia_cobalt::{
        self as cobalt, CobaltEvent, LoggerFactoryRequest::CreateLoggerFromProjectId,
    },
    fidl_fuchsia_cobalt_test as cobalt_test, fuchsia_async as fasync,
    fuchsia_cobalt::CobaltEventExt,
    fuchsia_syslog::{self as syslog, fx_log_info},
    futures::{lock::Mutex, StreamExt, TryStreamExt},
    std::{collections::HashMap, sync::Arc},
};

/// MAX_QUERY_LENGTH is used as a usize in this component
const MAX_QUERY_LENGTH: usize = cobalt_test::MAX_QUERY_LENGTH as usize;

struct LogState {
    log: Vec<CobaltEvent>,
    hanging: Vec<HangingGetState>,
}

#[derive(Default)]
// Does not record StartTimer, EndTimer, and LogCustomEvent requests
struct EventsLog {
    log_event: LogState,
    log_event_count: LogState,
    log_elapsed_time: LogState,
    log_frame_rate: LogState,
    log_memory_usage: LogState,
    log_string: LogState,
    log_int_histogram: LogState,
    log_cobalt_event: LogState,
    log_cobalt_events: LogState,
}

impl Default for LogState {
    fn default() -> LogState {
        LogState { log: vec![], hanging: vec![] }
    }
}

struct HangingGetState {
    // last_observed is concurrently mutated by calls to run_cobalt_query_service (one for each client
    // of fuchsia.cobalt.test.LoggerQuerier) and calls to handle_cobalt_logger (one for each client
    // of fuchsia.cobalt.Logger).
    last_observed: Arc<Mutex<usize>>,
    responder: fidl_fuchsia_cobalt_test::LoggerQuerierWatchLogsResponder,
}

// The LogState#log vectors in EventsLog are mutated by handle_cobalt_logger and
// concurrently observed by run_cobalt_query_service.
//
// The LogState#hanging vectors in EventsLog are concurrently mutated by run_cobalt_query_service
// (new values pushed) and handle_cobalt_logger (new values popped).
type EventsLogHandle = Arc<Mutex<EventsLog>>;

// Entries in the HashMap are concurrently added by run_cobalt_service and
// looked up by run_cobalt_query_service.
type LoggersHandle = Arc<Mutex<HashMap<u32, EventsLogHandle>>>;

/// Create a new Logger. Accepts all `project_id` values.
async fn run_cobalt_service(
    mut stream: cobalt::LoggerFactoryRequestStream,
    loggers: LoggersHandle,
) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        if let CreateLoggerFromProjectId { project_id, logger, responder } = event {
            let log =
                loggers.lock().await.entry(project_id).or_insert_with(Default::default).clone();
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
        let log_state = match event {
            LogEvent { metric_id, event_code, responder } => {
                let state = &mut log.log_event;
                state
                    .log
                    .push(CobaltEvent::builder(metric_id).with_event_code(event_code).as_event());
                let _ = responder.send(cobalt::Status::Ok);
                state
            }
            LogEventCount {
                metric_id,
                event_code,
                component,
                period_duration_micros,
                count,
                responder,
            } => {
                let state = &mut log.log_event_count;
                state.log.push(
                    CobaltEvent::builder(metric_id)
                        .with_event_code(event_code)
                        .with_component(component)
                        .as_count_event(period_duration_micros, count),
                );
                let _ = responder.send(cobalt::Status::Ok);
                state
            }
            LogElapsedTime { metric_id, event_code, component, elapsed_micros, responder } => {
                let state = &mut log.log_elapsed_time;
                state.log.push(
                    CobaltEvent::builder(metric_id)
                        .with_event_code(event_code)
                        .with_component(component)
                        .as_elapsed_time(elapsed_micros),
                );
                let _ = responder.send(cobalt::Status::Ok);
                state
            }
            LogFrameRate { metric_id, event_code, component, fps, responder } => {
                let state = &mut log.log_frame_rate;
                state.log.push(
                    CobaltEvent::builder(metric_id)
                        .with_event_code(event_code)
                        .with_component(component)
                        .as_frame_rate(fps),
                );
                let _ = responder.send(cobalt::Status::Ok);
                state
            }
            LogMemoryUsage { metric_id, event_code, component, bytes, responder } => {
                let state = &mut log.log_memory_usage;
                state.log.push(
                    CobaltEvent::builder(metric_id)
                        .with_event_code(event_code)
                        .with_component(component)
                        .as_memory_usage(bytes),
                );
                let _ = responder.send(cobalt::Status::Ok);
                state
            }
            LogString { metric_id, s, responder } => {
                let state = &mut log.log_string;
                state.log.push(CobaltEvent::builder(metric_id).as_string_event(s));
                let _ = responder.send(cobalt::Status::Ok);
                state
            }
            LogIntHistogram { metric_id, event_code, component, histogram, responder } => {
                let state = &mut log.log_int_histogram;
                state.log.push(
                    CobaltEvent::builder(metric_id)
                        .with_event_code(event_code)
                        .with_component(component)
                        .as_int_histogram(histogram),
                );
                let _ = responder.send(cobalt::Status::Ok);
                state
            }
            LogCobaltEvent { event, responder } => {
                let state = &mut log.log_cobalt_event;
                state.log.push(event);
                let _ = responder.send(cobalt::Status::Ok);
                state
            }
            LogCobaltEvents { mut events, responder } => {
                let state = &mut log.log_cobalt_events;
                state.log.append(&mut events);
                let _ = responder.send(cobalt::Status::Ok);
                state
            }
            e => unimplemented!("Event {:?} is not supported by the mock cobalt server", e),
        };

        while let Some(hanging_get_state) = log_state.hanging.pop() {
            let mut last_observed = hanging_get_state.last_observed.lock().await;
            *last_observed = log_state.log.len();
            let events =
                (&mut log_state.log).iter().take(MAX_QUERY_LENGTH).map(Clone::clone).collect();
            let _ = hanging_get_state.responder.send(&mut Ok((events, false)));
        }
    }
}

/// Handles requests to query the state of the mock.
async fn run_cobalt_query_service(
    mut stream: cobalt_test::LoggerQuerierRequestStream,
    loggers: LoggersHandle,
) -> Result<(), Error> {
    let mut client_state: HashMap<
        u32,
        HashMap<fidl_fuchsia_cobalt_test::LogMethod, Arc<Mutex<usize>>>,
    > = HashMap::new();
    use cobalt_test::LogMethod::*;

    while let Some(event) = stream.try_next().await? {
        match event {
            cobalt_test::LoggerQuerierRequest::WatchLogs { project_id, method, responder } => {
                if let Some(state) = loggers.lock().await.get(&project_id) {
                    let mut state = state.lock().await;
                    let log_state = match method {
                        LogEvent => &mut state.log_event,
                        LogEventCount => &mut state.log_event_count,
                        LogElapsedTime => &mut state.log_elapsed_time,
                        LogFrameRate => &mut state.log_frame_rate,
                        LogMemoryUsage => &mut state.log_memory_usage,
                        LogString => &mut state.log_string,
                        LogIntHistogram => &mut state.log_int_histogram,
                        LogCobaltEvent => &mut state.log_cobalt_event,
                        LogCobaltEvents => &mut state.log_cobalt_events,
                    };
                    let last_observed = client_state
                        .entry(project_id)
                        .or_insert_with(Default::default)
                        .entry(method)
                        .or_insert_with(Default::default);
                    let mut last_observed_len = last_observed.lock().await;
                    let current_len = log_state.log.len();
                    if current_len != *last_observed_len {
                        *last_observed_len = current_len;
                        let events = &mut log_state.log;
                        let more = events.len() > cobalt_test::MAX_QUERY_LENGTH as usize;
                        let events =
                            events.iter().take(MAX_QUERY_LENGTH).map(Clone::clone).collect();
                        responder.send(&mut Ok((events, more)))?;
                    } else {
                        log_state.hanging.push(HangingGetState {
                            responder: responder,
                            last_observed: last_observed.clone(),
                        });
                    }
                } else {
                    responder.send(&mut Err(cobalt_test::QueryError::LoggerNotFound))?;
                }
            }
            cobalt_test::LoggerQuerierRequest::ResetLogger {
                project_id,
                method,
                control_handle: _,
            } => {
                if let Some(log) = loggers.lock().await.get(&project_id) {
                    let mut state = log.lock().await;
                    match method {
                        LogEvent => state.log_event.log.clear(),
                        LogEventCount => state.log_event_count.log.clear(),
                        LogElapsedTime => state.log_elapsed_time.log.clear(),
                        LogFrameRate => state.log_frame_rate.log.clear(),
                        LogMemoryUsage => state.log_memory_usage.log.clear(),
                        LogString => state.log_string.log.clear(),
                        LogIntHistogram => state.log_int_histogram.log.clear(),
                        LogCobaltEvent => state.log_cobalt_event.log.clear(),
                        LogCobaltEvents => state.log_cobalt_events.log.clear(),
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
            .create_logger_from_project_id(1234, server)
            .await
            .expect("create_logger_from_project_id fidl call to succeed");

        assert!(loggers.lock().await.get(&1234).is_some());
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
            .create_logger_from_project_id(123, server)
            .await
            .expect("create_logger_from_project_id fidl call to succeed");

        // Log a single event
        logger_proxy.log_event(1, 2).await.expect("log_event fidl call to succeed");
        assert_eq!(
            Ok((vec![CobaltEvent::builder(1).with_event_code(2).as_event()], false)),
            querier_proxy
                .watch_logs(123, LogMethod::LogEvent)
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
            .create_logger_from_project_id(12, server)
            .await
            .expect("create_logger_from_project_id fidl call to succeed");

        // Log 1 more than the maximum number of events that can be stored and assert that
        // `more` flag is true on logger query request
        for i in 0..(MAX_QUERY_LENGTH as u32 + 1) {
            logger_proxy
                .log_event(i, i + 1)
                .await
                .expect("repeated log_event fidl call to succeed");
        }
        let (events, more) = querier_proxy
            .watch_logs(12, LogMethod::LogEvent)
            .await
            .expect("watch_logs fidl call to succeed")
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
                .watch_logs(1, LogMethod::LogEvent)
                .await
                .expect("watch_logs fidl call to succeed")
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
        let project_id = 1;

        factory_proxy
            .create_logger_from_project_id(project_id, server)
            .await
            .expect("create_logger_from_project_id fidl call to succeed");

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
        let log = log.get(&project_id).expect("project should have been created");
        let state = log.lock().await;
        assert_eq!(state.log_event.log.len(), 1);
        assert_eq!(state.log_event_count.log.len(), 1);
        assert_eq!(state.log_elapsed_time.log.len(), 1);
        assert_eq!(state.log_memory_usage.log.len(), 1);
        assert_eq!(state.log_frame_rate.log.len(), 1);
        assert_eq!(state.log_string.log.len(), 1);
        assert_eq!(state.log_int_histogram.log.len(), 1);
        assert_eq!(state.log_cobalt_event.log.len(), 1);
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
            .create_logger_from_project_id(987, server)
            .await
            .expect("create_logger_from_project_id fidl call to succeed");

        // Log a single event
        logger_proxy.log_event(1, 2).await.expect("log_event fidl call to succeed");
        assert_eq!(
            Ok((vec![CobaltEvent::builder(1).with_event_code(2).as_event()], false)),
            querier_proxy
                .watch_logs(987, LogMethod::LogEvent)
                .await
                .expect("log_event fidl call to succeed")
        );

        // Clear logger state
        querier_proxy
            .reset_logger(987, LogMethod::LogEvent)
            .expect("reset_logger fidl call to succeed");

        assert_eq!(
            Ok((vec![], false)),
            querier_proxy
                .watch_logs(987, LogMethod::LogEvent)
                .await
                .expect("watch_logs fidl call to succeed")
        );
    }

    #[test]
    fn mock_query_interface_hanging_get() {
        let mut executor = fuchsia_async::Executor::new().unwrap();
        let loggers = LoggersHandle::default();

        let (factory_proxy, factory_stream) = create_proxy_and_stream::<LoggerFactoryMarker>()
            .expect("create logger factroy proxy and stream to succeed");
        let (logger_proxy, logger_proxy_server_end) =
            create_proxy::<LoggerMarker>().expect("create logger proxy and server end to succeed");
        let (querier_proxy, query_stream) = create_proxy_and_stream::<LoggerQuerierMarker>()
            .expect("create logger querier proxy and stream to succeed");

        let cobalt_service = run_cobalt_service(factory_stream, loggers.clone());
        let cobalt_query_service = run_cobalt_query_service(query_stream, loggers.clone());

        futures::pin_mut!(cobalt_service);
        futures::pin_mut!(cobalt_query_service);

        // Neither of these futures should ever return if there no errors, so the joined future
        // will never return.
        let services = futures::future::join(cobalt_service, cobalt_query_service);

        let project_id = 765;
        let mut create_logger = futures::future::select(
            services,
            factory_proxy.create_logger_from_project_id(
                project_id,
                logger_proxy_server_end,
            ),
        );
        let create_logger_poll = executor.run_until_stalled(&mut create_logger);
        assert!(create_logger_poll.is_ready());

        let continuation = match create_logger_poll {
            core::task::Poll::Pending => unreachable!("we asserted that create_logger_poll was ready"),
            core::task::Poll::Ready(either) => match either {
                futures::future::Either::Left(_services_future_returned) => unreachable!("unexpected services future return (cannot be formatted with default formatter)"),
                futures::future::Either::Right((create_logger_status, services_continuation)) => {
                    assert_eq!(create_logger_status.expect("fidl call failed"), fidl_fuchsia_cobalt::Status::Ok);
                    services_continuation
                },
            }
        };

        let watch_logs_hanging_get = querier_proxy.watch_logs(project_id, LogMethod::LogEvent);
        let mut watch_logs_hanging_get =
            futures::future::select(continuation, watch_logs_hanging_get);
        let watch_logs_poll = executor.run_until_stalled(&mut watch_logs_hanging_get);
        assert!(watch_logs_poll.is_pending());

        let event_metric_id = 1;
        let event_code = 2;
        let log_event = logger_proxy.log_event(event_metric_id, event_code);

        let mut resolved_hanging_get = futures::future::join(watch_logs_hanging_get, log_event);
        let resolved_hanging_get = executor.run_until_stalled(&mut resolved_hanging_get);
        assert!(resolved_hanging_get.is_ready());

        let mut continuation = match resolved_hanging_get {
            core::task::Poll::Pending => {
                unreachable!("we asserted that resolved_hanging_get was ready")
            }
            core::task::Poll::Ready((watch_logs_result, log_event_result)) => {
                assert_eq!(
                    log_event_result.expect("expected log event to succeed"),
                    fidl_fuchsia_cobalt::Status::Ok
                );

                match watch_logs_result {
                    futures::future::Either::Left(_services_future_returned) => unreachable!("unexpected services future return (cannot be formatted with the default formatter)"),
                    futures::future::Either::Right((
                        cobalt_query_result,
                        services_continuation,
                    )) => {
                        let (mut logged_events, more) = cobalt_query_result
                            .expect("expect cobalt query FIDL call to succeed")
                            .expect("expect cobalt query call to return success");
                        assert_eq!(logged_events.len(), 1);
                        let mut logged_event = logged_events.pop().unwrap();
                        assert_eq!(logged_event.metric_id, event_metric_id);
                        assert_eq!(logged_event.event_codes.len(), 1);
                        assert_eq!(logged_event.event_codes.pop().unwrap(), event_code);
                        assert_eq!(more, false);
                        services_continuation
                    }
                }
            }
        };

        assert!(executor.run_until_stalled(&mut continuation).is_pending());
    }
}
