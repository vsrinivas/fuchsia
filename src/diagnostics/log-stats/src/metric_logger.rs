// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::stats::LogIdentifier,
    diagnostics_data::{LogsData, Severity},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_metrics::{
        MetricEventLoggerFactoryMarker, MetricEventLoggerProxy, ProjectSpec, Status,
    },
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_syslog::fx_log_warn,
    serde::Deserialize,
    std::collections::{HashMap, HashSet},
    std::convert::TryFrom,
};

#[derive(Deserialize)]
pub struct MetricSpecs {
    customer_id: u32,
    project_id: u32,
    granular_error_count_metric_id: u32,
    granular_error_interval_count_metric_id: u32,
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

// Establishes a channel to Cobalt.
async fn connect_to_cobalt(specs: &MetricSpecs) -> Result<MetricEventLoggerProxy, anyhow::Error> {
    let mut project_spec = ProjectSpec::EMPTY;
    project_spec.customer_id = Some(specs.customer_id);
    project_spec.project_id = Some(specs.project_id);

    let metric_logger_factory = connect_to_protocol::<MetricEventLoggerFactoryMarker>()?;
    let (proxy, request) = create_proxy().unwrap();
    metric_logger_factory.create_metric_event_logger(project_spec, request).await?;
    Ok(proxy)
}

impl MetricLogger {
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
        })
    }

    /// Processes one line of log. Logs the metric if the severity is ERROR or FATAL and the file
    /// path and line number of the location that the log originated from is known.
    pub async fn process(self: &mut Self, log: &LogsData) -> Result<(), anyhow::Error> {
        // We can't do anything here if we don't know the component URL.
        if log.metadata.component_url.is_none() {
            return Ok(());
        }
        let url = log.metadata.component_url.as_ref().unwrap();
        self.maybe_clear_errors_and_send_ping().await?;
        if log.metadata.severity != Severity::Error && log.metadata.severity != Severity::Fatal {
            return Ok(());
        }
        let log_identifier = LogIdentifier::try_from(log).unwrap_or(LogIdentifier {
            file_path: UNKNOWN_SOURCE_FILE_PATH.to_string(),
            line_no: EMPTY_LINE_NUMBER,
        });
        let event_code = self.component_map.get(url).unwrap_or(&OTHER_EVENT_CODE);
        let identifier_and_component =
            LogIdentifierAndComponent { log_identifier, component_event_code: *event_code };
        self.log_metric(self.specs.granular_error_count_metric_id, &identifier_and_component)
            .await?;
        if self.current_interval_errors.len() >= MAX_ERRORS_PER_INTERVAL {
            // Only print this warning once per interval: the first time that we reached capacity.
            if !self.reached_capacity {
                fx_log_warn!("Received too many ERRORs. Will temporarily halt logging the metric.");
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
            Status::Ok => Ok(()),
            _ => Err(anyhow::format_err!("Cobalt returned error: {}", status as u8)),
        }
    }
}
