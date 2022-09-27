// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_metrics::MetricEvent,
    fuchsia_cobalt_builders::MetricEventExt,
    fuchsia_zircon_status as zx_status,
    futures::{lock::Mutex, StreamExt, TryStreamExt},
    std::{collections::HashMap, sync::Arc},
    tracing::info,
};

/// MAX_QUERY_LENGTH is used as a usize in this component
const MAX_QUERY_LENGTH: usize = fidl_fuchsia_metrics_test::MAX_QUERY_LENGTH as usize;

#[derive(Default)]
struct MetricLogState {
    log: Vec<MetricEvent>,
    hanging: Vec<MetricHangingGetState>,
}

// Does not record StartTimer, EndTimer, and LogCustomEvent requests
#[derive(Default)]
struct EventsLog {
    log_occurrence: MetricLogState,
    log_integer: MetricLogState,
    log_integer_histogram: MetricLogState,
    log_string: MetricLogState,
    log_metric_events: MetricLogState,
}

struct MetricHangingGetState {
    // last_observed is concurrently mutated by calls to run_metrics_query_service (one for each
    // client of fuchsia.metrics.test.MetricEventLoggerQuerier) and calls to
    // handle_metric_event_logger (one for each client of fuchsia.metrics.MetricEventLogger).
    last_observed: Arc<Mutex<usize>>,
    responder: fidl_fuchsia_metrics_test::MetricEventLoggerQuerierWatchLogsResponder,
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

async fn run_metrics_service(
    stream: fidl_fuchsia_metrics::MetricEventLoggerFactoryRequestStream,
    loggers: LoggersHandle,
) -> Result<(), fidl::Error> {
    use fidl_fuchsia_metrics::MetricEventLoggerFactoryRequest::*;
    stream
        .try_for_each_concurrent(None, |event| {
            async {
                match event {
                    CreateMetricEventLogger { project_spec, logger, responder } => {
                        let handler = make_handler(project_spec, &loggers, logger);
                        let () = responder.send(&mut Ok(()))?;
                        handler.await
                    }
                    CreateMetricEventLoggerWithExperiments {
                        project_spec,
                        logger,
                        responder,
                        ..
                    } => {
                        // TODO(fxb/90740): Support experiment_ids.
                        let handler = make_handler(project_spec, &loggers, logger);
                        let () = responder.send(&mut Ok(()))?;
                        handler.await
                    }
                }
            }
        })
        .await
}

async fn make_handler(
    project_spec: fidl_fuchsia_metrics::ProjectSpec,
    loggers: &LoggersHandle,
    logger: fidl::endpoints::ServerEnd<fidl_fuchsia_metrics::MetricEventLoggerMarker>,
) -> Result<(), fidl::Error> {
    let log = loggers
        .lock()
        .await
        .entry(project_spec.project_id.unwrap_or(0))
        .or_insert_with(Default::default)
        .clone();
    handle_metric_event_logger(logger.into_stream()?, log).await
}

async fn handle_metric_event_logger(
    stream: fidl_fuchsia_metrics::MetricEventLoggerRequestStream,
    log: EventsLogHandle,
) -> Result<(), fidl::Error> {
    use fidl_fuchsia_metrics::MetricEventLoggerRequest::*;
    let fut = stream.try_for_each_concurrent(None, |event| async {
        let mut log = log.lock().await;
        let log_state = match event {
            LogOccurrence { responder, metric_id, count, event_codes } => {
                let state = &mut log.log_occurrence;
                state.log.push(
                    MetricEvent::builder(metric_id)
                        .with_event_codes(event_codes)
                        .as_occurrence(count),
                );
                responder.send(&mut Ok(()))?;
                state
            }
            LogInteger { responder, metric_id, value, event_codes } => {
                let state = &mut log.log_integer;
                state.log.push(
                    MetricEvent::builder(metric_id).with_event_codes(event_codes).as_integer(value),
                );
                responder.send(&mut Ok(()))?;
                state
            }
            LogIntegerHistogram { responder, metric_id, histogram, event_codes } => {
                let state = &mut log.log_integer_histogram;
                state.log.push(
                    MetricEvent::builder(metric_id)
                        .with_event_codes(event_codes)
                        .as_integer_histogram(histogram),
                );
                responder.send(&mut Ok(()))?;
                state
            }
            LogString { responder, metric_id, string_value, event_codes } => {
                let state = &mut log.log_string;
                state.log.push(
                    MetricEvent::builder(metric_id)
                        .with_event_codes(event_codes)
                        .as_string(string_value),
                );
                responder.send(&mut Ok(()))?;
                state
            }
            LogMetricEvents { responder, mut events } => {
                let state = &mut log.log_metric_events;
                state.log.append(&mut events);
                responder.send(&mut Ok(()))?;
                state
            }
        };

        while let Some(hanging_get_state) = log_state.hanging.pop() {
            let mut last_observed = hanging_get_state.last_observed.lock().await;
            let mut events: Vec<MetricEvent> = (&mut log_state.log)
                .iter()
                .skip(*last_observed)
                .take(MAX_QUERY_LENGTH)
                .map(Clone::clone)
                .collect();
            *last_observed = log_state.log.len();
            hanging_get_state.responder.send(&mut events.iter_mut(), false)?;
        }
        Ok(())
    });

    match fut.await {
        // Don't consider PEER_CLOSED to be an error.
        Err(fidl::Error::ServerResponseWrite(zx_status::Status::PEER_CLOSED)) => Ok(()),
        other => other,
    }
}

async fn run_metrics_query_service(
    stream: fidl_fuchsia_metrics_test::MetricEventLoggerQuerierRequestStream,
    loggers: LoggersHandle,
) -> Result<(), fidl::Error> {
    use fidl_fuchsia_metrics_test::LogMethod;

    let _client_state: HashMap<_, _> = stream
        .try_fold(
            HashMap::new(),
            |mut client_state: HashMap<u32, HashMap<LogMethod, Arc<Mutex<usize>>>>, event| async {
                match event {
                    fidl_fuchsia_metrics_test::MetricEventLoggerQuerierRequest::WatchLogs {
                        project_id,
                        method,
                        responder,
                    } => {
                        let state = loggers
                            .lock()
                            .await
                            .entry(project_id)
                            .or_insert_with(Default::default)
                            .clone();

                        let mut state = state.lock().await;
                        let log_state = match method {
                            LogMethod::LogOccurrence => &mut state.log_occurrence,
                            LogMethod::LogInteger => &mut state.log_integer,
                            LogMethod::LogIntegerHistogram => &mut state.log_integer_histogram,
                            LogMethod::LogString => &mut state.log_string,
                            LogMethod::LogMetricEvents => &mut state.log_metric_events,
                        };
                        let last_observed = client_state
                            .entry(project_id)
                            .or_insert_with(Default::default)
                            .entry(method)
                            .or_insert_with(Default::default);
                        let mut last_observed_len = last_observed.lock().await;
                        let current_len = log_state.log.len();
                        if current_len != *last_observed_len {
                            let events = &mut log_state.log;
                            let more =
                                events.len() > fidl_fuchsia_metrics_test::MAX_QUERY_LENGTH as usize;
                            let mut events: Vec<_> = events
                                .iter()
                                .skip(*last_observed_len)
                                .take(MAX_QUERY_LENGTH)
                                .cloned()
                                .collect();
                            *last_observed_len = current_len;
                            responder.send(&mut events.iter_mut(), more)?;
                        } else {
                            log_state.hanging.push(MetricHangingGetState {
                                responder: responder,
                                last_observed: last_observed.clone(),
                            });
                        }
                    }
                    fidl_fuchsia_metrics_test::MetricEventLoggerQuerierRequest::ResetLogger {
                        project_id,
                        method,
                        ..
                    } => {
                        if let Some(log) = loggers.lock().await.get(&project_id) {
                            let mut state = log.lock().await;
                            match method {
                                LogMethod::LogOccurrence => state.log_occurrence.log.clear(),
                                LogMethod::LogInteger => state.log_integer.log.clear(),
                                LogMethod::LogIntegerHistogram => {
                                    state.log_integer_histogram.log.clear()
                                }
                                LogMethod::LogString => state.log_string.log.clear(),
                                LogMethod::LogMetricEvents => state.log_metric_events.log.clear(),
                            }
                        }
                    }
                }
                Ok(client_state)
            },
        )
        .await?;
    Ok(())
}

enum IncomingService {
    Metrics(fidl_fuchsia_metrics::MetricEventLoggerFactoryRequestStream),
    MetricsQuery(fidl_fuchsia_metrics_test::MetricEventLoggerQuerierRequestStream),
}

#[fuchsia::main(logging_tags = ["mock-cobalt"])]
async fn main() -> Result<(), Error> {
    info!("Starting mock cobalt service...");

    let loggers = LoggersHandle::default();

    let mut fs = fuchsia_component::server::ServiceFs::new_local();
    fs.dir("svc")
        .add_fidl_service(IncomingService::Metrics)
        .add_fidl_service(IncomingService::MetricsQuery);
    fs.take_and_serve_directory_handle()?;
    fs.then(futures::future::ok)
        .try_for_each_concurrent(None, |client_request| async {
            let loggers = loggers.clone();
            match client_request {
                IncomingService::Metrics(stream) => run_metrics_service(stream, loggers).await,
                IncomingService::MetricsQuery(stream) => {
                    run_metrics_query_service(stream, loggers).await
                }
            }
        })
        .await?;

    Ok(())
}

#[cfg(test)]
mod metrics_tests {
    use super::*;
    use anyhow::format_err;
    use async_utils::PollExt;
    use fidl::endpoints::{create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_metrics::*;
    use fidl_fuchsia_metrics_test::{LogMethod, MetricEventLoggerQuerierMarker};
    use fuchsia_async as fasync;
    use futures::FutureExt;

    #[fasync::run_until_stalled(test)]
    async fn mock_metric_event_logger_factory() {
        let loggers = LoggersHandle::default();

        let (factory_proxy, factory_stream) =
            create_proxy_and_stream::<MetricEventLoggerFactoryMarker>()
                .expect("create logger factroy proxy and stream to succeed");
        let (_logger_proxy, server) = create_proxy::<MetricEventLoggerMarker>()
            .expect("create logger proxy and server end to succeed");

        fasync::Task::local(run_metrics_service(factory_stream, loggers.clone()).map(|_| ()))
            .detach();

        assert!(loggers.lock().await.is_empty());

        factory_proxy
            .create_metric_event_logger(
                ProjectSpec { project_id: Some(1234), ..ProjectSpec::EMPTY },
                server,
            )
            .await
            .expect("create_logger_from_project_id fidl call to succeed")
            .expect("create_metric_event_logger should not return an error");

        assert!(loggers.lock().await.get(&1234).is_some());
    }

    #[fasync::run_until_stalled(test)]
    async fn mock_metric_event_logger_and_query_interface_single_event() {
        let loggers = LoggersHandle::default();

        // Create channels.
        let (factory_proxy, factory_stream) =
            create_proxy_and_stream::<MetricEventLoggerFactoryMarker>()
                .expect("create logger factroy proxy and stream to succeed");
        let (logger_proxy, server) = create_proxy::<MetricEventLoggerMarker>()
            .expect("create logger proxy and server end to succeed");
        let (querier_proxy, query_stream) =
            create_proxy_and_stream::<MetricEventLoggerQuerierMarker>()
                .expect("create logger querier proxy and stream to succeed");

        // Spawn service handlers. Any failures in the services spawned here will trigger panics
        // via expect method calls below.
        fasync::Task::local(run_metrics_service(factory_stream, loggers.clone()).map(|_| ()))
            .detach();
        fasync::Task::local(run_metrics_query_service(query_stream, loggers.clone()).map(|_| ()))
            .detach();

        factory_proxy
            .create_metric_event_logger(
                ProjectSpec { project_id: Some(123), ..ProjectSpec::EMPTY },
                server,
            )
            .await
            .expect("create_logger_from_project_id fidl call to succeed")
            .expect("create_metric_event_logger should not return an error");

        // Log a single event.
        logger_proxy
            .log_integer(12345, 123, &[])
            .await
            .expect("log_event fidl call to succeed")
            .expect("log_event should not return an error");
        assert_eq!(
            (vec![MetricEvent::builder(12345).as_integer(123)], false),
            querier_proxy
                .watch_logs(123, LogMethod::LogInteger)
                .await
                .expect("log_event fidl call to succeed")
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn mock_metric_event_logger_and_query_interface_multiple_events() {
        let loggers = LoggersHandle::default();

        // Create channels.
        let (factory_proxy, factory_stream) =
            create_proxy_and_stream::<MetricEventLoggerFactoryMarker>()
                .expect("create logger factroy proxy and stream to succeed");
        let (logger_proxy, server) = create_proxy::<MetricEventLoggerMarker>()
            .expect("create logger proxy and server end to succeed");
        let (querier_proxy, query_stream) =
            create_proxy_and_stream::<MetricEventLoggerQuerierMarker>()
                .expect("create logger querier proxy and stream to succeed");

        // Spawn service handlers. Any failures in the services spawned here will trigger panics
        // via expect method calls below.
        fasync::Task::local(run_metrics_service(factory_stream, loggers.clone()).map(|_| ()))
            .detach();
        fasync::Task::local(run_metrics_query_service(query_stream, loggers.clone()).map(|_| ()))
            .detach();

        factory_proxy
            .create_metric_event_logger(
                ProjectSpec { project_id: Some(12), ..ProjectSpec::EMPTY },
                server,
            )
            .await
            .expect("create_logger_from_project_id fidl call to succeed")
            .expect("create_metric_event_logger should not return an error");

        // Log 1 more than the maximum number of events that can be stored and assert that
        // `more` flag is true on logger query request.
        for i in 0..(MAX_QUERY_LENGTH as u32 + 1) {
            logger_proxy
                .log_integer(i, (i + 1) as i64, &[1])
                .await
                .expect("repeated log_event fidl call to succeed")
                .expect("repeated log_event should not return an error");
        }
        let (events, more) = querier_proxy
            .watch_logs(12, LogMethod::LogInteger)
            .await
            .expect("watch_logs fidl call to succeed");
        assert_eq!(MetricEvent::builder(0).with_event_code(1).as_integer(1), events[0]);
        assert_eq!(MAX_QUERY_LENGTH, events.len());
        assert!(more);
    }

    #[test]
    fn mock_query_interface_no_logger_never_completes() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let loggers = LoggersHandle::default();

        let (factory_proxy, factory_stream) =
            create_proxy_and_stream::<MetricEventLoggerFactoryMarker>()
                .expect("create logger factroy proxy and stream to succeed");
        let (logger_proxy, server) = create_proxy::<MetricEventLoggerMarker>()
            .expect("create logger proxy and server end to succeed");
        let (querier_proxy, query_stream) =
            create_proxy_and_stream::<MetricEventLoggerQuerierMarker>()
                .expect("create logger querier proxy and stream to succeed");

        // Spawn service handlers. Any failures in the services spawned here will trigger panics
        // via expect method calls below.
        fasync::Task::local(run_metrics_service(factory_stream, loggers.clone()).map(|_| ()))
            .detach();
        fasync::Task::local(run_metrics_query_service(query_stream, loggers).map(|_| ())).detach();

        // watch_logs2 query does not complete without a logger for the requested project id.
        let watch_logs_fut = querier_proxy.watch_logs(123, LogMethod::LogInteger);
        futures::pin_mut!(watch_logs_fut);

        assert!(exec.run_until_stalled(&mut watch_logs_fut).is_pending());

        // Create a new logger for the requested project id
        let create_logger_fut = factory_proxy.create_metric_event_logger(
            ProjectSpec { project_id: Some(123), ..ProjectSpec::EMPTY },
            server,
        );
        futures::pin_mut!(create_logger_fut);
        exec.run_until_stalled(&mut create_logger_fut)
            .expect("logger creation future to complete")
            .expect("create_logger_from_project_id fidl call to succeed")
            .expect("create_metric_event_logger should not return an error");

        // watch_logs query still does not complete without a LogEvent for the requested project
        // id.
        assert!(exec.run_until_stalled(&mut watch_logs_fut).is_pending());

        // Log a single event
        let log_event_fut = logger_proxy.log_integer(1, 2, &[3]);
        futures::pin_mut!(log_event_fut);
        exec.run_until_stalled(&mut log_event_fut)
            .expect("log event future to complete")
            .expect("log_event fidl call to succeed")
            .expect("log_event should not return an error");

        // finally, now that a logger and log event have been created, watch_logs2 query will
        // succeed.
        let result = exec
            .run_until_stalled(&mut watch_logs_fut)
            .expect("log_event future to complete")
            .expect("log_event fidl call to succeed");
        assert_eq!((vec![MetricEvent::builder(1).with_event_code(3).as_integer(2)], false), result);
    }

    #[test]
    fn mock_query_interface_no_events_never_completes() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let loggers = LoggersHandle::default();

        // Create channels.
        let (factory_proxy, factory_stream) =
            create_proxy_and_stream::<MetricEventLoggerFactoryMarker>()
                .expect("create logger factroy proxy and stream to succeed");
        let (querier_proxy, query_stream) =
            create_proxy_and_stream::<MetricEventLoggerQuerierMarker>()
                .expect("create logger querier proxy and stream to succeed");
        let (_logger_proxy, server) = create_proxy::<MetricEventLoggerMarker>()
            .expect("create logger proxy and server end to succeed");

        // Spawn service handlers. Any failures in the services spawned here will trigger panics
        // via expect method calls below.
        fasync::Task::local(run_metrics_service(factory_stream, loggers.clone()).map(|_| ()))
            .detach();
        fasync::Task::local(run_metrics_query_service(query_stream, loggers.clone()).map(|_| ()))
            .detach();

        let test = async move {
            factory_proxy
                .create_metric_event_logger(
                    ProjectSpec { project_id: Some(123), ..ProjectSpec::EMPTY },
                    server,
                )
                .await
                .expect("create_logger_from_project_id fidl call to succeed")
                .expect("create_metric_event_logger to not return an error");

            assert_eq!(
                (vec![MetricEvent::builder(1).with_event_code(2).as_integer(3)], false),
                querier_proxy
                    .watch_logs(123, LogMethod::LogInteger)
                    .await
                    .expect("log_event fidl call to succeed")
            );
        };
        futures::pin_mut!(test);
        assert!(exec.run_until_stalled(&mut test).is_pending());
    }

    #[fasync::run_until_stalled(test)]
    async fn mock_metric_event_logger_logger_type_tracking() -> Result<(), anyhow::Error> {
        let loggers = LoggersHandle::default();

        let (factory_proxy, factory_stream) =
            create_proxy_and_stream::<MetricEventLoggerFactoryMarker>()
                .expect("create logger factroy proxy and stream to succeed");
        let (logger_proxy, server) = create_proxy::<MetricEventLoggerMarker>()
            .expect("create logger proxy and server end to succeed");

        fasync::Task::local(run_metrics_service(factory_stream, loggers.clone()).map(|_| ()))
            .detach();
        let project_id = 1;

        factory_proxy
            .create_metric_event_logger(
                ProjectSpec { project_id: Some(project_id), ..ProjectSpec::EMPTY },
                server,
            )
            .await
            .expect("create_logger_from_project_id fidl call to succeed")
            .expect("create_metric_event_logger to not return an error");

        let metric_id = 1;
        let event_code = 2;
        let count = 3;
        let value = 4;
        let string = "value";

        logger_proxy
            .log_occurrence(metric_id, count, &[event_code])
            .await?
            .map_err(|e| format_err!("cobalt error {:?}", e))?;
        logger_proxy
            .log_integer(metric_id, value, &[event_code])
            .await?
            .map_err(|e| format_err!("cobalt error {:?}", e))?;
        logger_proxy
            .log_integer_histogram(metric_id, &mut vec![].into_iter(), &[event_code])
            .await?
            .map_err(|e| format_err!("cobalt error {:?}", e))?;
        logger_proxy
            .log_string(metric_id, string, &[event_code])
            .await?
            .map_err(|e| format_err!("cobalt error {:?}", e))?;
        logger_proxy
            .log_metric_events(
                &mut vec![&mut MetricEvent::builder(metric_id)
                    .with_event_code(event_code)
                    .as_occurrence(count)]
                .into_iter(),
            )
            .await?
            .map_err(|e| format_err!("cobalt error {:?}", e))?;
        let log = loggers.lock().await;
        let log = log.get(&project_id).expect("project should have been created");
        let state = log.lock().await;
        assert_eq!(state.log_occurrence.log.len(), 1);
        assert_eq!(state.log_integer.log.len(), 1);
        assert_eq!(state.log_integer_histogram.log.len(), 1);
        assert_eq!(state.log_string.log.len(), 1);
        assert_eq!(state.log_metric_events.log.len(), 1);
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn mock_query_interface_reset_state() {
        let loggers = LoggersHandle::default();

        // Create channels.
        let (factory_proxy, factory_stream) =
            create_proxy_and_stream::<MetricEventLoggerFactoryMarker>()
                .expect("create logger factroy proxy and stream to succeed");
        let (logger_proxy, server) = create_proxy::<MetricEventLoggerMarker>()
            .expect("create logger proxy and server end to succeed");
        let (querier_proxy, query_stream) =
            create_proxy_and_stream::<MetricEventLoggerQuerierMarker>()
                .expect("create logger querier proxy and stream to succeed");

        // Spawn service handlers. Any failures in the services spawned here will trigger panics
        // via expect method calls below.
        fasync::Task::local(run_metrics_service(factory_stream, loggers.clone()).map(|_| ()))
            .detach();
        fasync::Task::local(run_metrics_query_service(query_stream, loggers.clone()).map(|_| ()))
            .detach();

        factory_proxy
            .create_metric_event_logger(
                ProjectSpec { project_id: Some(987), ..ProjectSpec::EMPTY },
                server,
            )
            .await
            .expect("create_logger_from_project_id fidl call to succeed")
            .expect("create_metric_event_logger failed internally");

        // Log a single event.
        logger_proxy
            .log_integer(1, 2, &[3])
            .await
            .expect("log_event fidl call to succeed")
            .expect("log_integer to not return an error");
        assert_eq!(
            (vec![MetricEvent::builder(1).with_event_code(3).as_integer(2)], false),
            querier_proxy
                .watch_logs(987, LogMethod::LogInteger)
                .await
                .expect("log_event fidl call to succeed")
        );

        // Clear logger state.
        querier_proxy
            .reset_logger(987, LogMethod::LogInteger)
            .expect("reset_logger fidl call to succeed");

        assert_eq!(
            (vec![], false),
            querier_proxy
                .watch_logs(987, LogMethod::LogInteger)
                .await
                .expect("watch_logs fidl call to succeed")
        );
    }

    #[test]
    fn mock_query_interface_hanging_get() {
        let mut executor = fuchsia_async::TestExecutor::new().unwrap();
        let loggers = LoggersHandle::default();

        let (factory_proxy, factory_stream) =
            create_proxy_and_stream::<MetricEventLoggerFactoryMarker>()
                .expect("create logger factroy proxy and stream to succeed");
        let (logger_proxy, logger_proxy_server_end) = create_proxy::<MetricEventLoggerMarker>()
            .expect("create logger proxy and server end to succeed");
        let (querier_proxy, query_stream) =
            create_proxy_and_stream::<MetricEventLoggerQuerierMarker>()
                .expect("create logger querier proxy and stream to succeed");

        let cobalt_service = run_metrics_service(factory_stream, loggers.clone());
        let cobalt_query_service = run_metrics_query_service(query_stream, loggers.clone());

        futures::pin_mut!(cobalt_service);
        futures::pin_mut!(cobalt_query_service);

        // Neither of these futures should ever return if there no errors, so the joined future
        // will never return.
        let services = futures::future::join(cobalt_service, cobalt_query_service);

        let project_id = 765;
        let mut create_logger = futures::future::select(
            services,
            factory_proxy.create_metric_event_logger(
                ProjectSpec { project_id: Some(project_id), ..ProjectSpec::EMPTY },
                logger_proxy_server_end,
            ),
        );
        let create_logger_poll = executor.run_until_stalled(&mut create_logger);
        assert!(create_logger_poll.is_ready());

        let mut continuation = match create_logger_poll {
            core::task::Poll::Pending => {
                unreachable!("we asserted that create_logger_poll was ready")
            }
            core::task::Poll::Ready(either) => match either {
                futures::future::Either::Left(_services_future_returned) => unreachable!(
                    "unexpected services future return (cannot be formatted with default formatter)"
                ),
                futures::future::Either::Right((create_logger_status, services_continuation)) => {
                    assert_eq!(create_logger_status.expect("fidl call failed"), Ok(()));
                    services_continuation
                }
            },
        };

        // Resolve two hanging gets and ensure that only the novel data (the same data both times)
        // is returned.
        for _ in 0..2 {
            let watch_logs_hanging_get =
                querier_proxy.watch_logs(project_id, LogMethod::LogInteger);
            let mut watch_logs_hanging_get =
                futures::future::select(continuation, watch_logs_hanging_get);
            let watch_logs_poll = executor.run_until_stalled(&mut watch_logs_hanging_get);
            assert!(watch_logs_poll.is_pending());

            let event_metric_id = 1;
            let event_code = 2;
            let value = 3;
            let log_event = logger_proxy.log_integer(event_metric_id, value, &[event_code]);

            let mut resolved_hanging_get = futures::future::join(watch_logs_hanging_get, log_event);
            let resolved_hanging_get = executor.run_until_stalled(&mut resolved_hanging_get);
            assert!(resolved_hanging_get.is_ready());

            continuation = match resolved_hanging_get {
                core::task::Poll::Pending => {
                    unreachable!("we asserted that resolved_hanging_get was ready")
                }
                core::task::Poll::Ready((watch_logs_result, log_event_result)) => {
                    assert_eq!(log_event_result.expect("expected log event to succeed"), Ok(()));

                    match watch_logs_result {
                        futures::future::Either::Left(_services_future_returned) => unreachable!(
                            "unexpected services future return (cannot be formatted with the \
                             default formatter)"
                        ),
                        futures::future::Either::Right((
                            cobalt_query_result,
                            services_continuation,
                        )) => {
                            let (mut logged_events, more) = cobalt_query_result
                                .expect("expect cobalt query FIDL call to succeed");
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
}
