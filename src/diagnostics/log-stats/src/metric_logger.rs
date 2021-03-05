// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::stats::LogIdentifier,
    archivist_lib::logs::message::Severity,
    diagnostics_data::LogsData,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_metrics::{
        MetricEventLoggerFactoryMarker, MetricEventLoggerProxy, ProjectSpec, Status,
    },
    fuchsia_component::client::connect_to_service,
    serde::Deserialize,
    std::collections::HashMap,
    std::convert::TryFrom,
};

#[derive(Deserialize)]
pub struct MetricSpecs {
    customer_id: u32,
    project_id: u32,
    metric_id: u32,
}

pub struct MetricLogger {
    specs: MetricSpecs,
    proxy: MetricEventLoggerProxy,
    component_map: ComponentEventCodeMap,
}

type ComponentEventCodeMap = HashMap<String, u32>;

/// The event code that is used if there is no corresponding event code for the component URL.
pub const OTHER_EVENT_CODE: u32 = 1_000_000;

impl MetricLogger {
    /// Create a MetricLogger that logs the given MetricSpecs.
    pub async fn new(
        specs: MetricSpecs,
        component_map: ComponentEventCodeMap,
    ) -> Result<MetricLogger, anyhow::Error> {
        let metric_logger_factory = connect_to_service::<MetricEventLoggerFactoryMarker>()?;
        let mut project_spec = ProjectSpec::EMPTY;
        project_spec.customer_id = Some(specs.customer_id);
        project_spec.project_id = Some(specs.project_id);

        let (proxy, request) = create_proxy().unwrap();

        metric_logger_factory.create_metric_event_logger(project_spec, request).await?;

        Ok(Self { specs, proxy, component_map })
    }

    /// Processes one line of log. Logs the metric if the severity is ERROR or FATAL and the file
    /// path and line number of the location that the log originated from is known.
    pub async fn process(self: &Self, log: &LogsData) -> Result<(), anyhow::Error> {
        if log.metadata.severity != Severity::Error && log.metadata.severity != Severity::Fatal {
            return Ok(());
        }
        if let Ok(log_identifier) = LogIdentifier::try_from(log) {
            let status = self
                .log(&log.metadata.component_url, &log_identifier.file_path, log_identifier.line_no)
                .await?;
            match status {
                Status::Ok => Ok(()),
                _ => Err(anyhow::format_err!("Cobalt returned error: {}", status as u8)),
            }
        } else {
            Ok(())
        }
    }

    /// Logs that an ERROR originating from |file_path| at |line_no| was observed.
    fn log(
        self: &Self,
        component_url: &str,
        file_path: &str,
        line_no: u64,
    ) -> fidl::client::QueryResponseFut<Status> {
        let event_code = self.component_map.get(component_url).unwrap_or(&OTHER_EVENT_CODE);
        self.proxy.log_string(self.specs.metric_id, file_path, &[line_no as u32, *event_code])
    }
}
