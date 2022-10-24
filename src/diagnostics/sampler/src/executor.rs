// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # Data Structure and Algorithm Overview
//!
//! Cobalt is organized into Projects, each of which contains several Metrics.
//!
//! [`MetricConfig`] - defined in src/diagnostics/lib/sampler-config/src/lib.rs
//! This is deserialized from Sampler config files or created by FIRE by interpolating
//! component information into FIRE config files. It contains
//!  - selectors: SelectorList
//!  - metric_id
//!  - metric_type: DataType
//!  - event_codes: Vec<u32>
//!  - upload_once boolean
//!
//! **NOTE:** Multiple selectors can be provided in a single metric. Only one selector is
//! expected to fetch/match data. When one does, the other selectors will be disabled for
//! efficiency.
//!
//! [`ProjectConfig`] - defined in src/diagnostics/lib/sampler-config/src/lib.rs
//! This encodes the contents of a single config file:
//!  - project_id
//!  - customer_id (defaults to 1)
//!  - poll_rate_sec
//!  - metrics: Vec<MetricConfig>
//!
//! [`SamplerConfig`] - defined in src/diagnostics/lib/sampler-config/src/lib.rs
//! The entire config for Sampler. Contains
//!  - list of ProjectConfig
//!  - minimum sample rate
//!
//! [`ProjectSampler`] - defined in src/diagnostics/sampler/src/executor.rs
//! This contains
//!  - several MetricConfig's
//!  - an ArchiveReader configured with all active selectors
//!  - a cache of previous Diagnostic values, indexed by selector strings
//!  - FIDL proxies for Cobalt and MetricEvent loggers
//!     - these loggers are configured with project_id and customer_id
//!  - Poll rate
//!  - Inspect stats (struct ProjectSamplerStats)
//!  - moniker_to_selector_map (see below * )
//!
//! [`ProjectSampler`] is stored in:
//!  - [`TaskCancellation`]:     execution_context: fasync::Task<Vec<ProjectSampler>>,
//!  - [`RebootSnapshotProcessor`]:    project_samplers: Vec<ProjectSampler>,
//!  - [`SamplerExecutor`]:     project_samplers: Vec<ProjectSampler>,
//!  - [`ProjectSamplerTaskExit::RebootTriggered(ProjectSampler)`],
//!
//! [`SamplerExecutor`] (defined in executor.rs) is built from a single [`SamplerConfig`].
//! [`SamplerExecutor`] contains
//!  - a list of ProjectSamplers
//!  - an Inspect stats structure
//! [`SamplerExecutor`] only has one member function execute() which calls spawn() on each
//! project sampler, passing it a receiver-oneshot to cancel it. The collection of
//! oneshot-senders and spawned-tasks builds the returned TaskCancellation.
//!
//! [`TaskCancellation`] is then passed to a reboot_watcher (in src/diagnostics/sampler/src/lib.rs)
//! which does nothing until the reboot service either closes (calling
//! run_without_cancellation()) or sends a message (calling perform_reboot_cleanup()).
//!
//! [`ProjectSampler`] calls fasync::Task::spawn to create a task that starts a timer, then loops
//! listening to that timer and to the reboot_oneshot. When the timer triggers, it calls
//! self.process_next_snapshot(). If the reboot oneshot arrives, the task returns
//! [`ProjectSamplerTaskExit::RebootTriggered(self)`].
//!
//!
//! * moniker_to_selector_map
//!
//! When a [`ProjectSampler`] retrieves data, the data arrives from the Archivist organized by
//! moniker. For each moniker, we need to visit the selectors that may find data in it. Those
//! selectors may be scattered throughout the [`MetricConfig`]'s in the [`ProjectSampler`].
//! moniker_to_selector_map contains a map from moniker to [`SelectorIndexes`], a struct containing
//! the index of the [`MetricConfig`] in the [`ProjectSampler`]'s list, and the selector in the
//! [`MetricConfig`]'s list.
//!
//! **NOTE:** The moniker portion of the selectors must match exactly the moniker returned from
//! the Archivist. No wildcards.
//!
//! Selectors will become unused, either because of upload_once, or because data was found by a
//! different selector. Rather than implement deletion in the moniker_to_selector_map and the vec's,
//! which would add lots of bug surface and maintenance debt, each selector is an Option<> so that
//! selectors can be deleted/disabled without changing the rest of the data structure.
//! Once all Diagnostic data is processed, the structure is rebuilt if any selectors
//! have been disabled; rebuilding less often would be
//! premature optimization at this point.
//!
//! perform_reboot_cleanup() builds a moniker_to_project_map for use by the
//! [`RebootSnapshotProcessor`]. This has a similar purpose to moniker_to_selector_map, since
//! monikers' data may be relevant to any number of projects. When not rebooting, each project
//! fetches its own data from Archivist, so there's no need for a moniker_to_project_map. But
//! on reboot, all projects' data is fetched at once, and needs to be sorted out.

use {
    crate::diagnostics::*,
    anyhow::{format_err, Context, Error},
    diagnostics_data::{self, Data},
    diagnostics_hierarchy::{ArrayContent, DiagnosticsHierarchy, Property},
    diagnostics_reader::{ArchiveReader, Inspect},
    fidl_fuchsia_metrics::{
        HistogramBucket, MetricEvent, MetricEventLoggerFactoryMarker,
        MetricEventLoggerFactoryProxy, MetricEventLoggerProxy, MetricEventPayload, ProjectSpec,
    },
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_inspect::{self as inspect, NumericProperty},
    fuchsia_inspect_derive::WithInspect,
    fuchsia_zircon as zx,
    futures::{channel::oneshot, future::join_all, select, stream::FuturesUnordered, StreamExt},
    sampler_config::{
        DataType, MetricConfig, ParsedSelector, ProjectConfig, SamplerConfig, SelectorList,
    },
    std::{
        collections::{HashMap, HashSet},
        convert::{TryFrom, TryInto},
        sync::Arc,
    },
    tracing::{info, warn},
};

/// An event to be logged to the cobalt logger. Events are generated first,
/// then logged. (This permits unit-testing the code that generates events from
/// Diagnostic data.)
type EventToLog = MetricEvent;

pub struct TaskCancellation {
    senders: Vec<oneshot::Sender<()>>,
    _sampler_executor_stats: Arc<SamplerExecutorStats>,
    execution_context: fasync::Task<Vec<ProjectSampler>>,
}

impl TaskCancellation {
    /// It's possible the reboot register goes down. If that
    /// happens, we want to continue driving execution with
    /// no consideration for cancellation. This does that.
    pub async fn run_without_cancellation(self) {
        self.execution_context.await;
    }

    pub async fn perform_reboot_cleanup(self) {
        // Let every sampler know that a reboot is pending and they should exit.
        self.senders.into_iter().for_each(|sender| {
            sender
                .send(())
                .unwrap_or_else(|err| warn!("Failed to send reboot over oneshot: {:?}", err))
        });

        // Get the most recently updated project samplers from all the futures. They hold the
        // cache with the most recent values for all the metrics we need to sample and diff!
        let project_samplers: Vec<ProjectSampler> = self.execution_context.await;

        // Maps a selector to the indices of the project_samplers vec which contain configurations
        // that transform the property defined by the selector.
        let mut moniker_to_projects_map: HashMap<String, HashSet<usize>> = HashMap::new();
        // Set of all selectors from all projects
        let mut mondo_selectors_set = Vec::new();

        for (index, project_sampler) in project_samplers.iter().enumerate() {
            for metric in &project_sampler.metrics {
                for selector_opt in metric.selectors.iter() {
                    if let Some(selector) = selector_opt {
                        if moniker_to_projects_map.get(&selector.moniker).is_none() {
                            moniker_to_projects_map
                                .insert(selector.moniker.to_string(), HashSet::new());
                        }
                        moniker_to_projects_map.get_mut(&selector.moniker).unwrap().insert(index);
                        mondo_selectors_set.push(selector.selector_string.clone());
                    }
                }
            }
        }

        let mut reader = ArchiveReader::new();
        reader.retry_if_empty(false).add_selectors(mondo_selectors_set.into_iter());

        let mut reboot_processor =
            RebootSnapshotProcessor { reader, project_samplers, moniker_to_projects_map };
        // Log errors encountered in final snapshot, but always swallow errors so we can gracefully
        // notify RebootMethodsWatcherRegister that we yield our remaining time.
        reboot_processor
            .process_reboot_sample()
            .await
            .unwrap_or_else(|e| warn!("Reboot snapshot failed! {:?}", e));
    }
}

struct RebootSnapshotProcessor {
    /// Reader constructed from the union of selectors
    /// for every [`ProjectSampler`] config.
    reader: ArchiveReader,
    /// Vector of mutable [`ProjectSampler`] objects that will
    /// process their final samples.
    project_samplers: Vec<ProjectSampler>,
    /// Mapping from a moniker to a vector of indices into
    /// the project_samplers, where each indexed [`ProjectSampler`] has
    /// at least one selector that uses that moniker.
    moniker_to_projects_map: HashMap<String, HashSet<usize>>,
}

impl RebootSnapshotProcessor {
    pub async fn process_reboot_sample(&mut self) -> Result<(), Error> {
        let snapshot_data = self.reader.snapshot::<Inspect>().await?;
        for data_packet in snapshot_data {
            let moniker = data_packet.moniker;
            match data_packet.payload {
                None => {
                    process_schema_errors(&data_packet.metadata.errors, &moniker);
                }
                Some(payload) => {
                    self.process_single_payload(payload, &data_packet.metadata.filename, &moniker)
                        .await
                }
            }
        }
        Ok(())
    }

    async fn process_single_payload(
        &mut self,
        hierarchy: DiagnosticsHierarchy<String>,
        diagnostics_filename: &str,
        moniker: &String,
    ) {
        if let Some(project_indexes) = self.moniker_to_projects_map.get(moniker) {
            for index in project_indexes {
                let project_sampler = &mut self.project_samplers[*index];
                // If processing the final sample failed, just log the
                // error and proceed, everything's getting shut down
                // soon anyway.
                let maybe_err = match project_sampler
                    .process_component_data(&hierarchy, diagnostics_filename, moniker)
                    .await
                {
                    Err(err) => Some(err),
                    Ok((_selector_changes, events_to_log)) => {
                        project_sampler.log_events(events_to_log).await.err()
                    }
                };
                if let Some(err) = maybe_err {
                    warn!(?err, "A project sampler failed to process a reboot sample");
                }
            }
        } else {
            warn!(%moniker, "A moniker was not found in the project_samplers map");
        }
    }
}

/// Owner of the sampler execution context.
pub struct SamplerExecutor {
    project_samplers: Vec<ProjectSampler>,
    sampler_executor_stats: Arc<SamplerExecutorStats>,
}

impl SamplerExecutor {
    /// Instantiate connection to the cobalt logger and map ProjectConfigurations
    /// to [`ProjectSampler`] plans.
    pub async fn new(sampler_config: SamplerConfig) -> Result<Self, Error> {
        let metric_logger_factory: Arc<MetricEventLoggerFactoryProxy> = Arc::new(
            connect_to_protocol::<MetricEventLoggerFactoryMarker>()
                .context("Failed to connect to the Metric LoggerFactory")?,
        );

        let minimum_sample_rate_sec = sampler_config.minimum_sample_rate_sec;

        let sampler_executor_stats = Arc::new(
            SamplerExecutorStats::new()
                .with_inspect(inspect::component::inspector().root(), "sampler_executor_stats")
                .unwrap_or_else(|err| {
                    warn!(?err, "Failed to attach inspector to SamplerExecutorStats struct");
                    SamplerExecutorStats::default()
                }),
        );

        sampler_executor_stats
            .total_project_samplers_configured
            .add(sampler_config.project_configs.len() as u64);

        let mut project_to_stats_map: HashMap<u32, Arc<ProjectSamplerStats>> = HashMap::new();
        // TODO(fxbug.dev/42067): Create only one ArchiveReader for each unique poll rate so we
        // can avoid redundant snapshots.
        let project_sampler_futures =
            sampler_config.project_configs.into_iter().map(|project_config| {
                let project_sampler_stats =
                    project_to_stats_map.entry(project_config.project_id).or_insert(Arc::new(
                        ProjectSamplerStats::new()
                            .with_inspect(
                                &sampler_executor_stats.inspect_node,
                                format!("project_{:?}", project_config.project_id,),
                            )
                            .unwrap_or_else(|err| {
                                warn!(
                                    ?err,
                                    "Failed to attach inspector to ProjectSamplerStats struct"
                                );
                                ProjectSamplerStats::default()
                            }),
                    ));
                ProjectSampler::new(
                    project_config,
                    metric_logger_factory.clone(),
                    minimum_sample_rate_sec,
                    project_sampler_stats.clone(),
                )
            });

        let mut project_samplers: Vec<ProjectSampler> = Vec::new();
        for project_sampler in join_all(project_sampler_futures).await.into_iter() {
            match project_sampler {
                Ok(project_sampler) => project_samplers.push(project_sampler),
                Err(e) => {
                    warn!("ProjectSampler construction failed: {:?}", e);
                }
            }
        }
        Ok(SamplerExecutor { project_samplers, sampler_executor_stats })
    }

    /// Turn each [`ProjectSampler`] plan into an [`fasync::Task`] which executes its associated plan,
    /// and process errors if any tasks exit unexpectedly.
    pub fn execute(self) -> TaskCancellation {
        // Take ownership of the inspect struct so we can give it to the execution context. We do this
        // so that the execution context can return the struct when it's halted by reboot, which allows inspect
        // properties to survive through the reboot flow.
        let task_cancellation_owned_stats = self.sampler_executor_stats.clone();
        let execution_context_owned_stats = self.sampler_executor_stats.clone();

        let (senders, mut spawned_tasks): (Vec<oneshot::Sender<()>>, FuturesUnordered<_>) = self
            .project_samplers
            .into_iter()
            .map(|project_sampler| {
                let (sender, receiver) = oneshot::channel::<()>();
                (sender, project_sampler.spawn(receiver))
            })
            .unzip();

        let execution_context = fasync::Task::spawn(async move {
            let mut healthily_exited_samplers = Vec::new();
            while let Some(sampler_result) = spawned_tasks.next().await {
                match sampler_result {
                    Err(e) => {
                        // TODO(fxbug.dev/42067): Consider restarting the failed sampler depending on
                        // failure mode.
                        warn!("A spawned sampler has failed: {:?}", e);
                        execution_context_owned_stats.errorfully_exited_samplers.add(1);
                    }
                    Ok(ProjectSamplerTaskExit::RebootTriggered(sampler)) => {
                        healthily_exited_samplers.push(sampler);
                        execution_context_owned_stats.reboot_exited_samplers.add(1);
                    }
                    Ok(ProjectSamplerTaskExit::WorkCompleted) => {
                        info!("A sampler completed its workload, and exited.");
                        execution_context_owned_stats.healthily_exited_samplers.add(1);
                    }
                }
            }

            healthily_exited_samplers
        });

        TaskCancellation {
            execution_context,
            senders,
            _sampler_executor_stats: task_cancellation_owned_stats,
        }
    }
}

pub struct ProjectSampler {
    archive_reader: ArchiveReader,
    /// The metrics used by this Project. Indexed by moniker_to_selector_map.
    metrics: Vec<MetricConfig>,
    /// Cache from Inspect selector to last sampled property. This is the selector from
    /// [`MetricConfig`]; it may contain wildcards.
    metric_cache: HashMap<MetricCacheKey, Property>,
    /// Cobalt logger proxy using this ProjectSampler's project id. It's an Option so it doesn't
    /// have to be created for unit tests; it will always be Some() outside unit tests.
    metrics_logger: Option<MetricEventLoggerProxy>,
    /// Map from moniker to relevant selectors.
    moniker_to_selector_map: HashMap<String, Vec<SelectorIndexes>>,
    /// The frequency with which we snapshot Inspect properties
    /// for this project.
    poll_rate_sec: i64,
    /// Inspect stats on a node namespaced by this project's associated id.
    /// It's an arc since a single project can have multiple samplers at
    /// different frequencies, but we want a single project to have one node.
    project_sampler_stats: Arc<ProjectSamplerStats>,
}

#[derive(Debug, Eq, Hash, PartialEq)]
struct MetricCacheKey {
    filename: String,
    selector: String,
}

#[derive(Clone, Debug)]
struct SelectorIndexes {
    /// The index of the metric in [`ProjectSampler`]'s `metrics` list.
    metric_index: usize,
    /// The index of the selector in the [`MetricConfig`]'s `selectors` list.
    selector_index: usize,
}

pub enum ProjectSamplerTaskExit {
    /// The [`ProjectSampler`] processed a reboot signal on its oneshot, and yielded
    /// to the final-snapshot.
    RebootTriggered(ProjectSampler),
    /// The [`ProjectSampler`] has no more work to complete; perhaps all metrics were "upload_once"?
    WorkCompleted,
}

pub enum ProjectSamplerEvent {
    TimerTriggered,
    TimerDied,
    RebootTriggered,
    RebootChannelClosed(Error),
}

/// Indicates whether a sampler in the project has been removed (set to None), in which case the
/// ArchiveAccessor should be reconfigured.
/// The selector lists may be consolidated (and thus the maps would be rebuilt), but
/// the program will run correctly whether they are or not.
#[derive(PartialEq)]
enum SnapshotOutcome {
    SelectorsChanged,
    SelectorsUnchanged,
}

impl ProjectSampler {
    pub async fn new(
        config: ProjectConfig,
        metric_logger_factory: Arc<MetricEventLoggerFactoryProxy>,
        minimum_sample_rate_sec: i64,
        project_sampler_stats: Arc<ProjectSamplerStats>,
    ) -> Result<ProjectSampler, Error> {
        let customer_id = config.customer_id();
        let project_id = config.project_id;
        let poll_rate_sec = config.poll_rate_sec;
        if poll_rate_sec < minimum_sample_rate_sec {
            return Err(format_err!(
                concat!(
                    "Project with id: {:?} uses a polling rate:",
                    " {:?} below minimum configured poll rate: {:?}"
                ),
                project_id,
                poll_rate_sec,
                minimum_sample_rate_sec,
            ));
        }

        project_sampler_stats.project_sampler_count.add(1);
        project_sampler_stats.metrics_configured.add(config.metrics.len() as u64);

        let metrics_logger = {
            let (metrics_logger_proxy, metrics_server_end) =
                fidl::endpoints::create_proxy().context("Failed to create endpoints")?;
            let mut project_spec = ProjectSpec::EMPTY;
            project_spec.customer_id = Some(customer_id);
            project_spec.project_id = Some(project_id);
            metric_logger_factory
                .create_metric_event_logger(project_spec, metrics_server_end)
                .await?
                .map_err(|e| format_err!("error response {:?}", e))?;
            Some(metrics_logger_proxy)
        };
        let mut all_selectors = Vec::<String>::new();
        for metric in &config.metrics {
            for selector_opt in metric.selectors.iter() {
                if let Some(selector) = selector_opt {
                    all_selectors.push(selector.selector_string.clone());
                }
            }
        }
        let mut project_sampler = ProjectSampler {
            archive_reader: ArchiveReader::new(),
            moniker_to_selector_map: HashMap::new(),
            metrics: config.metrics,
            metric_cache: HashMap::new(),
            metrics_logger,
            poll_rate_sec,
            project_sampler_stats,
        };
        // Fill in archive_reader and moniker_to_selector_map
        project_sampler.rebuild_selector_data_structures();
        Ok(project_sampler)
    }

    pub fn spawn(
        mut self,
        mut reboot_oneshot: oneshot::Receiver<()>,
    ) -> fasync::Task<Result<ProjectSamplerTaskExit, Error>> {
        fasync::Task::spawn(async move {
            let mut periodic_timer =
                fasync::Interval::new(zx::Duration::from_seconds(self.poll_rate_sec));
            loop {
                let done = select! {
                    opt = periodic_timer.next() => {
                        if opt.is_some() {
                            ProjectSamplerEvent::TimerTriggered
                        } else {
                            ProjectSamplerEvent::TimerDied
                        }
                    },
                    oneshot_res = reboot_oneshot => {
                        match oneshot_res {
                            Ok(()) => {
                                ProjectSamplerEvent::RebootTriggered
                            },
                            Err(e) => {
                                ProjectSamplerEvent::RebootChannelClosed(
                                    format_err!("Oneshot closure error: {:?}", e))
                            }
                        }
                    }
                };

                match done {
                    ProjectSamplerEvent::TimerDied => {
                        return Err(format_err!(concat!(
                            "The ProjectSampler timer died, something went wrong.",
                        )));
                    }
                    ProjectSamplerEvent::RebootChannelClosed(e) => {
                        // TODO(fxbug.dev/42067): Consider differentiating errors if
                        // we ever want to recover a sampler after a oneshot channel death.
                        return Err(format_err!(
                            concat!(
                                "The Reboot signaling oneshot died, something went wrong: {:?}",
                            ),
                            e
                        ));
                    }
                    ProjectSamplerEvent::RebootTriggered => {
                        // The reboot oneshot triggered, meaning it's time to perform
                        // our final snapshot. Return self to reuse most recent cache.
                        return Ok(ProjectSamplerTaskExit::RebootTriggered(self));
                    }
                    ProjectSamplerEvent::TimerTriggered => {
                        self.process_next_snapshot().await?;
                        // Check whether this sampler
                        // still needs to run (perhaps all metrics were
                        // "upload_once"?). If it doesn't, we want to be
                        // sure that it is not included in the reboot-workload.
                        if self.is_all_done() {
                            return Ok(ProjectSamplerTaskExit::WorkCompleted);
                        }
                    }
                }
            }
        })
    }

    async fn process_next_snapshot(&mut self) -> Result<(), Error> {
        let snapshot_data = self.archive_reader.snapshot::<Inspect>().await?;
        let events_to_log = self.process_snapshot(snapshot_data).await?;
        self.log_events(events_to_log).await?;
        Ok(())
    }

    async fn process_snapshot(
        &mut self,
        snapshot: Vec<Data<Inspect>>,
    ) -> Result<Vec<EventToLog>, Error> {
        let mut selectors_changed = false;
        let mut events_to_log = vec![];
        for data_packet in snapshot.iter() {
            match &data_packet.payload {
                None => {
                    process_schema_errors(&data_packet.metadata.errors, &data_packet.moniker);
                }
                Some(payload) => {
                    let (selector_outcome, mut events) = self
                        .process_component_data(
                            payload,
                            &data_packet.metadata.filename,
                            &data_packet.moniker,
                        )
                        .await?;
                    if selector_outcome == SnapshotOutcome::SelectorsChanged {
                        selectors_changed = true;
                    }
                    events_to_log.append(&mut events);
                }
            }
        }
        if selectors_changed {
            self.rebuild_selector_data_structures();
        }
        Ok(events_to_log)
    }

    fn is_all_done(&self) -> bool {
        self.moniker_to_selector_map.is_empty()
    }

    fn rebuild_selector_data_structures(&mut self) {
        let mut all_selectors = vec![];
        for mut metric in &mut self.metrics {
            // TODO(fxbug.dev/87709): Using Box<ParsedSelector> could reduce copying.
            let active_selectors = metric
                .selectors
                .iter()
                .filter(|selector| selector.is_some())
                .map(|selector| selector.to_owned())
                .collect::<Vec<_>>();
            for selector in active_selectors.iter() {
                // unwrap() is OK since we filtered for Some
                all_selectors.push(selector.as_ref().unwrap().selector_string.to_owned());
            }
            metric.selectors = SelectorList(active_selectors);
        }
        self.archive_reader = ArchiveReader::new();
        self.archive_reader.retry_if_empty(false).add_selectors(all_selectors.into_iter());
        self.moniker_to_selector_map = HashMap::new();
        for (metric_index, metric) in self.metrics.iter().enumerate() {
            for (selector_index, selector) in metric.selectors.iter().enumerate() {
                let moniker = &selector.as_ref().unwrap().moniker;
                if self.moniker_to_selector_map.get(moniker).is_none() {
                    self.moniker_to_selector_map.insert(moniker.clone(), Vec::new());
                }
                self.moniker_to_selector_map
                    .get_mut(moniker)
                    .unwrap()
                    .push(SelectorIndexes { metric_index, selector_index });
            }
        }
    }

    async fn process_component_data(
        &mut self,
        payload: &DiagnosticsHierarchy,
        diagnostics_filename: &str,
        moniker: &String,
    ) -> Result<(SnapshotOutcome, Vec<EventToLog>), Error> {
        let indexes_opt = &self.moniker_to_selector_map.get(moniker);
        let selector_indexes = match indexes_opt {
            None => {
                warn!(%moniker, "Moniker not found in map");
                return Ok((SnapshotOutcome::SelectorsUnchanged, vec![]));
            }
            Some(indexes) => indexes.to_vec(),
        };
        // We cloned the selector indexes. Whatever we do in this function must not invalidate them.
        let mut snapshot_outcome = SnapshotOutcome::SelectorsUnchanged;
        let mut events_to_log = vec![];
        for index_info in selector_indexes.iter() {
            let SelectorIndexes { metric_index, selector_index } = index_info;
            let metric = &self.metrics[*metric_index];
            // It's fine if a selector has been removed and is None.
            if let Some(ParsedSelector { selector, selector_string, .. }) =
                &metric.selectors[*selector_index]
            {
                let found_values = diagnostics_hierarchy::select_from_hierarchy(
                    payload.clone(),
                    selector.clone(),
                )?;
                match found_values.len() {
                    // Maybe the data hasn't been published yet. Maybe another selector in this
                    // metric is the correct one to find the data. Either way, not-found is fine.
                    0 => {}
                    1 => {
                        // export_sample() needs mut self, so we can't pass in values directly from
                        // metric, since metric is a ref into data contained in self;
                        // we have to copy them out first.
                        let metric_type = metric.metric_type;
                        let metric_id = metric.metric_id;
                        let codes = metric.event_codes.clone();
                        let metric_cache_key = MetricCacheKey {
                            filename: diagnostics_filename.to_string(),
                            selector: selector_string.to_string(),
                        };
                        if let Some(event) = self
                            .prepare_sample(
                                metric_type,
                                metric_id,
                                codes,
                                metric_cache_key,
                                &found_values[0].property,
                            )
                            .await?
                        {
                            if let Some(ParsedSelector { upload_count, .. }) =
                                &self.metrics[*metric_index].selectors[*selector_index]
                            {
                                upload_count.add(1);
                            }
                            events_to_log.push(event);
                        }
                        if self.update_metric_selectors(index_info) {
                            snapshot_outcome = SnapshotOutcome::SelectorsChanged;
                        }
                    }
                    too_many => warn!(%too_many, %selector_string, "Too many matches for selector"),
                }
            }
        }
        Ok((snapshot_outcome, events_to_log))
    }

    /// Handle selectors that may be removed (e.g. if upload_once is set). Return true if the
    /// selector was changed/removed, false otherwise.
    fn update_metric_selectors(&mut self, index_info: &SelectorIndexes) -> bool {
        let metric = &mut self.metrics[index_info.metric_index];
        if let Some(true) = metric.upload_once {
            for selector in metric.selectors.iter_mut() {
                *selector = None;
            }
            return true;
        }
        let mut deleted = false;
        for (index, selector) in metric.selectors.iter_mut().enumerate() {
            if index != index_info.selector_index && *selector != None {
                *selector = None;
                deleted = true;
            }
        }
        return deleted;
    }

    async fn prepare_sample(
        &mut self,
        metric_type: DataType,
        metric_id: u32,
        event_codes: Vec<u32>,
        metric_cache_key: MetricCacheKey,
        new_sample: &Property,
    ) -> Result<Option<EventToLog>, Error> {
        let previous_sample_opt: Option<&Property> = self.metric_cache.get(&metric_cache_key);

        if let Some(payload) = process_sample_for_data_type(
            new_sample,
            previous_sample_opt,
            &metric_cache_key,
            &metric_type,
        ) {
            self.maybe_update_cache(new_sample, &metric_type, metric_cache_key);
            Ok(Some(EventToLog { metric_id, event_codes, payload }))
        } else {
            Ok(None)
        }
    }

    async fn log_events(&mut self, events: Vec<EventToLog>) -> Result<(), Error> {
        for event in events.into_iter() {
            let mut metric_events = vec![event];
            // Note: The MetricEvent vector can't be marked send because it
            // is a dyn object stream and rust can't confirm that it doesn't have handles. This
            // is fine because we don't actually need to "send" to make the API call. But if we
            // chain the creation of the future with the await on the future, rust interperets all
            // variables including the reference to the event vector as potentially being needed
            // across the await. So we have to split the creation of the future out from the await
            // on the future. :(
            let log_future = self
                .metrics_logger
                .as_ref()
                .unwrap()
                .log_metric_events(&mut metric_events.iter_mut());

            log_future.await?.map_err(|e| format_err!("error from cobalt: {:?}", e))?;
            self.project_sampler_stats.cobalt_logs_sent.add(1);
        }
        Ok(())
    }

    fn maybe_update_cache(
        &mut self,
        new_sample: &Property,
        data_type: &DataType,
        metric_cache_key: MetricCacheKey,
    ) {
        match data_type {
            DataType::Occurrence | DataType::IntHistogram => {
                self.metric_cache.insert(metric_cache_key, new_sample.clone());
            }
            DataType::Integer | DataType::String => (),
        }
    }
}

fn process_sample_for_data_type(
    new_sample: &Property,
    previous_sample_opt: Option<&Property>,
    data_source: &MetricCacheKey,
    data_type: &DataType,
) -> Option<MetricEventPayload> {
    let event_payload_res = match data_type {
        DataType::Occurrence => process_occurence(new_sample, previous_sample_opt, data_source),
        DataType::IntHistogram => {
            process_int_histogram(new_sample, previous_sample_opt, data_source)
        }
        DataType::Integer => {
            // If we previously cached a metric with an int-type, log a warning and ignore it.
            // This may be a case of using a single selector for two metrics, one event count
            // and one int.
            if previous_sample_opt.is_some() {
                warn!("Sampler has erroneously cached an Int type metric: {:?}", data_source);
            }
            process_int(new_sample, data_source)
        }
        DataType::String => {
            if previous_sample_opt.is_some() {
                warn!("Sampler has erroneously cached a String type metric: {:?}", data_source);
            }
            process_string(new_sample, data_source)
        }
    };

    match event_payload_res {
        Ok(payload_opt) => payload_opt,
        Err(err) => {
            warn!(?data_source, ?err, "Failed to process Inspect property for cobalt",);
            None
        }
    }
}

/// It's possible for Inspect numerical properties to experience overflows/conversion
/// errors when being mapped to Cobalt types. Sanitize these numericals, and provide
/// meaningful errors.
fn sanitize_unsigned_numerical(diff: u64, data_source: &MetricCacheKey) -> Result<i64, Error> {
    match diff.try_into() {
        Ok(diff) => Ok(diff),
        Err(e) => {
            return Err(format_err!(
                concat!(
                    "Selector used for EventCount type",
                    " refered to an unsigned int property,",
                    " but cobalt requires i64, and casting introduced overflow",
                    " which produces a negative int: {:?}. This could be due to",
                    " a single sample being larger than i64, or a diff between",
                    " samples being larger than i64. Error: {:?}"
                ),
                data_source,
                e
            ));
        }
    }
}

fn process_int_histogram(
    new_sample: &Property,
    prev_sample_opt: Option<&Property>,
    data_source: &MetricCacheKey,
) -> Result<Option<MetricEventPayload>, Error> {
    let diff = match prev_sample_opt {
        None => convert_inspect_histogram_to_cobalt_histogram(new_sample, data_source)?,
        Some(prev_sample) => {
            // If the data type changed then we just reset the cache.
            if std::mem::discriminant(new_sample) == std::mem::discriminant(prev_sample) {
                compute_histogram_diff(new_sample, prev_sample, data_source)?
            } else {
                convert_inspect_histogram_to_cobalt_histogram(new_sample, data_source)?
            }
        }
    };

    let non_empty_diff: Vec<HistogramBucket> = diff.into_iter().filter(|v| v.count != 0).collect();
    if !non_empty_diff.is_empty() {
        Ok(Some(MetricEventPayload::Histogram(non_empty_diff)))
    } else {
        Ok(None)
    }
}

fn compute_histogram_diff(
    new_sample: &Property,
    old_sample: &Property,
    data_source: &MetricCacheKey,
) -> Result<Vec<HistogramBucket>, Error> {
    let new_histogram_buckets =
        convert_inspect_histogram_to_cobalt_histogram(new_sample, data_source)?;
    let old_histogram_buckets =
        convert_inspect_histogram_to_cobalt_histogram(old_sample, data_source)?;

    if old_histogram_buckets.len() != new_histogram_buckets.len() {
        return Err(format_err!(
            concat!(
                "Selector referenced an Inspect IntArray",
                " that was specified as an IntHistogram type ",
                " but the histogram bucket count changed between",
                " samples, which is incompatible with Cobalt.",
                " Selector: {:?}, Inspect type: {}"
            ),
            data_source,
            new_sample.discriminant_name()
        ));
    }

    new_histogram_buckets
        .iter()
        .zip(old_histogram_buckets)
        .map(|(new_bucket, old_bucket)| {
            if new_bucket.count < old_bucket.count {
                return Err(format_err!(
                    concat!(
                        "Selector referenced an Inspect IntArray",
                        " that was specified as an IntHistogram type ",
                        " but at least one bucket saw the count decrease",
                        " between samples, which is incompatible with Cobalt's",
                        " need for monotonically increasing counts.",
                        " Selector: {:?}, Inspect type: {}"
                    ),
                    data_source,
                    new_sample.discriminant_name()
                ));
            }
            Ok(HistogramBucket {
                count: new_bucket.count - old_bucket.count,
                index: new_bucket.index,
            })
        })
        .collect::<Result<Vec<HistogramBucket>, Error>>()
}

fn convert_inspect_histogram_to_cobalt_histogram(
    inspect_histogram: &Property,
    data_source: &MetricCacheKey,
) -> Result<Vec<HistogramBucket>, Error> {
    let histogram_bucket_constructor =
        |index: usize, count: u64| -> Result<HistogramBucket, Error> {
            match u32::try_from(index) {
                Ok(index) => Ok(HistogramBucket { index, count }),
                Err(_) => Err(format_err!(
                    concat!(
                        "Selector referenced an Inspect IntArray",
                        " that was specified as an IntHistogram type ",
                        " but a bucket contained a negative count. This",
                        " is incompatible with Cobalt histograms which only",
                        " support positive histogram counts.",
                        " vector. Selector: {:?}, Inspect type: {}"
                    ),
                    data_source,
                    inspect_histogram.discriminant_name()
                )),
            }
        };

    match inspect_histogram {
        Property::IntArray(_, ArrayContent::Buckets(bucket_vec)) => bucket_vec
            .iter()
            .enumerate()
            .map(|(index, bucket)| {
                if bucket.count < 0 {
                    return Err(format_err!(
                        concat!(
                            "Selector referenced an Inspect IntArray",
                            " that was specified as an IntHistogram type ",
                            " but a bucket contained a negative count. This",
                            " is incompatible with Cobalt histograms which only",
                            " support positive histogram counts.",
                            " vector. Selector: {:?}, Inspect type: {}"
                        ),
                        data_source,
                        inspect_histogram.discriminant_name()
                    ));
                }

                // Count is a non-negative i64, so casting with `as` is safe from
                // truncations.
                histogram_bucket_constructor(index, bucket.count as u64)
            })
            .collect::<Result<Vec<HistogramBucket>, Error>>(),
        Property::UintArray(_, ArrayContent::Buckets(bucket_vec)) => bucket_vec
            .iter()
            .enumerate()
            .map(|(index, bucket)| histogram_bucket_constructor(index, bucket.count))
            .collect::<Result<Vec<HistogramBucket>, Error>>(),
        _ => {
            // TODO(fxbug.dev/42067): Does cobalt support floors or step counts that are
            // not ints? if so, we can support that as well with double arrays if the
            // actual counts are whole numbers.
            return Err(format_err!(
                concat!(
                    "Selector referenced an Inspect property",
                    " that was specified as an IntHistogram type ",
                    " but is unable to be encoded in a cobalt HistogramBucket",
                    " vector. Selector: {:?}, Inspect type: {}"
                ),
                data_source,
                inspect_histogram.discriminant_name()
            ));
        }
    }
}

fn process_int(
    new_sample: &Property,
    data_source: &MetricCacheKey,
) -> Result<Option<MetricEventPayload>, Error> {
    let sampled_int = match new_sample {
        Property::Uint(_, val) => sanitize_unsigned_numerical(val.clone(), data_source)?,
        Property::Int(_, val) => val.clone(),
        _ => {
            return Err(format_err!(
                concat!(
                    "Selector referenced an Inspect property",
                    " that was specified as an Int type ",
                    " but is unable to be encoded in an i64",
                    " Selector: {:?}, Inspect type: {}"
                ),
                data_source,
                new_sample.discriminant_name()
            ));
        }
    };

    Ok(Some(MetricEventPayload::IntegerValue(sampled_int)))
}

fn process_string(
    new_sample: &Property,
    data_source: &MetricCacheKey,
) -> Result<Option<MetricEventPayload>, Error> {
    let sampled_string = match new_sample {
        Property::String(_, val) => val.clone(),
        _ => {
            return Err(format_err!(
                concat!(
                    "Selector referenced an Inspect property specified as String",
                    " but property is not type String.",
                    " Selector: {:?}, Inspect type: {}"
                ),
                data_source,
                new_sample.discriminant_name()
            ));
        }
    };

    Ok(Some(MetricEventPayload::StringValue(sampled_string)))
}

fn process_occurence(
    new_sample: &Property,
    prev_sample_opt: Option<&Property>,
    data_source: &MetricCacheKey,
) -> Result<Option<MetricEventPayload>, Error> {
    let diff = match prev_sample_opt {
        None => compute_initial_event_count(new_sample, data_source)?,
        Some(prev_sample) => compute_event_count_diff(new_sample, prev_sample, data_source)?,
    };

    if diff < 0 {
        return Err(format_err!(
            concat!(
                "Event count must be monotonically increasing,",
                " but we observed a negative event count diff for: {:?}"
            ),
            data_source
        ));
    }

    if diff == 0 {
        return Ok(None);
    }

    // TODO(fxbug.dev/42067): Once fuchsia.cobalt is gone, we don't need to preserve
    // occurrence counts "fitting" into i64s.
    Ok(Some(MetricEventPayload::Count(diff as u64)))
}

fn compute_initial_event_count(
    new_sample: &Property,
    data_source: &MetricCacheKey,
) -> Result<i64, Error> {
    match new_sample {
        Property::Uint(_, val) => sanitize_unsigned_numerical(val.clone(), data_source),
        Property::Int(_, val) => Ok(val.clone()),
        _ => Err(format_err!(
            concat!(
                "Selector referenced an Inspect property",
                " that is not compatible with cached",
                " transformation to an event count.",
                " Selector: {:?}, {}"
            ),
            data_source,
            new_sample.discriminant_name()
        )),
    }
}

fn compute_event_count_diff(
    new_sample: &Property,
    old_sample: &Property,
    data_source: &MetricCacheKey,
) -> Result<i64, Error> {
    match (new_sample, old_sample) {
        // We don't need to validate that old_count and new_count are positive here.
        // If new_count was negative, and old_count was positive, then the diff would be
        // negative, which is an errorful state. It's impossible for old_count to be negative
        // as either it was the first sample which would make a negative diff which is an error,
        // or it was a negative new_count with a positive old_count, which we've already shown will
        // produce an errorful state.
        (Property::Int(_, new_count), Property::Int(_, old_count)) => Ok(new_count - old_count),
        (Property::Uint(_, new_count), Property::Uint(_, old_count)) => {
            // u64::MAX will cause sanitized_unsigned_numerical to build an
            // appropriate error message for a subtraction underflow.
            sanitize_unsigned_numerical(
                new_count.checked_sub(*old_count).unwrap_or(u64::MAX),
                data_source,
            )
        }
        // If we have a correctly typed new sample, but it didn't match either of the above cases,
        // this means the new sample changed types compared to the old sample. We should just
        // restart the cache, and treat the new sample as a first observation.
        (_, Property::Uint(_, _)) | (_, Property::Int(_, _)) => {
            warn!(
                "Inspect type of sampled data changed between samples. Restarting cache. {:?}",
                data_source
            );
            compute_initial_event_count(new_sample, data_source)
        }
        _ => Err(format_err!(
            concat!(
                "Inspect type of sampled data changed between samples",
                " to a type incompatible with event counters.",
                " Selector: {:?}, New type: {:?}"
            ),
            data_source,
            new_sample.discriminant_name()
        )),
    }
}

fn process_schema_errors(errors: &Option<Vec<diagnostics_data::InspectError>>, moniker: &String) {
    match errors {
        Some(errors) => {
            for error in errors {
                if !error.message.contains("Inspect hierarchy was fully filtered") {
                    warn!("Moniker: {}, Error: {:?}", moniker, error);
                }
            }
        }
        None => {
            warn!("Moniker: {} encountered null payload and no errors.", moniker);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use diagnostics_hierarchy::{hierarchy, Bucket};
    use futures::executor;

    /// Test inserting a string into the hierarchy that requires escaping.
    #[fuchsia::test]
    fn test_process_payload_with_escapes() {
        let unescaped: String = "path/to".to_string();
        let hierarchy = hierarchy! {
            root: {
                var unescaped: {
                    value: 0,
                    "value/with:escapes": 0,
                }
            }
        };

        let mut sampler = ProjectSampler {
            archive_reader: ArchiveReader::new(),
            moniker_to_selector_map: HashMap::new(),
            metrics: vec![],
            metric_cache: HashMap::new(),
            metrics_logger: None,
            poll_rate_sec: 3600,
            project_sampler_stats: Arc::new(ProjectSamplerStats::new()),
        };
        let selector: String = "my/component:root/path\\/to:value".to_string();
        sampler.metrics.push(MetricConfig {
            selectors: SelectorList(vec![sampler_config::parse_selector_for_test(&selector)]),
            metric_id: 1,
            // Occurrence type with a value of zero will not attempt to use any loggers.
            metric_type: DataType::Occurrence,
            event_codes: Vec::new(),
            // upload_once means that process_component_data will return SelectorsChanged if
            // it is found in the map.
            upload_once: Some(true),
        });
        sampler.rebuild_selector_data_structures();
        match executor::block_on(sampler.process_component_data(
            &hierarchy,
            "a_filename",
            &"my/component".to_string(),
        )) {
            // This selector will be found and removed from the map, resulting in a
            // SelectorsChanged response.
            Ok((SnapshotOutcome::SelectorsChanged, _events)) => (),
            _ => panic!("Expecting SelectorsChanged from process_component_data."),
        }

        let selector_with_escaped_property: String =
            "my/component:root/path\\/to:value\\/with\\:escapes".to_string();
        sampler.metrics.push(MetricConfig {
            selectors: SelectorList(vec![sampler_config::parse_selector_for_test(
                &selector_with_escaped_property,
            )]),
            metric_id: 1,
            // Occurrence type with a value of zero will not attempt to use any loggers.
            metric_type: DataType::Occurrence,
            event_codes: Vec::new(),
            // upload_once means that the method will return SelectorsChanged if it is found
            // in the map.
            upload_once: Some(true),
        });
        sampler.rebuild_selector_data_structures();
        match executor::block_on(sampler.process_component_data(
            &hierarchy,
            "a_filename",
            &"my/component".to_string(),
        )) {
            // This selector will be found and removed from the map, resulting in a
            // SelectorsChanged response.
            Ok((SnapshotOutcome::SelectorsChanged, _events)) => (),
            _ => panic!("Expecting SelectorsChanged from process_component_data."),
        }

        let selector_unfound: String = "my/component:root/path/to:value".to_string();
        sampler.metrics.push(MetricConfig {
            selectors: SelectorList(vec![sampler_config::parse_selector_for_test(
                &selector_unfound,
            )]),
            metric_id: 1,
            // Occurrence type with a value of zero will not attempt to use any loggers.
            metric_type: DataType::Occurrence,
            event_codes: Vec::new(),
            // upload_once means that the method will return SelectorsChanged if it is found
            // in the map.
            upload_once: Some(true),
        });
        sampler.rebuild_selector_data_structures();
        match executor::block_on(sampler.process_component_data(
            &hierarchy,
            "a_filename",
            &"my/component".to_string(),
        )) {
            // This selector will not be found and removed from the map, resulting in SelectorsUnchanged.
            Ok((SnapshotOutcome::SelectorsUnchanged, _events)) => (),
            _ => panic!("Expecting SelectorsUnchanged from process_component_data."),
        }
    }

    /// Test that a decreasing occurrence type (which is not allowed) doesn't crash due to e.g.
    /// unchecked unsigned subtraction overflow.
    #[fuchsia::test]
    fn decreasing_occurrence_is_correct() {
        let big_number = Property::Uint("foo".to_string(), 5);
        let small_number = Property::Uint("foo".to_string(), 2);
        let key = MetricCacheKey { filename: "some_file".to_string(), selector: "sel".to_string() };

        assert_eq!(
            process_sample_for_data_type(
                &big_number,
                Some(&small_number),
                &key,
                &DataType::Occurrence
            ),
            Some(MetricEventPayload::Count(3))
        );
        assert_eq!(
            process_sample_for_data_type(
                &small_number,
                Some(&big_number),
                &key,
                &DataType::Occurrence
            ),
            None
        );
    }

    /// Test removal of selectors marked with upload_once.
    #[fuchsia::test]
    fn test_upload_once() {
        let hierarchy = hierarchy! {
            root: {
                value_one: 0,
                value_two: 1,
            }
        };

        let mut sampler = ProjectSampler {
            archive_reader: ArchiveReader::new(),
            moniker_to_selector_map: HashMap::new(),
            metrics: vec![],
            metric_cache: HashMap::new(),
            metrics_logger: None,
            poll_rate_sec: 3600,
            project_sampler_stats: Arc::new(ProjectSamplerStats::new()),
        };
        sampler.metrics.push(MetricConfig {
            selectors: SelectorList(vec![sampler_config::parse_selector_for_test(
                "my/component:root:value_one",
            )]),
            metric_id: 1,
            metric_type: DataType::Integer,
            event_codes: Vec::new(),
            upload_once: Some(true),
        });
        sampler.metrics.push(MetricConfig {
            selectors: SelectorList(vec![sampler_config::parse_selector_for_test(
                "my/component:root:value_two",
            )]),
            metric_id: 2,
            metric_type: DataType::Integer,
            event_codes: Vec::new(),
            upload_once: Some(true),
        });
        sampler.rebuild_selector_data_structures();

        // Both selectors should be found and removed from the map.
        match executor::block_on(sampler.process_component_data(
            &hierarchy,
            "a_filename",
            &"my/component".to_string(),
        )) {
            Ok((SnapshotOutcome::SelectorsChanged, _events)) => (),
            _ => panic!("Expecting SelectorsChanged from process_component_data."),
        }

        let selector_indices = sampler.moniker_to_selector_map.get("my/component").unwrap();
        for index_info in selector_indices {
            let metric = &sampler.metrics[index_info.metric_index];
            let selector = &metric.selectors[index_info.selector_index];
            assert!(selector.is_none());
        }
    }

    struct EventCountTesterParams {
        new_val: Property,
        old_val: Option<Property>,
        process_ok: bool,
        event_made: bool,
        diff: i64,
    }

    fn process_occurence_tester(params: EventCountTesterParams) {
        let data_source = MetricCacheKey {
            filename: "foo.file".to_string(),
            selector: "test:root:count".to_string(),
        };
        let event_res = process_occurence(&params.new_val, params.old_val.as_ref(), &data_source);

        if !params.process_ok {
            assert!(event_res.is_err());
            return;
        }

        assert!(event_res.is_ok());

        let event_opt = event_res.unwrap();

        if !params.event_made {
            assert!(event_opt.is_none());
            return;
        }

        assert!(event_opt.is_some());
        let event = event_opt.unwrap();
        match event {
            MetricEventPayload::Count(count) => {
                assert_eq!(count, params.diff as u64);
            }
            _ => panic!("Expecting event counts."),
        }
    }

    #[fuchsia::test]
    fn test_normal_process_occurence() {
        process_occurence_tester(EventCountTesterParams {
            new_val: Property::Int("count".to_string(), 1),
            old_val: None,
            process_ok: true,
            event_made: true,
            diff: 1,
        });

        process_occurence_tester(EventCountTesterParams {
            new_val: Property::Int("count".to_string(), 1),
            old_val: Some(Property::Int("count".to_string(), 1)),
            process_ok: true,
            event_made: false,
            diff: -1,
        });

        process_occurence_tester(EventCountTesterParams {
            new_val: Property::Int("count".to_string(), 3),
            old_val: Some(Property::Int("count".to_string(), 1)),
            process_ok: true,
            event_made: true,
            diff: 2,
        });
    }

    #[fuchsia::test]
    fn test_data_type_changing_process_occurence() {
        process_occurence_tester(EventCountTesterParams {
            new_val: Property::Int("count".to_string(), 1),
            old_val: None,
            process_ok: true,
            event_made: true,
            diff: 1,
        });

        process_occurence_tester(EventCountTesterParams {
            new_val: Property::Uint("count".to_string(), 1),
            old_val: None,
            process_ok: true,
            event_made: true,
            diff: 1,
        });

        process_occurence_tester(EventCountTesterParams {
            new_val: Property::Uint("count".to_string(), 3),
            old_val: Some(Property::Int("count".to_string(), 1)),
            process_ok: true,
            event_made: true,
            diff: 3,
        });

        process_occurence_tester(EventCountTesterParams {
            new_val: Property::String("count".to_string(), "big_oof".to_string()),
            old_val: Some(Property::Int("count".to_string(), 1)),
            process_ok: false,
            event_made: false,
            diff: -1,
        });
    }

    #[fuchsia::test]
    fn test_event_count_negatives_and_overflows() {
        process_occurence_tester(EventCountTesterParams {
            new_val: Property::Int("count".to_string(), -11),
            old_val: None,
            process_ok: false,
            event_made: false,
            diff: -1,
        });

        process_occurence_tester(EventCountTesterParams {
            new_val: Property::Int("count".to_string(), 9),
            old_val: Some(Property::Int("count".to_string(), 10)),
            process_ok: false,
            event_made: false,
            diff: -1,
        });

        process_occurence_tester(EventCountTesterParams {
            new_val: Property::Uint("count".to_string(), std::u64::MAX),
            old_val: None,
            process_ok: false,
            event_made: false,
            diff: -1,
        });

        let i64_max_in_u64: u64 = std::i64::MAX.try_into().unwrap();

        process_occurence_tester(EventCountTesterParams {
            new_val: Property::Uint("count".to_string(), i64_max_in_u64 + 1),
            old_val: Some(Property::Uint("count".to_string(), 1)),
            process_ok: true,
            event_made: true,
            diff: std::i64::MAX,
        });

        process_occurence_tester(EventCountTesterParams {
            new_val: Property::Uint("count".to_string(), i64_max_in_u64 + 2),
            old_val: Some(Property::Uint("count".to_string(), 1)),
            process_ok: false,
            event_made: false,
            diff: -1,
        });
    }

    struct IntTesterParams {
        new_val: Property,
        process_ok: bool,
        sample: i64,
    }

    fn process_int_tester(params: IntTesterParams) {
        let data_source = MetricCacheKey {
            filename: "foo.file".to_string(),
            selector: "test:root:count".to_string(),
        };
        let event_res = process_int(&params.new_val, &data_source);

        if !params.process_ok {
            assert!(event_res.is_err());
            return;
        }

        assert!(event_res.is_ok());

        let event = event_res.expect("event should be Ok").expect("event should be Some");
        match event {
            MetricEventPayload::IntegerValue(val) => {
                assert_eq!(val, params.sample);
            }
            _ => panic!("Expecting event counts."),
        }
    }

    #[fuchsia::test]
    fn test_normal_process_int() {
        process_int_tester(IntTesterParams {
            new_val: Property::Int("count".to_string(), 13),
            process_ok: true,
            sample: 13,
        });

        process_int_tester(IntTesterParams {
            new_val: Property::Int("count".to_string(), -13),
            process_ok: true,
            sample: -13,
        });

        process_int_tester(IntTesterParams {
            new_val: Property::Int("count".to_string(), 0),
            process_ok: true,
            sample: 0,
        });

        process_int_tester(IntTesterParams {
            new_val: Property::Uint("count".to_string(), 13),
            process_ok: true,
            sample: 13,
        });

        process_int_tester(IntTesterParams {
            new_val: Property::String("count".to_string(), "big_oof".to_string()),
            process_ok: false,
            sample: -1,
        });
    }

    #[fuchsia::test]
    fn test_int_edge_cases() {
        process_int_tester(IntTesterParams {
            new_val: Property::Int("count".to_string(), std::i64::MAX),
            process_ok: true,
            sample: std::i64::MAX,
        });

        process_int_tester(IntTesterParams {
            new_val: Property::Int("count".to_string(), std::i64::MIN),
            process_ok: true,
            sample: std::i64::MIN,
        });

        let i64_max_in_u64: u64 = std::i64::MAX.try_into().unwrap();

        process_int_tester(IntTesterParams {
            new_val: Property::Uint("count".to_string(), i64_max_in_u64),
            process_ok: true,
            sample: std::i64::MAX,
        });

        process_int_tester(IntTesterParams {
            new_val: Property::Uint("count".to_string(), i64_max_in_u64 + 1),
            process_ok: false,
            sample: -1,
        });
    }

    struct StringTesterParams {
        sample: Property,
        process_ok: bool,
        previous_sample: Option<Property>,
    }

    fn process_string_tester(params: StringTesterParams) {
        let metric_cache_key = MetricCacheKey {
            filename: "foo.file".to_string(),
            selector: "test:root:string_val".to_string(),
        };

        let event = process_sample_for_data_type(
            &params.sample,
            params.previous_sample.as_ref(),
            &metric_cache_key,
            &DataType::String,
        );

        if !params.process_ok {
            assert!(event.is_none());
            return;
        }

        match event.unwrap() {
            MetricEventPayload::StringValue(val) => {
                assert_eq!(val.as_str(), params.sample.string().unwrap());
            }
            _ => panic!("Expecting event with StringValue."),
        }
    }

    #[fuchsia::test]
    fn test_process_string() {
        process_string_tester(StringTesterParams {
            sample: Property::String("string_val".to_string(), "Hello, world!".to_string()),
            process_ok: true,
            previous_sample: None,
        });

        // Ensure any erroneously cached values are ignored (a warning is logged in this case).

        process_string_tester(StringTesterParams {
            sample: Property::String("string_val".to_string(), "Hello, world!".to_string()),
            process_ok: true,
            previous_sample: Some(Property::String("string_val".to_string(), "Uh oh!".to_string())),
        });

        // Ensure unsupported property types are not erroneously processed.

        process_string_tester(StringTesterParams {
            sample: Property::Int("string_val".to_string(), 123),
            process_ok: false,
            previous_sample: None,
        });

        process_string_tester(StringTesterParams {
            sample: Property::Uint("string_val".to_string(), 123),
            process_ok: false,
            previous_sample: None,
        });
    }

    fn create_inspect_bucket_vec<T: Copy>(hist: Vec<T>) -> Vec<Bucket<T>> {
        hist.iter()
            .map(|val| Bucket {
                // Cobalt doesn't use the Inspect floor and ceiling, so
                // lets use val for them since its the only thing available
                // with type T.
                floor: *val,
                ceiling: *val,
                count: *val,
            })
            .collect()
    }
    fn convert_vector_to_int_histogram(hist: Vec<i64>) -> Property<String> {
        let bucket_vec = create_inspect_bucket_vec::<i64>(hist);

        Property::IntArray("Bloop".to_string(), ArrayContent::Buckets(bucket_vec))
    }

    fn convert_vector_to_uint_histogram(hist: Vec<u64>) -> Property<String> {
        let bucket_vec = create_inspect_bucket_vec::<u64>(hist);

        Property::UintArray("Bloop".to_string(), ArrayContent::Buckets(bucket_vec))
    }

    struct IntHistogramTesterParams {
        new_val: Property,
        old_val: Option<Property>,
        process_ok: bool,
        event_made: bool,
        diff: Vec<(u32, u64)>,
    }
    fn process_int_histogram_tester(params: IntHistogramTesterParams) {
        let data_source = MetricCacheKey {
            filename: "foo.file".to_string(),
            selector: "test:root:count".to_string(),
        };
        let event_res =
            process_int_histogram(&params.new_val, params.old_val.as_ref(), &data_source);

        if !params.process_ok {
            assert!(event_res.is_err());
            return;
        }

        assert!(event_res.is_ok());

        let event_opt = event_res.unwrap();
        if !params.event_made {
            assert!(event_opt.is_none());
            return;
        }

        assert!(event_opt.is_some());

        let event = event_opt.unwrap();
        match event.clone() {
            MetricEventPayload::Histogram(histogram_buckets) => {
                assert_eq!(histogram_buckets.len(), params.diff.len());

                let expected_histogram_buckets = params
                    .diff
                    .iter()
                    .map(|(index, count)| HistogramBucket { index: *index, count: *count })
                    .collect::<Vec<HistogramBucket>>();

                assert_eq!(histogram_buckets, expected_histogram_buckets);
            }
            _ => panic!("Expecting int histogram."),
        }
    }

    /// Test that simple in-bounds first-samples of both types of Inspect histograms
    /// produce correct event types.
    #[fuchsia::test]
    fn test_normal_process_int_histogram() {
        let new_i64_sample = convert_vector_to_int_histogram(vec![1, 1, 1, 1]);
        let new_u64_sample = convert_vector_to_uint_histogram(vec![1, 1, 1, 1]);

        process_int_histogram_tester(IntHistogramTesterParams {
            new_val: new_i64_sample,
            old_val: None,
            process_ok: true,
            event_made: true,
            diff: vec![(0, 1), (1, 1), (2, 1), (3, 1)],
        });

        process_int_histogram_tester(IntHistogramTesterParams {
            new_val: new_u64_sample,
            old_val: None,
            process_ok: true,
            event_made: true,
            diff: vec![(0, 1), (1, 1), (2, 1), (3, 1)],
        });

        // Test an Inspect uint histogram at the boundaries of the type produce valid
        // cobalt events.
        let new_u64_sample = convert_vector_to_uint_histogram(vec![u64::MAX, u64::MAX, u64::MAX]);
        process_int_histogram_tester(IntHistogramTesterParams {
            new_val: new_u64_sample,
            old_val: None,
            process_ok: true,
            event_made: true,
            diff: vec![(0, u64::MAX), (1, u64::MAX), (2, u64::MAX)],
        });

        // Test that an empty Inspect histogram produces no event.
        let new_u64_sample = convert_vector_to_uint_histogram(Vec::new());
        process_int_histogram_tester(IntHistogramTesterParams {
            new_val: new_u64_sample,
            old_val: None,
            process_ok: true,
            event_made: false,
            diff: Vec::new(),
        });

        let new_u64_sample = convert_vector_to_uint_histogram(vec![0, 0, 0, 0]);
        process_int_histogram_tester(IntHistogramTesterParams {
            new_val: new_u64_sample,
            old_val: None,
            process_ok: true,
            event_made: false,
            diff: Vec::new(),
        });

        // Test that monotonically increasing histograms are good!.
        let new_u64_sample = convert_vector_to_uint_histogram(vec![2, 1, 2, 1]);
        let old_u64_sample = Some(convert_vector_to_uint_histogram(vec![1, 1, 0, 1]));
        process_int_histogram_tester(IntHistogramTesterParams {
            new_val: new_u64_sample,
            old_val: old_u64_sample,
            process_ok: true,
            event_made: true,
            diff: vec![(0, 1), (2, 2)],
        });

        let new_i64_sample = convert_vector_to_int_histogram(vec![5, 2, 1, 3]);
        let old_i64_sample = Some(convert_vector_to_int_histogram(vec![1, 1, 1, 1]));
        process_int_histogram_tester(IntHistogramTesterParams {
            new_val: new_i64_sample,
            old_val: old_i64_sample,
            process_ok: true,
            event_made: true,
            diff: vec![(0, 4), (1, 1), (3, 2)],
        });

        // Test that changing the histogram type resets the cache.
        let new_u64_sample = convert_vector_to_uint_histogram(vec![2, 1, 1, 1]);
        let old_i64_sample = Some(convert_vector_to_int_histogram(vec![1, 1, 1, 1]));
        process_int_histogram_tester(IntHistogramTesterParams {
            new_val: new_u64_sample,
            old_val: old_i64_sample,
            process_ok: true,
            event_made: true,
            diff: vec![(0, 2), (1, 1), (2, 1), (3, 1)],
        });
    }

    #[fuchsia::test]
    fn test_errorful_process_int_histogram() {
        // Test that changing the histogram length is an error.
        let new_u64_sample = convert_vector_to_uint_histogram(vec![1, 1, 1, 1]);
        let old_u64_sample = Some(convert_vector_to_uint_histogram(vec![1, 1, 1, 1, 1]));
        process_int_histogram_tester(IntHistogramTesterParams {
            new_val: new_u64_sample,
            old_val: old_u64_sample,
            process_ok: false,
            event_made: false,
            diff: Vec::new(),
        });

        // Test that new samples cant have negative values.
        let new_i64_sample = convert_vector_to_int_histogram(vec![1, 1, -1, 1]);
        process_int_histogram_tester(IntHistogramTesterParams {
            new_val: new_i64_sample,
            old_val: None,
            process_ok: false,
            event_made: false,
            diff: Vec::new(),
        });

        // Test that histograms must be monotonically increasing.
        let new_i64_sample = convert_vector_to_int_histogram(vec![5, 2, 1, 3]);
        let old_i64_sample = Some(convert_vector_to_int_histogram(vec![6, 1, 1, 1]));
        process_int_histogram_tester(IntHistogramTesterParams {
            new_val: new_i64_sample,
            old_val: old_i64_sample,
            process_ok: false,
            event_made: false,
            diff: Vec::new(),
        });
    }

    /// Ensure that data distinguished only by metadata-filename - with the same moniker and
    /// selector path - is kept properly separate in the previous-value cache. The same
    /// MetricConfig should match each data source, but the occurrence counts
    /// should reflect that the distinct values are individually tracked.
    #[fuchsia::test]
    async fn test_filename_distinguishes_data() {
        let mut sampler = ProjectSampler {
            archive_reader: ArchiveReader::new(),
            moniker_to_selector_map: HashMap::new(),
            metrics: vec![],
            metric_cache: HashMap::new(),
            metrics_logger: None,
            poll_rate_sec: 3600,
            project_sampler_stats: Arc::new(ProjectSamplerStats::new()),
        };
        let selector: String = "my/component:root/branch:leaf".to_string();
        let metric_id = 1;
        let event_codes = vec![];
        sampler.metrics.push(MetricConfig {
            selectors: SelectorList(vec![sampler_config::parse_selector_for_test(&selector)]),
            metric_id,
            metric_type: DataType::Occurrence,
            event_codes,
            upload_once: Some(false),
        });
        sampler.rebuild_selector_data_structures();

        let file1_value4 = vec![Data::for_inspect(
            "my/component",
            Some(hierarchy! { root: {branch: {leaf: 4i32}}}),
            0, /* timestamp */
            "component-url",
            "file1",
            vec![], /* errors */
        )];
        let file2_value3 = vec![Data::for_inspect(
            "my/component",
            Some(hierarchy! { root: {branch: {leaf: 3i32}}}),
            0, /* timestamp */
            "component-url",
            "file2",
            vec![], /* errors */
        )];
        let file1_value6 = vec![Data::for_inspect(
            "my/component",
            Some(hierarchy! { root: {branch: {leaf: 6i32}}}),
            0, /* timestamp */
            "component-url",
            "file1",
            vec![], /* errors */
        )];
        let file2_value8 = vec![Data::for_inspect(
            "my/component",
            Some(hierarchy! { root: {branch: {leaf: 8i32}}}),
            0, /* timestamp */
            "component-url",
            "file2",
            vec![], /* errors */
        )];

        fn expect_one_metric_event_value(
            events: Result<Vec<EventToLog>, Error>,
            value: u64,
            context: &'static str,
        ) {
            let events = events.expect(context);
            assert_eq!(events.len(), 1, "Events len not 1: {}: {}", context, events.len());
            let event = &events[0];
            let EventToLog { payload, .. } = event;
            if let fidl_fuchsia_metrics::MetricEventPayload::Count(payload) = payload {
                assert_eq!(
                    payload, &value,
                    "Wrong payload, expected {} got {} at {}",
                    value, payload, context
                );
            } else {
                panic!("Expected MetricEventPayload::Count at {}, got {:?}", context, payload);
            }
        }

        expect_one_metric_event_value(sampler.process_snapshot(file1_value4).await, 4, "first");
        expect_one_metric_event_value(sampler.process_snapshot(file2_value3).await, 3, "second");
        expect_one_metric_event_value(sampler.process_snapshot(file1_value6).await, 2, "third");
        expect_one_metric_event_value(sampler.process_snapshot(file2_value8).await, 5, "fourth");
    }
}
