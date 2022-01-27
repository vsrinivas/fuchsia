// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Error, Result},
    fidl_fuchsia_kernel::{self as fkernel},
    fidl_fuchsia_systemmetrics_test::{self as fsysmetrics, SystemMetricsLoggerRequest},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_zircon as zx,
    futures::{
        stream::{StreamExt, TryStreamExt},
        TryFutureExt,
    },
    std::{cell::RefCell, rc::Rc, task::Poll},
};

struct SystemMetricsLoggerServer {
    cpu_logging_task: RefCell<Option<fasync::Task<()>>>,

    // Optional proxy for testing
    stats_proxy: Option<fkernel::StatsProxy>,
}

impl SystemMetricsLoggerServer {
    fn new() -> Self {
        Self { cpu_logging_task: RefCell::new(None), stats_proxy: None }
    }

    #[cfg(test)]
    fn with_proxy(proxy: fkernel::StatsProxy) -> Self {
        Self { cpu_logging_task: RefCell::new(None), stats_proxy: Some(proxy) }
    }

    fn handle_new_service_connection(
        self: Rc<Self>,
        mut stream: fsysmetrics::SystemMetricsLoggerRequestStream,
    ) -> fasync::Task<()> {
        fasync::Task::local(
            async move {
                while let Some(request) = stream.try_next().await? {
                    self.handle_system_metrics_logger_request(request).await?;
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| fx_log_err!("{:?}", e)),
        )
    }

    async fn start_logging(
        &self,
        interval_ms: u32,
        duration_ms: Option<u32>,
    ) -> fsysmetrics::SystemMetricsLoggerStartLoggingResult {
        fx_log_info!(
            "Starting logging interval_ms {:?} duration_ms {:?}",
            interval_ms,
            duration_ms
        );

        // If self.cpu_logging_task is None, then the server has never logged. If the task exists
        // and is Pending then logging is already active, and an error is returned. If the task is
        // Ready, then a previous logging session has ended naturally, and we proceed to create a
        // new task.
        if let Some(task) = self.cpu_logging_task.borrow_mut().as_mut() {
            if let Poll::Pending = futures::poll!(task) {
                return Err(fsysmetrics::SystemMetricsLoggerError::AlreadyLogging);
            }
        }

        if interval_ms == 0 || duration_ms.map_or(false, |d| d <= interval_ms) {
            return Err(fsysmetrics::SystemMetricsLoggerError::InvalidArgument);
        }

        let cpu_logger = CpuUsageLogger::new(
            zx::Duration::from_millis(interval_ms as i64),
            duration_ms.map(|ms| zx::Duration::from_millis(ms as i64)),
            self.stats_proxy.clone(),
        );
        self.cpu_logging_task.borrow_mut().replace(cpu_logger.spawn_logging_task());

        Ok(())
    }

    async fn handle_system_metrics_logger_request(
        self: &Rc<Self>,
        request: SystemMetricsLoggerRequest,
    ) -> Result<()> {
        match request {
            SystemMetricsLoggerRequest::StartLogging { interval_ms, duration_ms, responder } => {
                let mut result = self.start_logging(interval_ms, Some(duration_ms)).await;
                responder.send(&mut result)?;
            }
            SystemMetricsLoggerRequest::StartLoggingForever { interval_ms, responder } => {
                let mut result = self.start_logging(interval_ms, None).await;
                responder.send(&mut result)?;
            }
            SystemMetricsLoggerRequest::StopLogging { responder } => {
                *self.cpu_logging_task.borrow_mut() = None;
                responder.send()?;
            }
        }

        Ok(())
    }
}

struct CpuUsageLogger {
    interval: zx::Duration,
    end_time: fasync::Time,
    last_sample: Option<(fasync::Time, fkernel::CpuStats)>,
    stats_proxy: Option<fkernel::StatsProxy>,
}

impl CpuUsageLogger {
    fn new(
        interval: zx::Duration,
        duration: Option<zx::Duration>,
        stats_proxy: Option<fkernel::StatsProxy>,
    ) -> Self {
        let end_time = duration.map_or(fasync::Time::INFINITE, |d| fasync::Time::now() + d);
        CpuUsageLogger { interval, end_time, last_sample: None, stats_proxy }
    }

    fn spawn_logging_task(mut self) -> fasync::Task<()> {
        let mut interval = fasync::Interval::new(self.interval);

        fasync::Task::local(async move {
            while let Some(()) = interval.next().await {
                let now = fasync::Time::now();
                if now >= self.end_time {
                    break;
                }
                self.log_cpu_usage(now).await;
            }
        })
    }

    async fn log_cpu_usage(&mut self, now: fasync::Time) {
        let kernel_stats = match &self.stats_proxy {
            Some(proxy) => proxy.clone(),
            None => match connect_to_protocol::<fkernel::StatsMarker>() {
                Ok(s) => s,
                Err(e) => {
                    fx_log_err!("Failed to connect to kernel_stats service: {}", e);
                    return;
                }
            },
        };
        match kernel_stats.get_cpu_stats().await {
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
                    fuchsia_trace::counter!(
                        "system_metrics_logger",
                        "cpu_usage",
                        0,
                        "cpu_usage" => cpu_percentage_sum / cpu_stats.actual_num_cpus as f64
                    );
                }

                self.last_sample.replace((now, cpu_stats));
            }
            Err(e) => fx_log_err!("get_cpu_stats IPC failed: {}", e),
        }
    }
}

#[fasync::run_singlethreaded]
async fn main() {
    // v2 components can't surface stderr yet, so we need to explicitly log errors.
    match inner_main().await {
        Err(e) => fx_log_err!("Terminated with error: {}", e),
        Ok(()) => fx_log_info!("Terminated with Ok(())"),
    }
}

async fn inner_main() -> Result<()> {
    fuchsia_syslog::init_with_tags(&["system-metrics-logger"])
        .expect("failed to initialize logger");

    fx_log_info!("Starting system metrics logger");

    // Set up tracing
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    let mut fs = ServiceFs::new_local();

    // Allow our services to be discovered.
    fs.take_and_serve_directory_handle()?;

    // Construct the server, and begin serving.
    let server = Rc::new(SystemMetricsLoggerServer::new());
    fs.dir("svc").add_fidl_service(move |stream: fsysmetrics::SystemMetricsLoggerRequestStream| {
        SystemMetricsLoggerServer::handle_new_service_connection(server.clone(), stream).detach();
    });

    // This future never completes.
    fs.collect::<()>().await;

    Ok(())
}

#[cfg(test)]
mod tests {
    #![allow(unused_imports, unused_mut, unused_variables, dead_code)]
    use {
        super::*,
        assert_matches::assert_matches,
        fidl_fuchsia_kernel::{CpuStats, PerCpuStats},
        std::cell::RefCell,
    };

    fn setup_fake_stats_service(
        mut get_cpu_stats: impl FnMut() -> CpuStats + 'static,
    ) -> (fkernel::StatsProxy, fasync::Task<()>) {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fkernel::StatsMarker>().unwrap();
        let task = fasync::Task::local(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fkernel::StatsRequest::GetCpuStats { responder }) => {
                        let _ = responder.send(&mut get_cpu_stats());
                    }
                    _ => assert!(false),
                }
            }
        });

        (proxy, task)
    }

    struct Runner {
        server_task: fasync::Task<()>,
        proxy: fsysmetrics::SystemMetricsLoggerProxy,

        cpu_stats: Rc<RefCell<CpuStats>>,

        _stats_task: fasync::Task<()>,

        // Fields are dropped in declaration order. Always drop executor last because we hold other
        // zircon objects tied to the executor in this struct, and those can't outlive the executor.
        //
        // See
        // - https://fuchsia-docs.firebaseapp.com/rust/fuchsia_async/struct.TestExecutor.html
        // - https://doc.rust-lang.org/reference/destructors.html.
        executor: fasync::TestExecutor,
    }

    impl Runner {
        fn new() -> Self {
            let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();
            executor.set_fake_time(fasync::Time::from_nanos(0));

            let cpu_stats = Rc::new(RefCell::new(CpuStats {
                actual_num_cpus: 1,
                per_cpu_stats: Some(vec![PerCpuStats { idle_time: Some(0), ..PerCpuStats::EMPTY }]),
            }));
            let cpu_stats_clone = cpu_stats.clone();
            let (stats_proxy, stats_task) =
                setup_fake_stats_service(move || cpu_stats_clone.borrow().clone());

            let server = Rc::new(SystemMetricsLoggerServer::with_proxy(stats_proxy));
            let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<
                fsysmetrics::SystemMetricsLoggerMarker,
            >()
            .unwrap();
            let server_task = server.handle_new_service_connection(stream);

            Self { executor, server_task, proxy, cpu_stats, _stats_task: stats_task }
        }

        // If the server has an active logging task, run until the next log and return true.
        // Otherwise, return false.
        fn iterate_logging_task(&mut self) -> bool {
            let wakeup_time = match self.executor.wake_next_timer() {
                Some(t) => t,
                None => return false,
            };
            self.executor.set_fake_time(wakeup_time);
            assert_eq!(
                futures::task::Poll::Pending,
                self.executor.run_until_stalled(&mut self.server_task)
            );
            true
        }
    }

    #[test]
    fn test_logging_duration() {
        let mut runner = Runner::new();

        // Start logging every 100ms for a total of 2000ms.
        let mut query = runner.proxy.start_logging(100, 2000);
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));

        // Ensure that we get exactly 20 samples.
        for _ in 0..20 {
            assert_eq!(runner.iterate_logging_task(), true);
        }
        assert_eq!(runner.iterate_logging_task(), false);
    }

    #[test]
    fn test_logging_duration_too_short() {
        let mut runner = Runner::new();

        // Attempt to start logging with an interval of 100ms but a duration of 50ms. The request
        // should fail as the logging session would not produce any samples.
        let mut query = runner.proxy.start_logging(100, 50);
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fsysmetrics::SystemMetricsLoggerError::InvalidArgument)))
        );
    }

    #[test]
    fn test_logging_forever() {
        let mut runner = Runner::new();

        // Start logging every 100ms with no predetermined end time.
        let mut query = runner.proxy.start_logging_forever(100);
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));

        // Samples should continue forever. Obviously we can't check infinitely many samples, but
        // we can check that they don't stop for a relatively large number of iterations.
        for _ in 0..1000 {
            assert_eq!(runner.iterate_logging_task(), true);
        }

        let mut query = runner.proxy.stop_logging();
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(())));
        let mut query = runner.proxy.start_logging_forever(100);
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));
    }

    #[test]
    fn test_already_logging() {
        let mut runner = Runner::new();

        // Start the first logging task.
        let mut query = runner.proxy.start_logging(100, 200);
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));

        assert_eq!(runner.iterate_logging_task(), true);

        // Attempt to start another logging task while the first one is still running. The request
        // to start should fail.
        let mut query = runner.proxy.start_logging(100, 200);
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fsysmetrics::SystemMetricsLoggerError::AlreadyLogging)))
        );

        // Run the first logging task to completion (2 total samples).
        assert_eq!(runner.iterate_logging_task(), true);
        assert_eq!(runner.iterate_logging_task(), false);

        // Starting a new logging task should succeed now.
        let mut query = runner.proxy.start_logging(100, 200);
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));
    }

    #[test]
    fn test_invalid_argument() {
        let mut runner = Runner::new();

        let mut query = runner.proxy.start_logging(0, 200);
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fsysmetrics::SystemMetricsLoggerError::InvalidArgument)))
        );
    }

    #[test]
    fn test_multiple_stops_ok() {
        let mut runner = Runner::new();

        let mut query = runner.proxy.stop_logging();
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(())));

        let mut query = runner.proxy.start_logging(100, 200);
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));

        let mut query = runner.proxy.stop_logging();
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(())));
        let mut query = runner.proxy.stop_logging();
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(())));
    }
}
