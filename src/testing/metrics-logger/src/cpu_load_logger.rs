// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_kernel as fkernel, fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_zircon as zx,
    futures::stream::StreamExt,
    std::{
        collections::hash_map::DefaultHasher,
        hash::{Hash, Hasher},
        rc::Rc,
    },
};

pub struct CpuLoadLogger {
    interval: zx::Duration,
    end_time: fasync::Time,
    last_sample: Option<(fasync::Time, fkernel::CpuStats)>,
    stats_proxy: Rc<fkernel::StatsProxy>,
    client_id: String,
    output_samples_to_syslog: bool,
}

impl CpuLoadLogger {
    pub fn new(
        interval: zx::Duration,
        duration: Option<zx::Duration>,
        stats_proxy: Rc<fkernel::StatsProxy>,
        client_id: String,
        output_samples_to_syslog: bool,
    ) -> Self {
        let end_time = duration.map_or(fasync::Time::INFINITE, |d| fasync::Time::now() + d);
        CpuLoadLogger {
            interval,
            end_time,
            last_sample: None,
            stats_proxy,
            client_id,
            output_samples_to_syslog,
        }
    }

    pub async fn log_cpu_usages(mut self) {
        let mut interval = fasync::Interval::new(self.interval);

        while let Some(()) = interval.next().await {
            let now = fasync::Time::now();
            if now >= self.end_time {
                break;
            }
            self.log_cpu_usage(now).await;
        }
    }

    // TODO (fxbug.dev/92320): Populate CPU Usageinfo into Inspect.
    async fn log_cpu_usage(&mut self, now: fasync::Time) {
        let mut hasher = DefaultHasher::new();
        self.client_id.hash(&mut hasher);
        let trace_counter_id = hasher.finish();

        match self.stats_proxy.get_cpu_stats().await {
            Ok(cpu_stats) => {
                if let Some((last_sample_time, last_cpu_stats)) = self.last_sample.take() {
                    let elapsed = now - last_sample_time;
                    let mut cpu_percentage_sum: f64 = 0.0;
                    for (i, per_cpu_stats) in
                        cpu_stats.per_cpu_stats.as_ref().unwrap().iter().enumerate()
                    {
                        let last_per_cpu_stats = &last_cpu_stats.per_cpu_stats.as_ref().unwrap()[i];
                        let delta_idle_time = zx::Duration::from_nanos(
                            per_cpu_stats.idle_time.unwrap()
                                - last_per_cpu_stats.idle_time.unwrap(),
                        );
                        let busy_time = elapsed - delta_idle_time;
                        cpu_percentage_sum +=
                            100.0 * busy_time.into_nanos() as f64 / elapsed.into_nanos() as f64;
                    }

                    let cpu_usage = cpu_percentage_sum / cpu_stats.actual_num_cpus as f64;

                    if self.output_samples_to_syslog {
                        fx_log_info!("CpuUsage: {:?}", cpu_usage);
                    }

                    // TODO (didis): Remove system_metrics_logger category after the e2e test is
                    // transitioned.
                    fuchsia_trace::counter!(
                        "system_metrics_logger",
                        "cpu_usage",
                        0,
                        "cpu_usage" => cpu_usage
                    );
                    fuchsia_trace::counter!(
                        "metrics_logger",
                        "cpu_usage",
                        trace_counter_id,
                        "client_id" => self.client_id.as_str(),
                        "cpu_usage" => cpu_usage
                    );
                }

                self.last_sample.replace((now, cpu_stats));
            }
            Err(e) => fx_log_err!("get_cpu_stats IPC failed: {}", e),
        }
    }
}
