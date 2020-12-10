// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::config::{DataType, MetricConfig, ProjectConfig, SamplerConfig},
    anyhow::{format_err, Context, Error},
    diagnostics_hierarchy::Property,
    diagnostics_reader::{ArchiveReader, Inspect},
    fidl_fuchsia_cobalt::{
        CobaltEvent, CountEvent, EventPayload, LoggerFactoryMarker, LoggerFactoryProxy, LoggerProxy,
    },
    fuchsia_async::{self as fasync, futures::StreamExt},
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon as zx,
    futures::{future::join_all, stream::FuturesUnordered},
    itertools::Itertools,
    log::{error, warn},
    std::{collections::HashMap, convert::TryInto, sync::Arc},
};

/// Owner of the sampler execution context.
pub struct SamplerExecutor {
    project_samplers: Vec<ProjectSampler>,
}

impl SamplerExecutor {
    /// Instantiate connection to the cobalt logger and map ProjectConfigurations
    /// to ProjectSampler plans.
    pub async fn new(sampler_config: SamplerConfig) -> Result<Self, Error> {
        let logger_factory: Arc<LoggerFactoryProxy> = Arc::new(
            connect_to_service::<LoggerFactoryMarker>()
                .context("Failed to connect to the Cobalt LoggerFactory")?,
        );

        let minimum_sample_rate_sec = sampler_config.minimum_sample_rate_sec;

        // TODO(lukenicholson): Create only one ArchiveReader for each unique poll rate so we
        // can avoid redundant snapshots.
        let project_sampler_futures =
            sampler_config.project_configs.into_iter().map(|project_config| {
                ProjectSampler::new(project_config, logger_factory.clone(), minimum_sample_rate_sec)
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

        Ok(SamplerExecutor { project_samplers })
    }

    /// Turn each ProjectSampler plan into an fasync::Task which executes its associated plan,
    /// and process errors if any tasks exit unexpectedly.
    pub async fn execute(self) {
        let mut spawned_tasks = self
            .project_samplers
            .into_iter()
            .map(|project_sampler| project_sampler.spawn())
            .collect::<FuturesUnordered<_>>();

        while let Some(sampler_result) = spawned_tasks.next().await {
            match sampler_result {
                Err(e) => {
                    // TODO(lukenicholson): Consider restarting the failed sampler depending on
                    // failure mode.
                    warn!("A spawned sampler has failed: {:?}", e);
                }
                _ => unreachable!(
                    "These are long running futures that should never healthily terminate."
                ),
            }
        }
    }
}

pub struct ProjectSampler {
    archive_reader: ArchiveReader,
    // Mapping from selector to the metric configs for that selector. Allows
    // for iteration over returned diagnostics schemas to drive transformations
    // with constant transformation metadata lookup.
    metric_transformation_map: HashMap<String, MetricConfig>,
    // Cache from inspect selector to last sampled property.
    metric_cache: HashMap<String, Property>,
    // Cobalt logger proxy using this ProjectSampler's project id.
    cobalt_logger: LoggerProxy,
    // The frequency with which we snapshot inspect properties
    // for this project.
    poll_rate_sec: i64,
}

impl ProjectSampler {
    pub async fn new(
        config: ProjectConfig,
        logger_factory: Arc<LoggerFactoryProxy>,
        minimum_sample_rate_sec: i64,
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
        let metric_transformation_map = config
            .metrics
            .into_iter()
            .map(|metric_config| (metric_config.selector.clone(), metric_config))
            .collect::<HashMap<String, MetricConfig>>();

        let (logger_proxy, server_end) =
            fidl::endpoints::create_proxy().context("Failed to create endpoints")?;

        logger_factory.create_logger_from_project_id(project_id, server_end).await?;

        Ok(ProjectSampler {
            archive_reader: ArchiveReader::new()
                .retry_if_empty(false)
                .add_selectors(metric_transformation_map.keys().cloned()),
            metric_transformation_map,
            metric_cache: HashMap::new(),
            cobalt_logger: logger_proxy,
            poll_rate_sec,
        })
    }

    pub fn spawn(mut self) -> fasync::Task<Result<(), Error>> {
        fasync::Task::spawn(async move {
            let mut periodic_timer =
                fasync::Interval::new(zx::Duration::from_seconds(self.poll_rate_sec));
            while let Some(()) = periodic_timer.next().await {
                let snapshot_data = self.archive_reader.snapshot::<Inspect>().await?;
                for data_packet in snapshot_data {
                    let moniker = data_packet.moniker;
                    match data_packet.payload {
                        None => warn!(
                            "Encountered errors snapshotting for {:?}: {:?}",
                            moniker, data_packet.metadata.errors
                        ),
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

                                    let metric_transformation_opt =
                                        self.metric_transformation_map.get(&selector);
                                    match metric_transformation_opt {
                                        None => {
                                            error!(concat!(
                                                "A property was returned by the",
                                                " diagnostics snapshot, which wasn't",
                                                " requested by the client."
                                            ));
                                            continue;
                                        }
                                        Some(metric_transformation) => {
                                            // Rust is scared that the sample processors require mutable
                                            // references to self, despite us using the values gathered
                                            // before the potential mutability, after. These values
                                            // won't change during the sample processing, but we do this to
                                            // appease the borrow checker.
                                            let metric_type =
                                                metric_transformation.metric_type.clone();
                                            let metric_id = metric_transformation.metric_id.clone();
                                            let event_codes =
                                                metric_transformation.event_codes.clone();

                                            self.process_metric_transformation(
                                                metric_type,
                                                metric_id,
                                                event_codes,
                                                selector,
                                                new_sample,
                                            )
                                            .await?
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Ok(())
        })
    }

    async fn process_metric_transformation(
        &mut self,
        metric_type: DataType,
        metric_id: u32,
        event_codes: Vec<u32>,
        selector: String,
        new_sample: &Property,
    ) -> Result<(), Error> {
        let previous_sample_opt: Option<&Property> = self.metric_cache.get(&selector);

        if let Some(payload) =
            process_sample_for_data_type(new_sample, previous_sample_opt, &selector, &metric_type)
        {
            self.maybe_update_cache(new_sample, &metric_type, selector);

            let mut cobalt_event = CobaltEvent {
                metric_id: metric_id,
                event_codes: event_codes,
                payload,
                component: None,
            };

            self.cobalt_logger.log_cobalt_event(&mut cobalt_event).await?;
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
            DataType::EventCount => {
                self.metric_cache.insert(selector.clone(), new_sample.clone());
            }
        }
    }
}

fn process_sample_for_data_type(
    new_sample: &Property,
    previous_sample_opt: Option<&Property>,
    selector: &String,
    data_type: &DataType,
) -> Option<EventPayload> {
    let event_payload_res = match data_type {
        DataType::EventCount => process_event_count(new_sample, previous_sample_opt, selector),
    };

    match event_payload_res {
        Ok(payload_opt) => payload_opt,
        Err(e) => {
            warn!(concat!("Failed to process inspect property for cobalt: {:?}"), e);
            None
        }
    }
}

// It's possible for the first sample of an event count
// to overflow, or for the diff of two samples to overflow.
// Sanitize both of these cases so we can surface actionable
// error logs to clients.
fn sanitize_unsigned_diff(diff: u64, selector: &String) -> Result<i64, Error> {
    match diff.try_into() {
        Ok(diff) => Ok(diff),
        Err(_) => {
            return Err(format_err!(
                concat!(
                    "Selector used for EventCount type",
                    " refered to an unsigned int property,",
                    " but cobalt requires i64, and casting introduced overflow",
                    " which produces a negative int: {:?}. This could be due to",
                    " the first sample being larger than i64, or a diff between",
                    " samples being larger than i64."
                ),
                selector
            ));
        }
    }
}

fn process_event_count(
    new_sample: &Property,
    prev_sample_opt: Option<&Property>,
    selector: &String,
) -> Result<Option<EventPayload>, Error> {
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

    // TODO(lukenicholson): If we decide to encode period duration,
    // use system uptime here.
    Ok(Some(EventPayload::EventCount(CountEvent { count: diff, period_duration_micros: 0 })))
}

fn compute_initial_event_count(new_sample: &Property, selector: &String) -> Result<i64, Error> {
    match new_sample {
        Property::Uint(_, val) => sanitize_unsigned_diff(val.clone(), selector),
        Property::Int(_, val) => Ok(val.clone()),
        _ => Err(format_err!(
            concat!(
                "Selector referenced an inspect property",
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
            sanitize_unsigned_diff(new_count - old_count, selector)
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

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn test_normal_process_event_count() {
        let selector: String = "test:root:count".to_string();
        let event_res =
            process_event_count(&Property::Int("count".to_string(), 1), None, &selector);
        assert!(event_res.is_ok());
        let event_opt = event_res.unwrap();
        assert!(event_opt.is_some());

        match event_opt.unwrap() {
            EventPayload::EventCount(count_event) => {
                assert_eq!(count_event.count, 1);
                assert_eq!(count_event.period_duration_micros, 0);
            }
            _ => panic!("Expecting event counts."),
        }

        let event_res = process_event_count(
            &Property::Int("count".to_string(), 1),
            Some(&Property::Int("count".to_string(), 1)),
            &selector,
        );

        assert!(event_res.is_ok());
        let event_opt = event_res.unwrap();
        assert!(event_opt.is_none());

        let event_res = process_event_count(
            &Property::Int("count".to_string(), 3),
            Some(&Property::Int("count".to_string(), 1)),
            &selector,
        );
        assert!(event_res.is_ok());

        let event_opt = event_res.unwrap();
        assert!(event_opt.is_some());

        match event_opt.unwrap() {
            EventPayload::EventCount(count_event) => {
                assert_eq!(count_event.count, 2);
                assert_eq!(count_event.period_duration_micros, 0);
            }
            _ => panic!("Expecting event counts."),
        }
    }

    #[test]
    fn test_data_type_changing_process_event_count() {
        let selector: String = "test:root:count".to_string();
        let event_res =
            process_event_count(&Property::Int("count".to_string(), 1), None, &selector);
        assert!(event_res.is_ok());
        let event_opt = event_res.unwrap();
        assert!(event_opt.is_some());

        match event_opt.unwrap() {
            EventPayload::EventCount(count_event) => {
                assert_eq!(count_event.count, 1);
                assert_eq!(count_event.period_duration_micros, 0);
            }
            _ => panic!("Expecting event counts."),
        }

        let event_res =
            process_event_count(&Property::Uint("count".to_string(), 1), None, &selector);

        assert!(event_res.is_ok());

        let event_opt = event_res.unwrap();
        assert!(event_opt.is_some());

        match event_opt.unwrap() {
            EventPayload::EventCount(count_event) => {
                assert_eq!(count_event.count, 1);
                assert_eq!(count_event.period_duration_micros, 0);
            }
            _ => panic!("Expecting event counts."),
        }

        let event_res = process_event_count(
            &Property::Uint("count".to_string(), 1),
            Some(&Property::Uint("count".to_string(), 1)),
            &selector,
        );

        assert!(event_res.is_ok());
        let event_opt = event_res.unwrap();
        assert!(event_opt.is_none());

        let event_res = process_event_count(
            &Property::String("count".to_string(), "big_oof".to_string()),
            Some(&Property::Uint("count".to_string(), 1)),
            &selector,
        );

        assert!(event_res.is_err());
    }

    #[test]
    fn test_event_count_negatives_and_overflows() {
        let selector: String = "test:root:count".to_string();

        let event_res =
            process_event_count(&Property::Int("count".to_string(), -11), None, &selector);
        assert!(event_res.is_err());

        let event_res = process_event_count(
            &Property::Int("count".to_string(), 9),
            Some(&Property::Int("count".to_string(), 10)),
            &selector,
        );
        assert!(event_res.is_err());

        let event_res = process_event_count(
            &Property::Uint("count".to_string(), std::u64::MAX),
            None,
            &selector,
        );
        assert!(event_res.is_err());

        let i64_max_in_u64: u64 = std::i64::MAX.try_into().unwrap();

        let event_res = process_event_count(
            &Property::Uint("count".to_string(), i64_max_in_u64 + 1),
            Some(&Property::Uint("count".to_string(), 1)),
            &selector,
        );
        assert!(event_res.is_ok());

        let event_res = process_event_count(
            &Property::Uint("count".to_string(), i64_max_in_u64 + 2),
            Some(&Property::Uint("count".to_string(), 1)),
            &selector,
        );

        assert!(event_res.is_err());
    }
}
