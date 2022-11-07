// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::stats::LogIdentifier,
    anyhow::format_err,
    diagnostics_data::{LogsData, Severity},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_metrics::{MetricEventLoggerFactoryMarker, MetricEventLoggerProxy, ProjectSpec},
    fidl_fuchsia_ui_activity as factivity, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    serde::Deserialize,
    std::collections::{HashMap, HashSet},
    std::convert::TryFrom,
    tracing::warn,
};

#[derive(Deserialize)]
pub struct MetricSpecs {
    customer_id: u32,
    project_id: u32,
    granular_error_count_metric_id: u32,
    granular_error_interval_count_metric_id: u32,
    granular_idle_state_log_count_metric_id: u32,
}

#[derive(Hash, PartialEq, Eq, Clone)]
struct LogIdentifierAndComponent {
    log_identifier: LogIdentifier,
    component_event_code: u32,
}

pub struct MetricLogger {
    specs: MetricSpecs,
    proxy: MetricEventLoggerProxy,
    component_map: ComponentEventCodeMap,
    current_interval_errors: HashSet<LogIdentifierAndComponent>,
    next_interval_index: u64,
    reached_capacity: bool,
    last_cobalt_failure_time: u64,
}

type ComponentEventCodeMap = HashMap<String, u32>;

/// The event code that is used if there is no corresponding event code for the component URL.
pub const OTHER_EVENT_CODE: u32 = 1_000_000;

/// What file path to use if the source of the log is not known.
pub const UNKNOWN_SOURCE_FILE_PATH: &str = "<Unknown source>";

/// The length of an interval for which we report every ERROR at most once.
pub const INTERVAL_IN_MINUTES: u64 = 15;

/// Maximum number of unique ERRORs reported in one interval. Once this limit is reached, we no
/// longer log the interval count metric, but the error count metric is still logged. This is an
/// arbitrary limit that ensures the set containing the errors doesn't get too large.
pub const MAX_ERRORS_PER_INTERVAL: usize = 150;

/// What file path to use for the ping message.
pub const PING_FILE_PATH: &str = "<Ping>";

/// What line number to use for the ping message or when the source is not known.
pub const EMPTY_LINE_NUMBER: u64 = 0;

/// Number of seconds to wait after Cobalt fails before logging another metric.
pub const COBALT_BACKOFF_SECONDS: u64 = 60;

// Establishes a channel to Cobalt.
async fn connect_to_cobalt(specs: &MetricSpecs) -> Result<MetricEventLoggerProxy, anyhow::Error> {
    let mut project_spec = ProjectSpec::EMPTY;
    project_spec.customer_id = Some(specs.customer_id);
    project_spec.project_id = Some(specs.project_id);

    let metric_logger_factory = connect_to_protocol::<MetricEventLoggerFactoryMarker>()?;
    let (proxy, request) = create_proxy().unwrap();
    metric_logger_factory
        .create_metric_event_logger(project_spec, request)
        .await?
        .map_err(|e| format_err!("error response {:?}", e))?;
    Ok(proxy)
}

impl MetricLogger {
    /// Provides MetricLogger for testing.
    #[cfg(test)]
    fn init_for_testing(
        specs: MetricSpecs,
        component_map: ComponentEventCodeMap,
        proxy: MetricEventLoggerProxy,
    ) -> Self {
        Self {
            specs,
            proxy,
            component_map,
            current_interval_errors: HashSet::new(),
            next_interval_index: 0,
            reached_capacity: false,
            last_cobalt_failure_time: 0,
        }
    }

    /// Create a MetricLogger that logs the given MetricSpecs.
    pub async fn new(
        specs: MetricSpecs,
        component_map: ComponentEventCodeMap,
    ) -> Result<MetricLogger, anyhow::Error> {
        let proxy = connect_to_cobalt(&specs).await?;
        Ok(Self {
            specs,
            proxy,
            component_map,
            current_interval_errors: HashSet::new(),
            next_interval_index: 0,
            reached_capacity: false,
            last_cobalt_failure_time: 0,
        })
    }

    /// - Processes one line of log. If the file path and line number of the
    /// location that the log originated from is known.
    /// - Report the idle state metric if the log was generated during idle
    /// state regardless of the severity of the log.
    /// - Report the error metric if the severity of the log is ERROR or FATAL
    /// Input:
    /// - log: Log data
    /// - prev_device_state_change: Tuple with a previous state of the device and transition time
    /// - curr_device_state_change: Tuple with a latest state of the device and transition time
    pub async fn process(
        self: &mut Self,
        log: &LogsData,
        prev_device_state_change: Option<(factivity::State, i64)>,
        curr_device_state_change: Option<(factivity::State, i64)>,
    ) -> Result<(), anyhow::Error> {
        if log.metadata.component_url.is_none() {
            return Ok(());
        }
        let url = log.metadata.component_url.as_ref().unwrap();

        let log_identifier = LogIdentifier::try_from(log).unwrap_or(LogIdentifier {
            file_path: UNKNOWN_SOURCE_FILE_PATH.to_string(),
            line_no: EMPTY_LINE_NUMBER,
        });
        let event_code = self.component_map.get(url).unwrap_or(&OTHER_EVENT_CODE);
        let identifier_and_component =
            LogIdentifierAndComponent { log_identifier, component_event_code: *event_code };

        match curr_device_state_change {
            Some(curr_device_state) => match prev_device_state_change {
                Some(prev_device_state) => {
                    if self.is_idle_state_log(
                        log.metadata.timestamp,
                        curr_device_state,
                        prev_device_state,
                    ) {
                        self.log_metric(
                            self.specs.granular_idle_state_log_count_metric_id,
                            &identifier_and_component,
                        )
                        .await?;
                    }
                }
                None => (),
            },
            None => warn!("Current state of the device is unknown."),
        }

        if log.metadata.severity != Severity::Error && log.metadata.severity != Severity::Fatal {
            return Ok(());
        }
        self.maybe_clear_errors_and_send_ping().await?;
        self.log_metric(self.specs.granular_error_count_metric_id, &identifier_and_component)
            .await?;
        if self.current_interval_errors.len() >= MAX_ERRORS_PER_INTERVAL {
            // Only print this warning once per interval: the first time that we reached capacity.
            if !self.reached_capacity {
                warn!("Received too many ERRORs. Will temporarily halt logging the metric.");
                self.reached_capacity = true;
            }
            return Ok(());
        }
        if !self.current_interval_errors.contains(&identifier_and_component) {
            self.log_metric(
                self.specs.granular_error_interval_count_metric_id,
                &identifier_and_component,
            )
            .await?;
            self.current_interval_errors.insert(identifier_and_component);
        }
        Ok(())
    }

    async fn maybe_clear_errors_and_send_ping(self: &mut Self) -> Result<(), anyhow::Error> {
        let interval_index =
            fasync::Time::now().into_nanos() as u64 / 1_000_000_000 / 60 / INTERVAL_IN_MINUTES;
        if interval_index >= self.next_interval_index {
            self.current_interval_errors.clear();
            self.reached_capacity = false;
            self.next_interval_index = interval_index + 1;
            let identifier_and_component = LogIdentifierAndComponent {
                log_identifier: LogIdentifier {
                    file_path: PING_FILE_PATH.to_string(),
                    line_no: EMPTY_LINE_NUMBER,
                },
                component_event_code: OTHER_EVENT_CODE,
            };
            self.log_metric(
                self.specs.granular_error_interval_count_metric_id,
                &identifier_and_component,
            )
            .await?;
        }
        Ok(())
    }

    async fn log_metric(
        self: &mut Self,
        metric_id: u32,
        log_identifier_and_component: &LogIdentifierAndComponent,
    ) -> Result<(), anyhow::Error> {
        let now = fasync::Time::now().into_nanos() as u64 / 1_000_000_000;
        // If Cobalt failed recently, don't try to log another metric again. Cobalt may log an ERROR
        // in response to the failure and if we log the metric in response to the ERROR, we will get
        // stuck in an endless loop.
        if self.last_cobalt_failure_time != 0
            && now < self.last_cobalt_failure_time + COBALT_BACKOFF_SECONDS
        {
            return Ok(());
        }
        let status_result = self
            .proxy
            .log_string(
                metric_id,
                &log_identifier_and_component.log_identifier.file_path,
                &[
                    log_identifier_and_component.log_identifier.line_no as u32,
                    log_identifier_and_component.component_event_code,
                ],
            )
            .await;
        // Re-establish connection to Cobalt if channel is closed.
        if let Err(fidl::Error::ClientChannelClosed { .. }) = status_result {
            self.proxy = connect_to_cobalt(&self.specs).await?;
        }
        let status = status_result?;
        match status {
            Ok(()) => Ok(()),
            Err(e) => {
                warn!(
                    seconds = COBALT_BACKOFF_SECONDS,
                    "Not logging metrics because Cobalt failed",
                );
                self.last_cobalt_failure_time = now;
                Err(anyhow::format_err!("Cobalt returned error: {}", e as u8))
            }
        }
    }

    /// Log will be considered idle state log if it matches following criteria:
    /// - Current state of the device is Idle and the log is generated after that.
    /// - Previous state of the device is Idle, current state of the device is Active
    ///   and the log is reported between those two device states.
    fn is_idle_state_log(
        self: &mut Self,
        log_timestamp: i64,
        curr_device_state: (factivity::State, i64),
        prev_device_state: (factivity::State, i64),
    ) -> bool {
        let curr_state = curr_device_state.0;
        let curr_state_change_time = curr_device_state.1;
        let prev_state = prev_device_state.0;
        let prev_state_change_time = prev_device_state.1;

        (curr_state == factivity::State::Idle && curr_state_change_time <= log_timestamp)
            || (prev_state == factivity::State::Idle
                && curr_state == factivity::State::Active
                && prev_state_change_time <= log_timestamp
                && curr_state_change_time > log_timestamp)
    }
}

#[cfg(test)]
mod tests {

    use super::*;
    use diagnostics_data::BuilderArgs;
    use diagnostics_data::LogsDataBuilder;
    use fidl_fuchsia_metrics::{
        MetricEventLoggerMarker, MetricEventLoggerRequest, MetricEventLoggerRequestStream,
    };
    use fuchsia_zircon as zx;
    use futures::StreamExt;
    use futures::TryStreamExt;

    const TEST_URL: &'static str = "fake-test-env/test-component.cmx";
    const TEST_MONIKER: &'static str = "fuchsia-pkg://fuchsia.com/testing123#test-component.cmx";
    const TEST_METRIC_SPECS: MetricSpecs = MetricSpecs {
        customer_id: 1,
        project_id: 1,
        granular_error_count_metric_id: 3,
        granular_error_interval_count_metric_id: 4,
        granular_idle_state_log_count_metric_id: 5,
    };

    /// Test scenario where MetricLogger.log_metric() is successfully able to log the Cobalt Metrics
    #[fasync::run_singlethreaded(test)]
    async fn test_logged_metric_successfully() {
        let specs = TEST_METRIC_SPECS;
        let component_map = ComponentEventCodeMap::new();
        let fake_metric_event_provider = FakeMetricEventProvider::new();
        let mut metric_logger = MetricLogger::init_for_testing(
            specs,
            component_map,
            fake_metric_event_provider.metric_event_logger_proxy,
        );
        let log_identifier_and_component = LogIdentifierAndComponent {
            log_identifier: LogIdentifier { file_path: String::from("xyz"), line_no: 1 },
            component_event_code: 1,
        };
        let expected_log_identifier_and_component = log_identifier_and_component.clone();
        let mut stream = fake_metric_event_provider.metric_event_logger_stream;

        fasync::Task::spawn(async move {
            let _ = metric_logger.log_metric(1, &log_identifier_and_component).await;
        })
        .detach();

        verify_single_logged_string_metric(
            stream.try_next().await.unwrap(),
            1,
            String::from("xyz"),
            expected_log_identifier_and_component,
        )
        .await;
    }

    /// Test scenario where MetricLogger.log_metric() is unable to log the Cobalt Metrics
    #[fasync::run_singlethreaded(test)]
    async fn test_logged_metric_unsuccessfully() {
        let specs = TEST_METRIC_SPECS;
        let component_map = ComponentEventCodeMap::new();
        let fake_metric_event_provider = FakeMetricEventProvider::new();
        let mut metric_logger = MetricLogger::init_for_testing(
            specs,
            component_map,
            fake_metric_event_provider.metric_event_logger_proxy,
        );
        let log_identifier_and_component = LogIdentifierAndComponent {
            log_identifier: LogIdentifier { file_path: String::from("xyz"), line_no: 1 },
            component_event_code: 1,
        };

        fasync::Task::spawn(async move {
            assert!(metric_logger.log_metric(1, &log_identifier_and_component).await.is_err());
        })
        .detach();
    }

    /// Test scenario where MetricLogger.maybe_clear_errors_and_send_ping is able to clear the errors and send ping to Cobalt.
    #[fasync::run_singlethreaded(test)]
    async fn test_successfully_cleared_errors_and_sent_ping() {
        let specs = TEST_METRIC_SPECS;
        let component_map = ComponentEventCodeMap::new();
        let fake_metric_event_provider = FakeMetricEventProvider::new();
        let mut metric_logger = MetricLogger::init_for_testing(
            specs,
            component_map,
            fake_metric_event_provider.metric_event_logger_proxy,
        );
        let mut stream = fake_metric_event_provider.metric_event_logger_stream;

        fasync::Task::spawn(async move {
            let _ = metric_logger.maybe_clear_errors_and_send_ping().await;
        })
        .detach();

        verify_single_logged_string_metric(
            stream.try_next().await.unwrap(),
            4,
            PING_FILE_PATH.to_string(),
            get_ping_log_identifier_and_component(),
        )
        .await;
    }

    /// Test scenario where MetricLogger.maybe_clear_errors_and_send_ping doesn't need to clear the errors and send ping to Cobalt.
    #[fasync::run_singlethreaded(test)]
    async fn test_no_need_to_clear_errors_and_send_ping() {
        let specs = TEST_METRIC_SPECS;
        let component_map = ComponentEventCodeMap::new();
        let mut fake_metric_event_provider = FakeMetricEventProvider::new();
        let mut metric_logger = MetricLogger::init_for_testing(
            specs,
            component_map,
            fake_metric_event_provider.metric_event_logger_proxy,
        );
        metric_logger.next_interval_index =
            fasync::Time::now().into_nanos() as u64 / 1_000_000_000 / 60 / INTERVAL_IN_MINUTES + 1;

        fasync::Task::spawn(async move {
            let _ = metric_logger.maybe_clear_errors_and_send_ping().await;
        })
        .detach();

        assert!(fake_metric_event_provider.metric_event_logger_stream.next().await.is_none());
    }

    /// MetricLogger.process() doesn't process Severity::Info under error metric
    #[fasync::run_singlethreaded(test)]
    async fn test_non_error_log_process() -> Result<(), anyhow::Error> {
        let specs = TEST_METRIC_SPECS;
        let mut component_map = ComponentEventCodeMap::new();
        component_map.insert(TEST_URL.to_string(), 1);
        let fake_metric_event_provider = FakeMetricEventProvider::new();
        let mut metric_logger = MetricLogger::init_for_testing(
            specs,
            component_map,
            fake_metric_event_provider.metric_event_logger_proxy,
        );
        let data = get_sample_logs_data_with_severity(Severity::Info);
        metric_logger.process(&data, None, None).await
    }

    /// MetricLogger.process() processes and reports logs to Cobalt Metrics if the severity is  Severity::Error
    #[fasync::run_singlethreaded(test)]
    async fn test_logs_proccessed_successfully() {
        let specs = TEST_METRIC_SPECS;
        let mut component_map = ComponentEventCodeMap::new();
        component_map.insert(TEST_URL.to_string(), 1);
        let fake_metric_event_provider = FakeMetricEventProvider::new();
        let mut metric_logger = MetricLogger::init_for_testing(
            specs,
            component_map,
            fake_metric_event_provider.metric_event_logger_proxy,
        );
        let data = get_sample_logs_data_with_severity(Severity::Error);
        let expected_log_identifier_and_component = LogIdentifierAndComponent {
            log_identifier: LogIdentifier {
                file_path: "path/to/file.cc".to_string(),
                line_no: 123u64,
            },
            component_event_code: 1,
        };
        let mut stream = fake_metric_event_provider.metric_event_logger_stream;

        fasync::Task::spawn(async move {
            let _ = metric_logger.process(&data, None, None).await;
        })
        .detach();

        verify_single_logged_string_metric(
            stream.try_next().await.unwrap(),
            4,
            PING_FILE_PATH.to_string(),
            get_ping_log_identifier_and_component(),
        )
        .await;
        verify_single_logged_string_metric(
            stream.try_next().await.unwrap(),
            3,
            "path/to/file.cc".to_string(),
            expected_log_identifier_and_component.clone(),
        )
        .await;
    }

    /// MetricLogger.process() processes and reports log to Cobalt Metrics with severity Error with OTHER_EVENT_CODE
    #[fasync::run_singlethreaded(test)]
    async fn test_log_with_other_event_code() {
        let specs = TEST_METRIC_SPECS;
        let component_map = ComponentEventCodeMap::new();
        let fake_metric_event_provider = FakeMetricEventProvider::new();
        let mut metric_logger = MetricLogger::init_for_testing(
            specs,
            component_map,
            fake_metric_event_provider.metric_event_logger_proxy,
        );
        let data = get_sample_logs_data_with_severity(Severity::Error);
        let expected_log_identifier_and_component = LogIdentifierAndComponent {
            log_identifier: LogIdentifier { file_path: "path".to_string(), line_no: 123u64 },
            component_event_code: OTHER_EVENT_CODE,
        };
        let mut stream = fake_metric_event_provider.metric_event_logger_stream;

        fasync::Task::spawn(async move {
            let _ = metric_logger.process(&data, None, None).await;
        })
        .detach();

        verify_single_logged_string_metric(
            stream.try_next().await.unwrap(),
            4,
            PING_FILE_PATH.to_string(),
            get_ping_log_identifier_and_component(),
        )
        .await;
        verify_single_logged_string_metric(
            stream.try_next().await.unwrap(),
            3,
            "path/to/file.cc".to_string(),
            expected_log_identifier_and_component.clone(),
        )
        .await;
    }

    /// MetricLogger.process() processes and reports unparsable log to Cobalt Metrics with
    /// severity Error, OTHER_EVENT_CODE, UNKNOWN_SOURCE_FILE_PATH and EMPTY_LINE_NUMBER
    #[fasync::run_singlethreaded(test)]
    async fn test_unparsable_logs_data_proccess() {
        let specs = TEST_METRIC_SPECS;
        let component_map = ComponentEventCodeMap::new();
        let fake_metric_event_provider = FakeMetricEventProvider::new();
        let mut metric_logger = MetricLogger::init_for_testing(
            specs,
            component_map,
            fake_metric_event_provider.metric_event_logger_proxy,
        );
        let data = LogsDataBuilder::new(BuilderArgs {
            timestamp_nanos: zx::Time::from_nanos(1).into(),
            component_url: Some(TEST_URL.to_string()),
            moniker: TEST_MONIKER.to_string(),
            severity: Severity::Fatal,
        })
        .build();
        let expected_log_identifier_and_component = LogIdentifierAndComponent {
            log_identifier: LogIdentifier {
                file_path: UNKNOWN_SOURCE_FILE_PATH.to_string(),
                line_no: EMPTY_LINE_NUMBER,
            },
            component_event_code: OTHER_EVENT_CODE,
        };
        let mut stream = fake_metric_event_provider.metric_event_logger_stream;

        fasync::Task::spawn(async move {
            let _ = metric_logger.process(&data, None, None).await;
        })
        .detach();

        verify_single_logged_string_metric(
            stream.try_next().await.unwrap(),
            4,
            PING_FILE_PATH.to_string(),
            get_ping_log_identifier_and_component(),
        )
        .await;
        verify_single_logged_string_metric(
            stream.try_next().await.unwrap(),
            3,
            UNKNOWN_SOURCE_FILE_PATH.to_string(),
            expected_log_identifier_and_component.clone(),
        )
        .await;
    }

    /// Log in scope is generated during idle state:
    /// Current device state: Idle
    /// Current device state change time <= Log metadata timestamp
    #[fasync::run_singlethreaded(test)]
    async fn test_log_process_for_idle_current_device_state() {
        let specs = TEST_METRIC_SPECS;
        let mut component_map = ComponentEventCodeMap::new();
        component_map.insert(TEST_URL.to_string(), 1);
        let fake_metric_event_provider = FakeMetricEventProvider::new();
        let mut metric_logger = MetricLogger::init_for_testing(
            specs,
            component_map,
            fake_metric_event_provider.metric_event_logger_proxy,
        );
        let current_device_state =
            Some((factivity::State::Idle, zx::Time::from_nanos(2).into_nanos()));
        let previous_device_state =
            Some((factivity::State::Active, zx::Time::from_nanos(1).into_nanos()));
        let data = get_sample_logs_data_with_severity(Severity::Info);
        let expected_log_identifier_and_component = LogIdentifierAndComponent {
            log_identifier: LogIdentifier {
                file_path: "path/to/file.cc".to_string(),
                line_no: 123u64,
            },
            component_event_code: 1,
        };
        let mut stream = fake_metric_event_provider.metric_event_logger_stream;

        fasync::Task::spawn(async move {
            let _ = metric_logger.process(&data, previous_device_state, current_device_state).await;
        })
        .detach();

        verify_single_logged_string_metric(
            stream.try_next().await.unwrap(),
            5,
            "path/to/file.cc".to_string(),
            expected_log_identifier_and_component.clone(),
        )
        .await;
    }

    /// Log in scope is generated during idle state:
    /// Current device state: Active
    /// Previous device state: Idle
    /// Previous device state change time <= Log metadata timestamp
    /// Current device state change time > Log metadata timestamp
    #[fasync::run_singlethreaded(test)]
    async fn test_log_process_for_idle_previous_device_state() {
        let specs = TEST_METRIC_SPECS;
        let mut component_map = ComponentEventCodeMap::new();
        component_map.insert(TEST_URL.to_string(), 1);
        let fake_metric_event_provider = FakeMetricEventProvider::new();
        let mut metric_logger = MetricLogger::init_for_testing(
            specs,
            component_map,
            fake_metric_event_provider.metric_event_logger_proxy,
        );
        let current_device_state =
            Some((factivity::State::Active, zx::Time::from_nanos(3).into_nanos()));
        let previous_device_state =
            Some((factivity::State::Idle, zx::Time::from_nanos(1).into_nanos()));
        let data = get_sample_logs_data_with_severity(Severity::Info);
        let expected_log_identifier_and_component = LogIdentifierAndComponent {
            log_identifier: LogIdentifier {
                file_path: "path/to/file.cc".to_string(),
                line_no: 123u64,
            },
            component_event_code: 1,
        };
        let mut stream = fake_metric_event_provider.metric_event_logger_stream;

        fasync::Task::spawn(async move {
            let _ = metric_logger.process(&data, previous_device_state, current_device_state).await;
        })
        .detach();

        verify_single_logged_string_metric(
            stream.try_next().await.unwrap(),
            5,
            "path/to/file.cc".to_string(),
            expected_log_identifier_and_component.clone(),
        )
        .await;
    }

    /// Log in scope is generated during active state:
    /// Current device state: Active
    #[fasync::run_singlethreaded(test)]
    async fn test_log_process_for_active_current_device_state() {
        let specs = TEST_METRIC_SPECS;
        let mut component_map = ComponentEventCodeMap::new();
        component_map.insert(TEST_URL.to_string(), 1);
        let fake_metric_event_provider = FakeMetricEventProvider::new();
        let mut metric_logger = MetricLogger::init_for_testing(
            specs,
            component_map,
            fake_metric_event_provider.metric_event_logger_proxy,
        );
        let current_device_state =
            Some((factivity::State::Active, zx::Time::from_nanos(1).into_nanos()));
        let previous_device_state = None;
        let data = get_sample_logs_data_with_severity(Severity::Info);
        let mut stream = fake_metric_event_provider.metric_event_logger_stream;

        fasync::Task::spawn(async move {
            let _ = metric_logger.process(&data, previous_device_state, current_device_state).await;
        })
        .detach();

        assert!(stream.next().await.is_none());
    }

    async fn verify_single_logged_string_metric(
        metric_event: Option<MetricEventLoggerRequest>,
        expected_metric_id: u32,
        expected_string_value: String,
        expected_log_identifier_and_component: LogIdentifierAndComponent,
    ) {
        if let Some(MetricEventLoggerRequest::LogString {
            metric_id,
            string_value,
            event_codes,
            responder,
            ..
        }) = metric_event
        {
            assert_eq!(metric_id, expected_metric_id);
            assert_eq!(string_value, expected_string_value);
            assert_eq!(
                event_codes,
                &[
                    expected_log_identifier_and_component.log_identifier.line_no as u32,
                    expected_log_identifier_and_component.component_event_code
                ]
            );
            let _ = responder.send(&mut Ok(()));
        } else {
            assert!(false);
        }
    }

    struct FakeMetricEventProvider {
        metric_event_logger_stream: MetricEventLoggerRequestStream,
        metric_event_logger_proxy: MetricEventLoggerProxy,
    }

    impl FakeMetricEventProvider {
        fn new() -> Self {
            let (metric_event_logger_proxy, metric_event_logger_stream) =
                fidl::endpoints::create_proxy_and_stream::<MetricEventLoggerMarker>().unwrap();
            Self { metric_event_logger_stream, metric_event_logger_proxy }
        }
    }

    fn get_sample_logs_data_with_severity(severity: Severity) -> LogsData {
        LogsDataBuilder::new(BuilderArgs {
            timestamp_nanos: zx::Time::from_nanos(2).into(),
            component_url: Some(TEST_URL.to_string()),
            moniker: TEST_MONIKER.to_string(),
            severity,
        })
        .set_message("[irrelevant_tag(32)] Hello".to_string())
        .set_line(123u64)
        .set_file("path/to/file.cc".to_string())
        .build()
    }

    fn get_ping_log_identifier_and_component() -> LogIdentifierAndComponent {
        LogIdentifierAndComponent {
            log_identifier: LogIdentifier {
                file_path: PING_FILE_PATH.to_string(),
                line_no: EMPTY_LINE_NUMBER,
            },
            component_event_code: OTHER_EVENT_CODE,
        }
    }
}
