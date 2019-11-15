// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::types::Nanoseconds;
use async_trait::async_trait;
use failure::{format_err, Error};
use fidl_fuchsia_kernel as fstats;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use fuchsia_zircon as zx;
use std::cell::RefCell;
use std::rc::Rc;

/// Node: CpuStatsHandler
///
/// Summary: Provides CPU statistic information by interfacing with the Kernel Stats service. That
///          information includes the number of CPU cores and CPU load information.
///
/// Handles Messages:
///     - GetNumCpus
///     - GetTotalCpuLoad
///
/// Sends Messages: N/A
///
/// FIDL dependencies:
///     - fidl_fuchsia_kernel: used by this node to query the Kernel Stats service.

/// The Kernel Stats service that we'll be communicating with
const CPU_STATS_SVC: &'static str = "/svc/fuchsia.kernel.Stats";

/// The CpuStatsHandler node
pub struct CpuStatsHandler {
    /// A proxy handle to communicate with the Kernel Stats service
    stats_svc: fstats::StatsProxy,

    /// Cached CPU idle states from the most recent call
    cpu_idle_stats: RefCell<CpuIdleStats>,
}

/// A record to store the total time spent idle for each CPU in the system at a moment in time
#[derive(Default)]
struct CpuIdleStats {
    /// Time the record was taken
    timestamp: Nanoseconds,

    /// Vector containing the total time since boot that each CPU has spent has spent idle. The
    /// length of the vector is equal to the number of CPUs in the system at the time of the record.
    idle_times: Vec<Nanoseconds>,
}

impl CpuStatsHandler {
    pub fn new() -> Result<Rc<Self>, Error> {
        Ok(Rc::new(Self::new_with_svc_handle(Self::connect_stats_service()?)))
    }

    /// Create the node with an existing Kernel Stats proxy (test configuration can use this
    /// to pass a proxy which connects to a fake stats service)
    fn new_with_svc_handle(stats_svc: fstats::StatsProxy) -> Self {
        Self { stats_svc, cpu_idle_stats: RefCell::new(Default::default()) }
    }

    fn connect_stats_service() -> Result<fstats::StatsProxy, Error> {
        let (client, server) =
            zx::Channel::create().map_err(|s| format_err!("Failed to create channel: {}", s))?;

        fdio::service_connect(CPU_STATS_SVC, server)
            .map_err(|s| format_err!("Failed to connect to Stats service: {}", s))?;
        Ok(fstats::StatsProxy::new(fasync::Channel::from_channel(client)?))
    }

    /// Calls out to the Kernel Stats service to retrieve the latest CPU stats
    async fn get_cpu_stats(&self) -> Result<fstats::CpuStats, Error> {
        let stats = self
            .stats_svc
            .get_cpu_stats()
            .await
            .map_err(|e| format_err!("get_cpu_stats IPC failed: {}", e))?;
        Ok(stats)
    }

    async fn handle_get_num_cpus(&self) -> Result<MessageReturn, Error> {
        let stats = self.get_cpu_stats().await?;
        Ok(MessageReturn::GetNumCpus(stats.actual_num_cpus as u32))
    }

    async fn handle_get_total_cpu_load(&self) -> Result<MessageReturn, Error> {
        let new_stats = self.get_idle_stats().await?;

        // If the cached idle stats' idle_times vector has a length of 0, it means we have never
        // cached the idle stats before. In this case, we should just return a value of 0.0.
        let load = if self.cpu_idle_stats.borrow().idle_times.len() == 0 {
            0.0
        } else {
            Self::calculate_total_cpu_load(&self.cpu_idle_stats.borrow(), &new_stats)
        };
        self.cpu_idle_stats.replace(new_stats);
        Ok(MessageReturn::GetTotalCpuLoad(load))
    }

    /// Gets the CPU idle stats, then populates them into the CpuIdleStats struct format that we
    /// can more easily use for calculations.
    async fn get_idle_stats(&self) -> Result<CpuIdleStats, Error> {
        let mut idle_stats: CpuIdleStats = Default::default();
        let cpu_stats = self.get_cpu_stats().await?;
        let per_cpu_stats =
            cpu_stats.per_cpu_stats.ok_or(format_err!("Received null per_cpu_stats"))?;

        idle_stats.timestamp = Nanoseconds(fasync::Time::now().into_nanos());
        for i in 0..cpu_stats.actual_num_cpus as usize {
            idle_stats.idle_times.push(Nanoseconds(
                per_cpu_stats[i]
                    .idle_time
                    .ok_or(format_err!("Received null idle_time for CPU {}", i))?,
            ));
        }

        Ok(idle_stats)
    }

    /// Calculates the sum of the load of all CPUs in the system. Per-CPU load is measured as
    /// a value from 0.0 to 1.0. Therefore the total load is a value from 0.0 to [num_cpus].
    ///     old_idle: the starting idle stats
    ///     new_idle: the ending idle stats
    fn calculate_total_cpu_load(old_idle: &CpuIdleStats, new_idle: &CpuIdleStats) -> f32 {
        if old_idle.idle_times.len() != new_idle.idle_times.len() {
            fx_log_err!(
                "Number of CPUs changed (old={}; new={})",
                old_idle.idle_times.len(),
                new_idle.idle_times.len()
            );
            return 0.0;
        }

        let mut total_load = 0.0;
        for i in 0..old_idle.idle_times.len() as usize {
            total_load += Self::calculate_cpu_load(i, old_idle, new_idle);
        }
        total_load
    }

    /// Calculates the CPU load for the nth CPU from two idle stats readings. Per-CPU load is
    /// measured as a value from 0.0 to 1.0.
    ///     cpu_num: the CPU number for which to calculate load. This indexes into the
    ///              old_idle and new_idle idle_times vector
    ///     old_idle: the starting idle stats
    ///     new_idle: the ending idle stats
    fn calculate_cpu_load(cpu_num: usize, old_idle: &CpuIdleStats, new_idle: &CpuIdleStats) -> f32 {
        let total_time_delta = new_idle.timestamp.0 - old_idle.timestamp.0;
        if total_time_delta <= 0 {
            fx_log_err!(
                "Expected positive total_time_delta, got: {} (start={}; end={})",
                total_time_delta,
                old_idle.timestamp.0,
                new_idle.timestamp.0
            );
            return 0.0;
        }

        let idle_time_delta = new_idle.idle_times[cpu_num].0 - old_idle.idle_times[cpu_num].0;
        let busy_time = total_time_delta - idle_time_delta;
        busy_time as f32 / total_time_delta as f32
    }
}

#[async_trait(?Send)]
impl Node for CpuStatsHandler {
    fn name(&self) -> &'static str {
        "CpuStatsHandler"
    }

    async fn handle_message(&self, msg: &Message<'_>) -> Result<MessageReturn, Error> {
        match msg {
            Message::GetNumCpus => self.handle_get_num_cpus().await,
            Message::GetTotalCpuLoad => self.handle_get_total_cpu_load().await,
            _ => Err(format_err!("Unsupported message: {:?}", msg)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::TryStreamExt;

    const TEST_NUM_CORES: u32 = 4;

    /// Generate some fake CPU stats to be used in the test configuration
    fn gen_cpu_stats() -> fstats::CpuStats {
        let mut per_cpu_stats = Vec::new();
        for i in 0..TEST_NUM_CORES {
            let cpu_stats = fstats::PerCpuStats {
                cpu_number: Some(i),
                flags: None,
                idle_time: Some(0),
                reschedules: None,
                context_switches: None,
                irq_preempts: None,
                yields: None,
                ints: None,
                timer_ints: None,
                timers: None,
                page_faults: None,
                exceptions: None,
                syscalls: None,
                reschedule_ipis: None,
                generic_ipis: None,
            };
            per_cpu_stats.push(cpu_stats);
        }

        fstats::CpuStats {
            actual_num_cpus: TEST_NUM_CORES as u64,
            per_cpu_stats: Some(per_cpu_stats),
        }
    }

    fn setup_fake_service() -> fstats::StatsProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fstats::StatsMarker>().unwrap();

        fasync::spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fstats::StatsRequest::GetCpuStats { responder }) => {
                        let _ = responder.send(&mut gen_cpu_stats());
                    }
                    _ => assert!(false),
                }
            }
        });

        proxy
    }

    fn setup_test_node() -> CpuStatsHandler {
        CpuStatsHandler::new_with_svc_handle(setup_fake_service())
    }

    /// This test creates a CpuStatsHandler node and sends it the 'GetNumCpus' message. The
    /// test verifies that the message returns successfully and the expected number of CPUs
    /// are reported (in the test configuration, it should report `TEST_NUM_CORES`).
    #[fasync::run_singlethreaded(test)]
    async fn test_get_num_cpus() {
        let node = setup_test_node();
        let num_cpus = node.handle_message(&Message::GetNumCpus).await.unwrap();
        if let MessageReturn::GetNumCpus(n) = num_cpus {
            assert_eq!(n, TEST_NUM_CORES);
        } else {
            assert!(false);
        }
    }

    /// Tests that the 'GetTotalCpuLoad' message behaves as expected. Specifically:
    ///     1) the first call should fail (because CPU load is calculated based on idle time
    ///        since the previous call)
    ///     2) the second call should succeed, and load should be reported as 1.0 * NUM_CORES in
    ///        the test configuration
    #[fasync::run_singlethreaded(test)]
    async fn test_handle_get_cpu_load() {
        let node = setup_test_node();

        if let MessageReturn::GetTotalCpuLoad(load) =
            node.handle_message(&Message::GetTotalCpuLoad).await.unwrap()
        {
            // The first call should return a load of 0.0
            assert_eq!(load, 0.0);
        } else {
            assert!(false);
        }

        if let MessageReturn::GetTotalCpuLoad(load) =
            node.handle_message(&Message::GetTotalCpuLoad).await.unwrap()
        {
            // In test configuration, CpuStats is set to always report "0" idle time, which
            // corresponds to 100% load. So total load should be 1.0 * TEST_NUM_CORES.
            assert_eq!(load, TEST_NUM_CORES as f32);
        } else {
            assert!(false);
        }
    }

    /// Tests the CPU load calculation function. Test values are used as input (representing
    /// the total time delta and idle times of four total CPUs), and the result is compared
    /// against expected output for these test values:
    ///     CPU 1: 20ns idle / 100ns total = 0.2 load
    ///     CPU 2: 40ns idle / 100ns total = 0.4 load
    ///     CPU 3: 60ns idle / 100ns total = 0.6 load
    ///     CPU 4: 80ns idle / 100ns total = 0.8 load
    ///     Total load: 2.0
    #[test]
    fn test_calculate_total_cpu_load() {
        let idle_sample_1 = CpuIdleStats {
            timestamp: Nanoseconds(0),
            idle_times: vec![Nanoseconds(0), Nanoseconds(0), Nanoseconds(0), Nanoseconds(0)],
        };

        let idle_sample_2 = CpuIdleStats {
            timestamp: Nanoseconds(100),
            idle_times: vec![Nanoseconds(20), Nanoseconds(40), Nanoseconds(60), Nanoseconds(80)],
        };

        let calculated_load =
            CpuStatsHandler::calculate_total_cpu_load(&idle_sample_1, &idle_sample_2);
        assert_eq!(calculated_load, 2.0)
    }

    /// Tests that an unsupported message is handled gracefully and an error is returned
    #[fasync::run_singlethreaded(test)]
    async fn test_unsupported_msg() {
        let node = setup_test_node();
        let message = Message::ReadTemperature("");
        let result = node.handle_message(&message).await;
        assert!(result.is_err());
    }
}
