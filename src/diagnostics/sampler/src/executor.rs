// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        config::{DataType, MetricConfig, ProjectConfig, SamplerConfig},
        diagnostics::*,
    },
    anyhow::{format_err, Context, Error},
    diagnostics_data,
    diagnostics_hierarchy::{ArrayContent, DiagnosticsHierarchy, Property},
    diagnostics_reader::{ArchiveReader, Inspect},
    fidl_fuchsia_cobalt::{
        CobaltEvent, CountEvent, EventPayload, HistogramBucket as CobaltHistogramBucket,
        LoggerFactoryMarker, LoggerFactoryProxy, LoggerProxy,
    },
    fidl_fuchsia_metrics::{
        HistogramBucket, MetricEvent, MetricEventLoggerFactoryMarker,
        MetricEventLoggerFactoryProxy, MetricEventLoggerProxy, MetricEventPayload, ProjectSpec,
    },
    fuchsia_async::{self as fasync, futures::StreamExt},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_inspect::{self as inspect, NumericProperty},
    fuchsia_inspect_derive::WithInspect,
    fuchsia_zircon as zx,
    futures::{channel::oneshot, future::join_all, select, stream::FuturesUnordered},
    itertools::Itertools,
    log::{error, info, warn},
    std::{
        collections::HashMap,
        convert::{TryFrom, TryInto},
        sync::Arc,
    },
};

pub struct TaskCancellation {
    senders: Vec<oneshot::Sender<()>>,
    _sampler_executor_stats: Arc<SamplerExecutorStats>,
    execution_context: fasync::Task<Vec<ProjectSampler>>,
}

impl TaskCancellation {
    // It's possible the reboot register goes down. If that
    // happens, we want to continue driving execution with
    // no consideration for cancellation. This does that.
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
        let mut project_map: HashMap<String, Vec<usize>> = HashMap::new();

        for (index, project_sampler) in project_samplers.iter().enumerate() {
            for selector in project_sampler.metric_transformation_map.keys() {
                let relevant_projects = project_map.entry(selector.clone()).or_insert(Vec::new());
                (*relevant_projects).push(index);
            }
        }

        let mondo_selectors_set = project_map.keys().cloned();

        let reboot_snapshot_reader =
            ArchiveReader::new().retry_if_empty(false).add_selectors(mondo_selectors_set);

        let mut reboot_processor = RebootSnapshotProcessor {
            reader: reboot_snapshot_reader,
            project_samplers,
            project_map,
        };
        // Log errors encountered in final snapshot, but always swallow errors so we can gracefully
        // notify RebootMethodsWatcherRegister that we yield our remaining time.
        reboot_processor
            .execute_reboot_sample()
            .await
            .unwrap_or_else(|e| warn!("Reboot snapshot failed! {:?}", e));
    }
}

struct RebootSnapshotProcessor {
    // Reader constructed from the union of selectors
    // for every ProjectSampler config.
    reader: ArchiveReader,
    // Vector of mutable ProjectSamplers that will
    // process their final samples.
    project_samplers: Vec<ProjectSampler>,
    // Mapping from a selector to a vector of indices into
    // the project_samplers, where each ProjectSampler has
    // an active config for that selector.
    project_map: HashMap<String, Vec<usize>>,
}

impl RebootSnapshotProcessor {
    pub async fn execute_reboot_sample(&mut self) -> Result<(), Error> {
        let snapshot_data = self.reader.snapshot::<Inspect>().await?;
        for data_packet in snapshot_data {
            let moniker = data_packet.moniker;
            match data_packet.payload {
                None => {
                    process_schema_errors(&data_packet.metadata.errors, &moniker);
                }
                Some(payload) => self.process_single_payload(payload, &moniker).await,
            }
        }
        Ok(())
    }

    async fn process_single_payload(
        &mut self,
        hierarchy: DiagnosticsHierarchy<String>,
        moniker: &String,
    ) {
        // The property iterator will visit empty nodes once,
        // with a None property type. Skip those by filtering None props.
        for (hierarchy_path, property) in
            hierarchy.property_iter().filter(|(_, property)| property.is_some())
        {
            let new_sample = property.expect("Filtered out all None props already.");
            let selector =
                format!("{}:{}:{}", moniker, hierarchy_path.iter().join("/"), new_sample.key());

            // Get the vector of ints mapping this selector to all project samplers
            // that are configured to this metric.
            match self.project_map.get(&selector) {
                None => {
                    warn!(
                        concat!(
                            "Reboot snapshot retrieved",
                            " a property that no project samplers claim",
                            " to need... {:?}"
                        ),
                        selector
                    );
                }
                Some(project_sampler_indices) => {
                    // For each ProjectSampler that is configured with this property,
                    // use the most recent sample to do one final processing.
                    // TODO(42067): Should we do these async?
                    for project_sampler_index in project_sampler_indices {
                        self.project_samplers
                            .get_mut(*project_sampler_index)
                            .context("Index guaranteed to be present.")
                            .unwrap()
                            .process_newly_sampled_property(&selector, new_sample)
                            .await
                            .unwrap_or_else(|e| {
                                // If processing the final sample failed, just log the
                                // error and proceed, everything's getting shut down soon
                                // anyways.
                                warn!(
                                    concat!(
                                        "A project sampler failed to",
                                        " process a reboot sample: {:?}"
                                    ),
                                    e
                                )
                            });
                    }
                }
            }
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
    /// to ProjectSampler plans.
    pub async fn new(sampler_config: SamplerConfig) -> Result<Self, Error> {
        let logger_factory: Arc<LoggerFactoryProxy> = Arc::new(
            connect_to_protocol::<LoggerFactoryMarker>()
                .context("Failed to connect to the Cobalt LoggerFactory")?,
        );

        let metric_logger_factory: Arc<MetricEventLoggerFactoryProxy> = Arc::new(
            connect_to_protocol::<MetricEventLoggerFactoryMarker>()
                .context("Failed to connect to the Metric LoggerFactory")?,
        );

        let minimum_sample_rate_sec = sampler_config.minimum_sample_rate_sec;

        let sampler_executor_stats = Arc::new(
            SamplerExecutorStats::new()
                .with_inspect(inspect::component::inspector().root(), "sampler_executor_stats")
                .unwrap_or_else(|e| {
                    warn!(
                        concat!("Failed to attach inspector to SamplerExecutorStats struct: {:?}",),
                        e
                    );
                    SamplerExecutorStats::default()
                }),
        );

        sampler_executor_stats
            .total_project_samplers_configured
            .add(sampler_config.project_configs.len() as u64);

        let mut project_to_stats_map: HashMap<u32, Arc<ProjectSamplerStats>> = HashMap::new();

        // TODO(42067): Create only one ArchiveReader for each unique poll rate so we
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
                            .unwrap_or_else(|e| {
                                warn!(
                                    concat!(
                                "Failed to attach inspector to ProjectSamplerStats struct: {:?}",
                            ),
                                    e
                                );
                                ProjectSamplerStats::default()
                            }),
                    ));

                ProjectSampler::new(
                    project_config,
                    logger_factory.clone(),
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

    /// Turn each ProjectSampler plan into an fasync::Task which executes its associated plan,
    /// and process errors if any tasks exit unexpectedly.
    pub fn execute(self) -> TaskCancellation {
        // Take ownership of the inspect struct so we can give it to the execution context. We do this
        // so that the execution context can return the struct when its halted by reboot, which allows inspect
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
                        // TODO(42067): Consider restarting the failed sampler depending on
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
    // Mapping from selector to the metric configs for that selector. Allows
    // for iteration over returned diagnostics schemas to drive transformations
    // with constant transformation metadata lookup.
    metric_transformation_map: HashMap<String, MetricConfig>,
    // Cache from Inspect selector to last sampled property.
    metric_cache: HashMap<String, Property>,
    // Cobalt logger proxy using this ProjectSampler's project id.
    cobalt_logger: Option<LoggerProxy>,
    // fuchsia.metrics logger proxy using this ProjectSampler's project id.
    metrics_logger: Option<MetricEventLoggerProxy>,
    // The frequency with which we snapshot Inspect properties
    // for this project.
    poll_rate_sec: i64,
    // Inspect stats on a node namespaced by this project's associated id.
    // It's an arc since a single project can have multiple samplers at
    // different frequencies, but we want a single project to have one node.
    project_sampler_stats: Arc<ProjectSamplerStats>,
}

pub enum ProjectSamplerTaskExit {
    // The project sampler processed a
    // reboot signal on its oneshot and
    // yielded to the final-snapshot.
    RebootTriggered(ProjectSampler),
    // The project sampler has no more
    // work to complete; perhaps all
    // metrics were "upload_once"?
    WorkCompleted,
}

pub enum ProjectSamplerEvent {
    TimerTriggered,
    TimerDied,
    RebootTriggered,
    RebootChannelClosed(Error),
}
impl ProjectSampler {
    pub async fn new(
        config: ProjectConfig,
        cobalt_logger_factory: Arc<LoggerFactoryProxy>,
        metric_logger_factory: Arc<MetricEventLoggerFactoryProxy>,
        minimum_sample_rate_sec: i64,
        project_sampler_stats: Arc<ProjectSamplerStats>,
    ) -> Result<ProjectSampler, Error> {
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

        let mut cobalt_logged: i64 = 0;
        let mut metrics_logged: i64 = 0;
        let metric_transformation_map = config
            .metrics
            .into_iter()
            .map(|metric_config| {
                if metric_config.use_legacy_cobalt.unwrap_or(false) {
                    cobalt_logged += 1;
                } else {
                    metrics_logged += 1;
                };
                (metric_config.selector.clone(), metric_config)
            })
            .collect::<HashMap<String, MetricConfig>>();

        project_sampler_stats.project_sampler_count.add(1);
        project_sampler_stats.metrics_configured.add(metric_transformation_map.len() as u64);

        let cobalt_logger = if cobalt_logged > 0 {
            let (cobalt_logger_proxy, cobalt_server_end) =
                fidl::endpoints::create_proxy().context("Failed to create endpoints")?;
            cobalt_logger_factory
                .create_logger_from_project_id(project_id, cobalt_server_end)
                .await?;
            Some(cobalt_logger_proxy)
        } else {
            None
        };

        let metrics_logger = if metrics_logged > 0 {
            let (metrics_logger_proxy, metrics_server_end) =
                fidl::endpoints::create_proxy().context("Failed to create endpoints")?;
            let mut project_spec = ProjectSpec::EMPTY;
            project_spec.project_id = Some(project_id);
            metric_logger_factory
                .create_metric_event_logger(project_spec, metrics_server_end)
                .await?;
            Some(metrics_logger_proxy)
        } else {
            None
        };

        Ok(ProjectSampler {
            archive_reader: ArchiveReader::new()
                .retry_if_empty(false)
                .add_selectors(metric_transformation_map.keys().cloned()),
            metric_transformation_map,
            metric_cache: HashMap::new(),
            cobalt_logger,
            metrics_logger,
            poll_rate_sec,
            project_sampler_stats,
        })
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
                        // TODO(42067): Consider differentiating errors if
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
                        // If the next snapshot was processed and we
                        // returned Ok(false), we know that this sampler
                        // no longer needs to run (perhaps all metrics were
                        // "upload_once"?). If this is the case, we want to be
                        // sure that it is not included in the reboot-workload.
                        if !self.process_next_snapshot().await? {
                            return Ok(ProjectSamplerTaskExit::WorkCompleted);
                        }
                    }
                }
            }
        })
    }

    // Ok(true) implies that the snapshot was processed and we should sample again.
    // Ok(false) implies that the snapshot was processed and the project sampler can now stop.
    // Err implies an error was reached while processing the sample.
    async fn process_next_snapshot(&mut self) -> Result<bool, Error> {
        let snapshot_data = self.archive_reader.snapshot::<Inspect>().await?;
        for data_packet in snapshot_data {
            let moniker = data_packet.moniker;
            match data_packet.payload {
                None => process_schema_errors(&data_packet.metadata.errors, &moniker),
                Some(payload) => {
                    for (hierarchy_path, property) in payload.property_iter() {
                        // The property iterator will visit empty nodes once,
                        // with a None property type. Skip those.
                        if let Some(new_sample) = property {
                            let selector = format!(
                                "{}:{}:{}",
                                moniker,
                                hierarchy_path.iter().join("/"),
                                new_sample.key()
                            );

                            self.process_newly_sampled_property(&selector, new_sample).await?;

                            // The map is empty, break early, even if there were another
                            // property there would be nothing to transform.
                            if self.metric_transformation_map.is_empty() {
                                return Ok(false);
                            }
                        }
                    }
                }
            }
        }

        Ok(true)
    }

    // Process a single newly sampled diagnostics property, and update state accordingly.
    async fn process_newly_sampled_property(
        &mut self,
        selector: &String,
        new_sample: &Property,
    ) -> Result<(), Error> {
        let metric_transformation_opt = self.metric_transformation_map.get(selector);
        match metric_transformation_opt {
            None => {
                warn!(concat!(
                    "A property was returned by the",
                    " diagnostics snapshot, which wasn't",
                    " requested by the client."
                ));
            }
            Some(metric_transformation) => {
                // Rust is scared that the sample processors require mutable
                // references to self, despite us using the values gathered
                // before the potential mutability, after. These values
                // won't change during the sample processing, but we do this to
                // appease the borrow checker.
                let metric_type = metric_transformation.metric_type.clone();
                let metric_id = metric_transformation.metric_id.clone();
                let event_codes = metric_transformation.event_codes.clone();
                let upload_once = metric_transformation.upload_once.clone();
                let use_legacy_logger =
                    metric_transformation.use_legacy_cobalt.clone() == Some(true);

                self.process_metric_transformation(
                    metric_type,
                    metric_id,
                    event_codes,
                    selector.clone(),
                    new_sample,
                    use_legacy_logger,
                )
                .await?;

                if let Some(true) = upload_once {
                    self.metric_transformation_map.remove(selector);

                    // If the metric transformation map is empty, there's no
                    // more work to be done.
                    if !self.metric_transformation_map.is_empty() {
                        // Update archive reader since we've removed
                        // a selector.
                        self.archive_reader = ArchiveReader::new()
                            .retry_if_empty(false)
                            .add_selectors(self.metric_transformation_map.keys().cloned());
                    }
                }
            }
        }
        Ok(())
    }

    async fn process_metric_transformation(
        &mut self,
        metric_type: DataType,
        metric_id: u32,
        event_codes: Vec<u32>,
        selector: String,
        new_sample: &Property,
        use_legacy_logger: bool,
    ) -> Result<(), Error> {
        let previous_sample_opt: Option<&Property> = self.metric_cache.get(&selector);

        if let Some(payload) =
            process_sample_for_data_type(new_sample, previous_sample_opt, &selector, &metric_type)
        {
            self.maybe_update_cache(new_sample, &metric_type, selector);

            if use_legacy_logger {
                let transformed_payload: EventPayload =
                    transform_metrics_payload_to_cobalt(payload);
                let mut cobalt_event = CobaltEvent {
                    metric_id,
                    event_codes,
                    payload: transformed_payload,
                    component: None,
                };
                self.cobalt_logger.as_ref().unwrap().log_cobalt_event(&mut cobalt_event).await?;
            } else {
                let mut metric_events = vec![MetricEvent { metric_id, event_codes, payload }];
                // Note: The MetricEvent vector can't be marked send because it
                // is a dyn object stream and rust can't confirm that it doesn't have handles. This
                // is fine because we don't actually need to "send" to make the API call. But if we chain
                // the creation of the future with the await on the future, rust interperets all variables
                // including the reference to the event vector as potentially being needed across the await.
                // So we have to split the creation of the future out from the await on the future. :(
                let log_future = self
                    .metrics_logger
                    .as_ref()
                    .unwrap()
                    .log_metric_events(&mut metric_events.iter_mut());

                log_future.await?;
            }
            self.project_sampler_stats.cobalt_logs_sent.add(1);
        }

        Ok(())
    }

    fn maybe_update_cache(
        &mut self,
        new_sample: &Property,
        data_type: &DataType,
        selector: String,
    ) {
        match data_type {
            DataType::Occurrence | DataType::IntHistogram => {
                self.metric_cache.insert(selector.clone(), new_sample.clone());
            }
            DataType::Integer => (),
        }
    }
}

// This is only called for Cobalt 1.0 metrics.
fn transform_metrics_payload_to_cobalt(payload: MetricEventPayload) -> EventPayload {
    match payload {
        MetricEventPayload::Count(count) => {
            // Safe to unwrap because we use cobalt v1.0 sanitization when constructing the Count metric event payload.
            EventPayload::EventCount(CountEvent {
                count: count.try_into().unwrap(),
                period_duration_micros: 0,
            })
        }
        // Cobalt 1.0 doesn't have Integer values, MEMORY_USED is the closest approximation.
        MetricEventPayload::IntegerValue(value) => EventPayload::MemoryBytesUsed(value),
        MetricEventPayload::Histogram(hist) => {
            let legacy_histogram = hist
                .into_iter()
                .map(|metric_bucket| CobaltHistogramBucket {
                    index: metric_bucket.index,
                    count: metric_bucket.count,
                })
                .collect();
            EventPayload::IntHistogram(legacy_histogram)
        }
        _ => unreachable!("We only support count, int, and histogram"),
    }
}

fn process_sample_for_data_type(
    new_sample: &Property,
    previous_sample_opt: Option<&Property>,
    selector: &String,
    data_type: &DataType,
) -> Option<MetricEventPayload> {
    let event_payload_res = match data_type {
        DataType::Occurrence => process_occurence(new_sample, previous_sample_opt, selector),
        DataType::IntHistogram => process_int_histogram(new_sample, previous_sample_opt, selector),
        DataType::Integer => {
            // If we previously cached a metric with an int-type, log a warning and ignore it.
            // This may be a case of using a single selector for two metrics, one event count
            // and one int.
            if previous_sample_opt.is_some() {
                error!("Sampler has erroneously cached an Int type metric: {:?}", selector);
            }
            process_int(new_sample, selector)
        }
    };

    match event_payload_res {
        Ok(payload_opt) => payload_opt,
        Err(e) => {
            warn!(concat!("Failed to process Inspect property for cobalt: {:?}"), e);
            None
        }
    }
}

// It's possible for Inspect numerical properties to experience overflows/conversion
// errors when being mapped to cobalt types. Sanitize these numericals, and provide
// meaningful errors.
fn sanitize_unsigned_numerical(diff: u64, selector: &str) -> Result<i64, Error> {
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
                selector,
                e
            ));
        }
    }
}

fn process_int_histogram(
    new_sample: &Property,
    prev_sample_opt: Option<&Property>,
    selector: &String,
) -> Result<Option<MetricEventPayload>, Error> {
    let diff = match prev_sample_opt {
        None => convert_inspect_histogram_to_cobalt_histogram(new_sample, selector)?,
        Some(prev_sample) => {
            // If the data type changed then we just reset the cache.
            if std::mem::discriminant(new_sample) == std::mem::discriminant(prev_sample) {
                compute_histogram_diff(new_sample, prev_sample, selector)?
            } else {
                convert_inspect_histogram_to_cobalt_histogram(new_sample, selector)?
            }
        }
    };

    if diff.iter().any(|v| v.count != 0) {
        Ok(Some(MetricEventPayload::Histogram(diff)))
    } else {
        Ok(None)
    }
}

fn compute_histogram_diff(
    new_sample: &Property,
    old_sample: &Property,
    selector: &String,
) -> Result<Vec<HistogramBucket>, Error> {
    let new_histogram_buckets =
        convert_inspect_histogram_to_cobalt_histogram(new_sample, selector)?;
    let old_histogram_buckets =
        convert_inspect_histogram_to_cobalt_histogram(old_sample, selector)?;

    if old_histogram_buckets.len() != new_histogram_buckets.len() {
        return Err(format_err!(
            concat!(
                "Selector referenced an Inspect IntArray",
                " that was specified as an IntHistogram type ",
                " but the histogram bucket count changed between",
                " samples, which is incompatible with Cobalt.",
                " Selector: {:?}, Inspect type: {}"
            ),
            selector,
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
                        " but atleast one bucket saw the count decrease",
                        " between samples, which is incompatible with Cobalt's",
                        " need for monotonically increasing counts.",
                        " Selector: {:?}, Inspect type: {}"
                    ),
                    selector,
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
    selector: &String,
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
                    selector,
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
                        selector,
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
            // TODO(42067): Does cobalt support floors or step counts that are
            // not ints? if so, we can support that as well with double arrays if the
            // actual counts are whole numbers.
            return Err(format_err!(
                concat!(
                    "Selector referenced an Inspect property",
                    " that was specified as an IntHistogram type ",
                    " but is unable to be encoded in a cobalt HistogramBucket",
                    " vector. Selector: {:?}, Inspect type: {}"
                ),
                selector,
                inspect_histogram.discriminant_name()
            ));
        }
    }
}

fn process_int(
    new_sample: &Property,
    selector: &String,
) -> Result<Option<MetricEventPayload>, Error> {
    let sampled_int = match new_sample {
        Property::Uint(_, val) => sanitize_unsigned_numerical(val.clone(), selector)?,
        Property::Int(_, val) => val.clone(),
        _ => {
            return Err(format_err!(
                concat!(
                    "Selector referenced an Inspect property",
                    " that was specified as an Int type ",
                    " but is unable to be encoded in an i64",
                    " Selector: {:?}, Inspect type: {}"
                ),
                selector,
                new_sample.discriminant_name()
            ));
        }
    };

    Ok(Some(MetricEventPayload::IntegerValue(sampled_int)))
}

fn process_occurence(
    new_sample: &Property,
    prev_sample_opt: Option<&Property>,
    selector: &String,
) -> Result<Option<MetricEventPayload>, Error> {
    let diff = match prev_sample_opt {
        None => compute_initial_event_count(new_sample, selector)?,
        Some(prev_sample) => compute_event_count_diff(new_sample, prev_sample, selector)?,
    };

    if diff < 0 {
        return Err(format_err!(
            concat!(
                "Event count must be monotonically increasing,",
                " but we observed a negative event count diff for: {:?}"
            ),
            selector
        ));
    }

    if diff == 0 {
        return Ok(None);
    }

    // TODO(42067): Once fuchsia.cobalt is gone, we don't need to preserve
    // occurence counts "fitting" into i64s.
    Ok(Some(MetricEventPayload::Count(diff as u64)))
}

fn compute_initial_event_count(new_sample: &Property, selector: &String) -> Result<i64, Error> {
    match new_sample {
        Property::Uint(_, val) => sanitize_unsigned_numerical(val.clone(), selector),
        Property::Int(_, val) => Ok(val.clone()),
        _ => Err(format_err!(
            concat!(
                "Selector referenced an Inspect property",
                " that is not compatible with cached",
                " transformation to an event count.",
                " Selector: {:?}, {}"
            ),
            selector,
            new_sample.discriminant_name()
        )),
    }
}

fn compute_event_count_diff(
    new_sample: &Property,
    old_sample: &Property,
    selector: &String,
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
            sanitize_unsigned_numerical(new_count - old_count, selector)
        }
        // If we have a correctly typed new sample, but it didn't match either of the above cases,
        // this means the new sample changed types compared to the old sample. We should just
        // restart the cache, and treat the new sample as a first observation.
        (_, Property::Uint(_, _)) | (_, Property::Int(_, _)) => {
            warn!(
                "Inspect type of sampled data changed between samples. Restarting cache. {}",
                selector
            );
            compute_initial_event_count(new_sample, selector)
        }
        _ => Err(format_err!(
            concat!(
                "Inspect type of sampled data changed between samples",
                " to a type incompatible with event counters.",
                " Selector: {:?}, New type: {:?}"
            ),
            selector,
            new_sample.discriminant_name()
        )),
    }
}

fn process_schema_errors(errors: &Option<Vec<diagnostics_data::Error>>, moniker: &String) {
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
    use diagnostics_hierarchy::Bucket;

    struct EventCountTesterParams {
        new_val: Property,
        old_val: Option<Property>,
        process_ok: bool,
        event_made: bool,
        diff: i64,
    }

    fn process_occurence_tester(params: EventCountTesterParams) {
        let selector: String = "test:root:count".to_string();
        let event_res = process_occurence(&params.new_val, params.old_val.as_ref(), &selector);

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

        let transformed_event = transform_metrics_payload_to_cobalt(event);
        match transformed_event {
            EventPayload::EventCount(count_event) => {
                assert_eq!(count_event.count, params.diff);
            }
            _ => panic!("Expecting count events."),
        }
    }

    #[test]
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

    #[test]
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

    #[test]
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
        let selector: String = "test:root:count".to_string();
        let event_res = process_int(&params.new_val, &selector);

        if !params.process_ok {
            assert!(event_res.is_err());
            return;
        }

        assert!(event_res.is_ok());

        let event_opt = event_res.unwrap();
        assert!(event_opt.is_some());
        let event = event_opt.unwrap();
        match event {
            MetricEventPayload::IntegerValue(val) => {
                assert_eq!(val, params.sample);
            }
            _ => panic!("Expecting event counts."),
        }

        let transformed_event = transform_metrics_payload_to_cobalt(event);
        match transformed_event {
            EventPayload::MemoryBytesUsed(value) => {
                assert_eq!(value, params.sample);
            }
            _ => panic!("Expecting count events."),
        }
    }
    #[test]
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

    #[test]
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
        diff: Vec<u64>,
    }
    fn process_int_histogram_tester(params: IntHistogramTesterParams) {
        let selector: String = "test:root:count".to_string();
        let event_res = process_int_histogram(&params.new_val, params.old_val.as_ref(), &selector);

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
                    .enumerate()
                    .map(|(index, count)| HistogramBucket {
                        index: u32::try_from(index).unwrap(),
                        count: *count,
                    })
                    .collect::<Vec<HistogramBucket>>();

                assert_eq!(histogram_buckets, expected_histogram_buckets);
            }
            _ => panic!("Expecting int histogram."),
        }

        let transformed_event = transform_metrics_payload_to_cobalt(event);

        match transformed_event {
            EventPayload::IntHistogram(histogram_buckets) => {
                assert_eq!(histogram_buckets.len(), params.diff.len());

                let expected_histogram_buckets = params
                    .diff
                    .iter()
                    .enumerate()
                    .map(|(index, count)| CobaltHistogramBucket {
                        index: u32::try_from(index).unwrap(),
                        count: *count,
                    })
                    .collect::<Vec<CobaltHistogramBucket>>();

                assert_eq!(histogram_buckets, expected_histogram_buckets);
            }
            _ => panic!("Expecting int histogram."),
        }
    }

    #[test]
    fn test_normal_process_int_histogram() {
        // Test that simple in-bounds first-samples of both types of Inspect histograms
        // produce correct event types.
        let new_i64_sample = convert_vector_to_int_histogram(vec![1, 1, 1, 1]);
        let new_u64_sample = convert_vector_to_uint_histogram(vec![1, 1, 1, 1]);

        process_int_histogram_tester(IntHistogramTesterParams {
            new_val: new_i64_sample,
            old_val: None,
            process_ok: true,
            event_made: true,
            diff: vec![1, 1, 1, 1],
        });

        process_int_histogram_tester(IntHistogramTesterParams {
            new_val: new_u64_sample,
            old_val: None,
            process_ok: true,
            event_made: true,
            diff: vec![1, 1, 1, 1],
        });

        // Test an Inspect uint histogram at the boundaries of the type produce valid
        // cobalt events.
        let new_u64_sample = convert_vector_to_uint_histogram(vec![u64::MAX, u64::MAX, u64::MAX]);
        process_int_histogram_tester(IntHistogramTesterParams {
            new_val: new_u64_sample,
            old_val: None,
            process_ok: true,
            event_made: true,
            diff: vec![u64::MAX, u64::MAX, u64::MAX],
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
        let new_u64_sample = convert_vector_to_uint_histogram(vec![2, 1, 1, 1]);
        let old_u64_sample = Some(convert_vector_to_uint_histogram(vec![1, 1, 1, 1]));
        process_int_histogram_tester(IntHistogramTesterParams {
            new_val: new_u64_sample,
            old_val: old_u64_sample,
            process_ok: true,
            event_made: true,
            diff: vec![1, 0, 0, 0],
        });

        let new_i64_sample = convert_vector_to_int_histogram(vec![5, 2, 1, 3]);
        let old_i64_sample = Some(convert_vector_to_int_histogram(vec![1, 1, 1, 1]));
        process_int_histogram_tester(IntHistogramTesterParams {
            new_val: new_i64_sample,
            old_val: old_i64_sample,
            process_ok: true,
            event_made: true,
            diff: vec![4, 1, 0, 2],
        });

        // Test that changing the histogram type resets the cache.
        let new_u64_sample = convert_vector_to_uint_histogram(vec![2, 1, 1, 1]);
        let old_i64_sample = Some(convert_vector_to_int_histogram(vec![1, 1, 1, 1]));
        process_int_histogram_tester(IntHistogramTesterParams {
            new_val: new_u64_sample,
            old_val: old_i64_sample,
            process_ok: true,
            event_made: true,
            diff: vec![2, 1, 1, 1],
        });
    }

    #[test]
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
}
