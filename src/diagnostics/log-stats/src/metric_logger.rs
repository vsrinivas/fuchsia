// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::create_proxy,
    fidl_fuchsia_metrics::{
        MetricEventLoggerFactoryMarker, MetricEventLoggerProxy, ProjectSpec, Status,
    },
    fuchsia_component::client::connect_to_service,
    serde::Deserialize,
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
}

impl MetricLogger {
    /// Create a MetricLogger that logs the given MetricSpecs.
    pub async fn new(specs: MetricSpecs) -> Result<MetricLogger, anyhow::Error> {
        let metric_logger_factory = connect_to_service::<MetricEventLoggerFactoryMarker>()?;
        let mut project_spec = ProjectSpec::EMPTY;
        project_spec.customer_id = Some(specs.customer_id);
        project_spec.project_id = Some(specs.project_id);

        let (proxy, request) = create_proxy().unwrap();

        metric_logger_factory.create_metric_event_logger(project_spec, request).await?;

        Ok(Self { specs, proxy })
    }

    /// Logs that an ERROR originating from |file_path| at |line_no| was observed.
    pub fn log(
        self: &Self,
        file_path: &str,
        line_no: u64,
    ) -> fidl::client::QueryResponseFut<Status> {
        self.proxy.log_string(self.specs.metric_id, file_path, &[line_no as u32])
    }
}
