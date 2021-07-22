// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::PowerManagerError;
use crate::log_if_err;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::types::{Milliseconds, Nanoseconds};
use crate::utils::get_current_timestamp;
use anyhow::{format_err, Error};
use async_trait::async_trait;
use fidl_fuchsia_kernel as fstats;
use fuchsia_inspect::{self as inspect};
use fuchsia_inspect_contrib::{inspect_log, nodes::BoundedListNode};
use serde_derive::Deserialize;
use serde_json as json;
use std::cell::RefCell;
use std::collections::HashMap;
use std::rc::Rc;

/// Node: CpuStatsHandler
///
/// Summary: Provides CPU statistic information by interfacing with the Kernel Stats service. That
///          information includes the number of CPU cores and CPU load information.
///
/// Handles Messages:
///     - GetNumCpus
///     - GetCpuLoads
///
/// Sends Messages: N/A
///
/// FIDL dependencies:
///     - fuchsia.kernel.Stats: the node connects to this service to query kernel information

/// A builder for constructing the CpuStatsHandler node
#[derive(Default)]
pub struct CpuStatsHandlerBuilder<'a> {
    stats_svc_proxy: Option<fstats::StatsProxy>,
    inspect_root: Option<&'a inspect::Node>,
    cpu_load_cache_duration: Option<Milliseconds>,
}

impl<'a> CpuStatsHandlerBuilder<'a> {
    #[cfg(test)]
    fn new() -> Self {
        Self::default()
    }

    pub fn new_from_json(json_data: json::Value, _nodes: &HashMap<String, Rc<dyn Node>>) -> Self {
        #[derive(Deserialize)]
        struct Config {
            cpu_load_cache_duration_ms: Option<u32>,
        }

        #[derive(Deserialize)]
        struct JsonData {
            config: Option<Config>,
        }

        let data: JsonData = json::from_value(json_data).unwrap();
        Self {
            stats_svc_proxy: None,
            inspect_root: None,
            cpu_load_cache_duration: data
                .config
                .map(|config| config.cpu_load_cache_duration_ms)
                .flatten()
                .map(|limit| Milliseconds(limit.into())),
        }
    }

    #[cfg(test)]
    pub fn with_proxy(mut self, proxy: fstats::StatsProxy) -> Self {
        self.stats_svc_proxy = Some(proxy);
        self
    }

    #[cfg(test)]
    pub fn with_inspect_root(mut self, root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(root);
        self
    }

    #[cfg(test)]
    pub fn with_cpu_load_cache_duration(mut self, cpu_load_cache_duration: Milliseconds) -> Self {
        self.cpu_load_cache_duration = Some(cpu_load_cache_duration);
        self
    }

    pub async fn build(self) -> Result<Rc<CpuStatsHandler>, Error> {
        // Optionally use the default proxy
        let proxy = if self.stats_svc_proxy.is_none() {
            fuchsia_component::client::connect_to_protocol::<fstats::StatsMarker>()?
        } else {
            self.stats_svc_proxy.unwrap()
        };

        // Optionally use the default inspect root node
        let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());

        let node = Rc::new(CpuStatsHandler {
            stats_svc_proxy: proxy,
            cpu_stats: RefCell::new(Default::default()),
            inspect: InspectData::new(inspect_root, "CpuStatsHandler".to_string()),
            cpu_load_cache_duration: self.cpu_load_cache_duration.unwrap_or(Milliseconds(0)),
        });

        // Seed the idle stats
        node.cpu_stats.replace(node.get_idle_stats().await?);

        Ok(node)
    }
}

/// The CpuStatsHandler node
pub struct CpuStatsHandler {
    /// A proxy handle to communicate with the Kernel Stats service.
    stats_svc_proxy: fstats::StatsProxy,

    /// Cached CPU stats from the most recent GetCpuLoads request.
    cpu_stats: RefCell<CpuStats>,

    /// Cache duration for updating CPU load. If a GetCpuLoads request is received within this
    /// period of time from the previous request, the previous CPU load values are returned instead
    /// of refreshing the data.
    cpu_load_cache_duration: Milliseconds,

    /// A struct for managing Component Inspection data.
    inspect: InspectData,
}

/// A record to store the total time spent idle for each CPU in the system at a moment in time and
/// the calculated CPU load derived from those idle stats.
#[derive(Default, Debug)]
struct CpuStats {
    /// Time the record was taken
    timestamp: Nanoseconds,

    /// Vector containing the total time since boot that each CPU has spent has spent idle. The
    /// length of the vector is equal to the number of CPUs in the system at the time of the record.
    idle_times: Vec<Nanoseconds>,

    /// CPU load calculated using deltas from this and the previous `CpuStats` record.
    calculated_cpu_loads: Vec<f32>,
}

impl CpuStatsHandler {
    /// Calls out to the Kernel Stats service to retrieve the latest CPU stats
    async fn get_cpu_stats(&self) -> Result<fstats::CpuStats, Error> {
        fuchsia_trace::duration!("power_manager", "CpuStatsHandler::get_cpu_stats");
        let result = self
            .stats_svc_proxy
            .get_cpu_stats()
            .await
            .map_err(|e| format_err!("get_cpu_stats IPC failed: {}", e));

        log_if_err!(result, "Failed to get CPU stats");
        fuchsia_trace::instant!(
            "power_manager",
            "CpuStatsHandler::get_cpu_stats_result",
            fuchsia_trace::Scope::Thread,
            "result" => format!("{:?}", result).as_str()
        );

        Ok(result?)
    }

    async fn handle_get_num_cpus(&self) -> Result<MessageReturn, PowerManagerError> {
        fuchsia_trace::duration!("power_manager", "CpuStatsHandler::handle_get_num_cpus");
        let stats = self.get_cpu_stats().await?;
        Ok(MessageReturn::GetNumCpus(stats.actual_num_cpus as u32))
    }

    async fn handle_get_cpu_loads(&self) -> Result<MessageReturn, PowerManagerError> {
        fuchsia_trace::duration!("power_manager", "CpuStatsHandler::handle_get_cpu_loads");

        if self.is_cpu_load_stale() {
            self.update_cpu_stats().await?;
        }

        Ok(MessageReturn::GetCpuLoads(self.cpu_stats.borrow().calculated_cpu_loads.clone()))
    }

    /// Determines if the cached CPU load stats are stale. The data is considered to be "stale" if:
    ///     - The CPU loads have not yet been calculated
    ///     - The time since the previous CPU load calculation exceeds
    ///       `self.cpu_load_cache_duration`
    fn is_cpu_load_stale(&self) -> bool {
        let cpu_stats = self.cpu_stats.borrow();

        cpu_stats.calculated_cpu_loads.len() == 0
            || Milliseconds::from(get_current_timestamp() - cpu_stats.timestamp)
                > self.cpu_load_cache_duration
    }

    /// Gets the idle CPU stats from the server and returns a new CpuStats instance without
    /// populating the `calculated_cpu_loads` field.
    async fn get_idle_stats(&self) -> Result<CpuStats, Error> {
        Ok(CpuStats {
            timestamp: get_current_timestamp(),
            idle_times: self
                .get_cpu_stats()
                .await?
                .per_cpu_stats
                .ok_or(format_err!("Received null per_cpu_stats"))?
                .iter()
                .enumerate()
                .map(|(i, per_cpu_stats)| match per_cpu_stats.idle_time {
                    Some(idle_time) => Ok(Nanoseconds(idle_time)),
                    None => Err(format_err!("Received null idle_time for CPU {}", i)),
                })
                .collect::<Result<Vec<Nanoseconds>, Error>>()?,
            calculated_cpu_loads: vec![],
        })
    }

    /// Updates the `cpu_stats` state by first requesting updated CPU stats from the server, then
    /// calculating refreshed CPU load values based on the new stats.
    async fn update_cpu_stats(&self) -> Result<(), Error> {
        fuchsia_trace::duration!("power_manager", "CpuStatsHandler::update_cpu_stats");

        let mut new_stats = self.get_idle_stats().await?;
        let cpu_loads = Self::calculate_cpu_loads(&self.cpu_stats.borrow(), &new_stats)?;
        new_stats.calculated_cpu_loads = cpu_loads.clone();

        // Log the total load to Inspect / tracing
        let total_load: f32 = cpu_loads.iter().sum();
        self.inspect.log_total_cpu_load(total_load as f64);
        fuchsia_trace::instant!(
            "power_manager",
            "CpuStatsHandler::total_cpu_load",
            fuchsia_trace::Scope::Thread,
            "load" => total_load as f64
        );

        self.cpu_stats.replace(new_stats);
        Ok(())
    }

    /// Calculates the load of all CPUs in the system. Per-CPU load is measured as a value from 0.0
    /// to 1.0.
    ///     old_idle: the starting idle stats
    ///     new_idle: the ending idle stats
    fn calculate_cpu_loads(old_stats: &CpuStats, new_stats: &CpuStats) -> Result<Vec<f32>, Error> {
        if old_stats.idle_times.len() != new_stats.idle_times.len() {
            return Err(format_err!(
                "Number of CPUs changed (old={}; new={})",
                old_stats.idle_times.len(),
                new_stats.idle_times.len()
            ));
        }

        let total_time_delta = new_stats.timestamp - old_stats.timestamp;
        if total_time_delta.0 <= 0 {
            return Err(format_err!(
                "Expected positive total_time_delta, got: {:?} (start={:?}; end={:?})",
                total_time_delta,
                old_stats.timestamp,
                new_stats.timestamp
            ));
        }

        Ok(old_stats
            .idle_times
            .iter()
            .zip(new_stats.idle_times.iter())
            .map(|(old_idle_time, new_idle_time)| {
                let busy_time = total_time_delta.0 - (new_idle_time.0 - old_idle_time.0);
                busy_time as f32 / total_time_delta.0 as f32
            })
            .collect())
    }
}

const NUM_INSPECT_LOAD_SAMPLES: usize = 10;

struct InspectData {
    cpu_loads: RefCell<BoundedListNode>,
}

impl InspectData {
    fn new(parent: &inspect::Node, name: String) -> Self {
        // Create a local root node and properties
        let root = parent.create_child(name);
        let cpu_loads = RefCell::new(BoundedListNode::new(
            root.create_child("cpu_loads"),
            NUM_INSPECT_LOAD_SAMPLES,
        ));

        // Pass ownership of the new node to the parent node, otherwise it'll be dropped
        parent.record(root);

        InspectData { cpu_loads }
    }

    fn log_total_cpu_load(&self, load: f64) {
        inspect_log!(self.cpu_loads.borrow_mut(), load: load);
    }
}

#[async_trait(?Send)]
impl Node for CpuStatsHandler {
    fn name(&self) -> String {
        "CpuStatsHandler".to_string()
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, PowerManagerError> {
        match msg {
            Message::GetNumCpus => self.handle_get_num_cpus().await,
            Message::GetCpuLoads => self.handle_get_cpu_loads().await,
            _ => Err(PowerManagerError::Unsupported),
        }
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use crate::types::Seconds;
    use async_utils::PollExt;
    use fuchsia_async as fasync;
    use fuchsia_zircon::DurationNum;
    use futures::TryStreamExt;
    use inspect::assert_data_tree;
    use std::collections::VecDeque;
    use std::ops::Add;

    const TEST_NUM_CORES: u32 = 4;

    /// Generates CpuStats for an input vector of idle times, using the length of the idle times
    /// vector to determine the number of CPUs.
    fn idle_times_to_cpu_stats(idle_times: &Vec<Nanoseconds>) -> fstats::CpuStats {
        let mut per_cpu_stats = Vec::new();
        for (i, idle_time) in idle_times.iter().enumerate() {
            per_cpu_stats.push(fstats::PerCpuStats {
                cpu_number: Some(i as u32),
                flags: None,
                idle_time: Some(idle_time.0),
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
                ..fstats::PerCpuStats::EMPTY
            });
        }

        fstats::CpuStats {
            actual_num_cpus: idle_times.len() as u64,
            per_cpu_stats: Some(per_cpu_stats),
        }
    }

    fn setup_fake_service(
        mut get_idle_times: impl FnMut() -> Vec<Nanoseconds> + 'static,
    ) -> fstats::StatsProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fstats::StatsMarker>().unwrap();

        fasync::Task::local(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fstats::StatsRequest::GetCpuStats { responder }) => {
                        let mut cpu_stats = idle_times_to_cpu_stats(&get_idle_times());
                        let _ = responder.send(&mut cpu_stats);
                    }
                    _ => assert!(false),
                }
            }
        })
        .detach();

        proxy
    }

    /// Creates a test CpuStatsHandler node, with the provided closure giving per-CPU idle times
    /// that will be reported in CpuStats. The number of CPUs is implied by the length of the
    /// closure's returned Vec.
    pub async fn setup_test_node(
        get_idle_times: impl FnMut() -> Vec<Nanoseconds> + 'static,
    ) -> Rc<CpuStatsHandler> {
        CpuStatsHandlerBuilder::new()
            .with_proxy(setup_fake_service(get_idle_times))
            .build()
            .await
            .unwrap()
    }

    /// Creates a test CpuStatsHandler that reports zero idle times, with `TEST_NUM_CORES` CPUs.
    pub async fn setup_simple_test_node() -> Rc<CpuStatsHandler> {
        setup_test_node(|| vec![Nanoseconds(0); TEST_NUM_CORES as usize]).await
    }

    /// This test creates a CpuStatsHandler node and sends it the 'GetNumCpus' message. The
    /// test verifies that the message returns successfully and the expected number of CPUs
    /// are reported (in the test configuration, it should report `TEST_NUM_CORES`).
    #[fasync::run_singlethreaded(test)]
    async fn test_get_num_cpus() {
        let node = setup_simple_test_node().await;
        let num_cpus = node.handle_message(&Message::GetNumCpus).await.unwrap();
        if let MessageReturn::GetNumCpus(n) = num_cpus {
            assert_eq!(n, TEST_NUM_CORES);
        } else {
            assert!(false);
        }
    }

    /// Tests that the node correctly calculates CPU loads for all CPUs as a response to the
    /// 'GetCpuLoads' message.
    #[test]
    fn test_handle_get_cpu_loads() {
        // Use executor so we can advance the fake time
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();

        // Fake idle times that will be fed into the node. These idle times mean the node will first
        // see idle times of 0 for both CPUs, then idle times of 1s and 2s on the next poll.
        let mut fake_idle_times: VecDeque<Vec<Nanoseconds>> = vec![
            vec![Seconds(0.0).into(), Seconds(0.0).into()],
            vec![Seconds(1.0).into(), Seconds(2.0).into()],
        ]
        .into();

        let node = executor
            .run_until_stalled(&mut Box::pin(setup_test_node(move || {
                fake_idle_times.pop_front().unwrap()
            })))
            .unwrap();

        // Move fake time forward by 4s. This total time delta combined with the `fake_idle_times`
        // data mean the CPU loads should be reported as 0.75 and 0.25.
        executor.set_fake_time(executor.now().add(4.seconds()));
        executor
            .run_until_stalled(&mut Box::pin(async {
                match node.handle_message(&Message::GetCpuLoads).await {
                    Ok(MessageReturn::GetCpuLoads(loads)) => {
                        assert_eq!(loads, vec![0.75, 0.5]);
                    }
                    e => panic!("Unexpected message response: {:?}", e),
                }
            }))
            .unwrap();
    }

    #[test]
    fn test_handle_get_cpu_loads_with_staleness() {
        // Use executor so we can advance the fake time
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();

        // Fake idle times that will be fed into the node. These idle times mean the node will first
        // see idle times of 0 for both CPUs, then idle times of 1s and 2s on the next poll.
        let mut fake_idle_times: VecDeque<Vec<Nanoseconds>> = vec![
            vec![Seconds(0.0).into(), Seconds(0.0).into()],
            vec![Seconds(1.0).into(), Seconds(2.0).into()],
            vec![Seconds(7.0).into(), Seconds(2.0).into()],
        ]
        .into();

        // Create a node with a 10s cache duration
        let node = executor
            .run_until_stalled(&mut Box::pin(
                CpuStatsHandlerBuilder::new()
                    .with_proxy(setup_fake_service(move || fake_idle_times.pop_front().unwrap()))
                    .with_cpu_load_cache_duration(Seconds(10.0).into())
                    .build(),
            ))
            .unwrap()
            .unwrap();

        // Move fake time forward by 4s. This total time delta combined with the `fake_idle_times`
        // data mean the CPU loads should be reported as 0.75 and 0.25. CPU load will be considered
        // stale because it has never been calculated before.
        executor.set_fake_time(executor.now().add(4.seconds()));
        executor
            .run_until_stalled(&mut Box::pin(async {
                match node.handle_message(&Message::GetCpuLoads).await {
                    Ok(MessageReturn::GetCpuLoads(loads)) => {
                        assert_eq!(loads, vec![0.75, 0.5]);
                    }
                    e => panic!("Unexpected message response: {:?}", e),
                }
            }))
            .unwrap();

        // Move time forward another 4s. Since this is within the 10s cache duration,
        // `fake_idle_times` should remain unpolled and the node should report identical CPU loads
        // as before.
        executor.set_fake_time(executor.now().add(4.seconds()));
        executor
            .run_until_stalled(&mut Box::pin(async {
                match node.handle_message(&Message::GetCpuLoads).await {
                    Ok(MessageReturn::GetCpuLoads(loads)) => {
                        assert_eq!(loads, vec![0.75, 0.5]);
                    }
                    e => panic!("Unexpected message response: {:?}", e),
                }
            }))
            .unwrap();

        // Move time forward another 8s. We should now see the node poll `fake_idle_times` again and
        // report load from the last 12 seconds (since we've crossed the 10s cache duration).
        executor.set_fake_time(executor.now().add(8.seconds()));
        executor
            .run_until_stalled(&mut Box::pin(async {
                match node.handle_message(&Message::GetCpuLoads).await {
                    Ok(MessageReturn::GetCpuLoads(loads)) => {
                        assert_eq!(loads, vec![0.5, 1.0]);
                    }
                    e => panic!("Unexpected message response: {:?}", e),
                }
            }))
            .unwrap();
    }

    /// Tests that an unsupported message is handled gracefully and an Unsupported error is returned
    #[fasync::run_singlethreaded(test)]
    async fn test_unsupported_msg() {
        let node = setup_simple_test_node().await;
        match node.handle_message(&Message::ReadTemperature).await {
            Err(PowerManagerError::Unsupported) => {}
            e => panic!("Unexpected return value: {:?}", e),
        }
    }

    /// Tests for the presence and correctness of dynamically-added inspect data
    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_data() {
        let inspector = inspect::Inspector::new();
        let node = CpuStatsHandlerBuilder::new()
            .with_proxy(setup_fake_service(|| vec![Nanoseconds(0); TEST_NUM_CORES as usize]))
            .with_inspect_root(inspector.root())
            .build()
            .await
            .unwrap();

        // For each message, the node will query CPU load and log the sample into Inspect
        node.handle_message(&Message::GetCpuLoads).await.unwrap();

        assert_data_tree!(
            inspector,
            root: {
                CpuStatsHandler: {
                    cpu_loads: {
                        "0": {
                            load: TEST_NUM_CORES as f64,
                            "@time": inspect::testing::AnyProperty
                        }
                    }
                }
            }
        );
    }

    /// Tests that well-formed configuration JSON does not panic the `new_from_json` function.
    #[fasync::run_singlethreaded(test)]
    async fn test_new_from_json() {
        let json_data = json::json!({
            "type": "CpuStatsHandler",
            "name": "cpu_stats",
        });
        let _ = CpuStatsHandlerBuilder::new_from_json(json_data, &HashMap::new());

        let json_data = json::json!({
            "type": "CpuStatsHandler",
            "name": "cpu_stats",
            "config": {
                "cpu_load_cache_duration_ms": 10
            }
        });
        let _ = CpuStatsHandlerBuilder::new_from_json(json_data, &HashMap::new());
    }
}
