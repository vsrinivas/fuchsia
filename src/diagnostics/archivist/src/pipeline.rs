// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        accessor::ArchiveAccessor, configs, constants, diagnostics::AccessorStats, error::Error,
        inspect::container::UnpopulatedInspectDataContainer, moniker_rewriter::MonikerRewriter,
        repository::DataRepo, ImmutableString,
    },
    async_lock::RwLock,
    diagnostics_data::LogsData,
    diagnostics_hierarchy::InspectHierarchyMatcher,
    fidl::prelude::*,
    fidl_fuchsia_diagnostics::{self, ArchiveAccessorMarker, Selector, StreamMode},
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceObj},
    fuchsia_inspect as inspect,
    futures::{channel::mpsc, prelude::*},
    selectors,
    std::{collections::HashMap, convert::TryInto, path::Path, sync::Arc},
};

struct PipelineParameters {
    has_config: bool,
    name: &'static str,
    protocol_name: &'static str,
    empty_behavior: configs::EmptyBehavior,
    moniker_rewriter: Option<MonikerRewriter>,
}

/// Overlay that mediates connections between servers and the central
/// data repository. The overlay is provided static configurations that
/// make it unique to a specific pipeline, and uses those static configurations
/// to offer filtered access to the central repository.
pub struct Pipeline {
    /// Static selectors that the pipeline uses. Loaded from configuration.
    static_selectors: Option<Vec<Selector>>,

    /// A hierarchy matcher for any selector present in the static selectors.
    moniker_to_static_matcher_map: HashMap<ImmutableString, InspectHierarchyMatcher>,

    /// The data repository.
    data_repo: DataRepo,

    /// When the pipeline starts serving, this holds stats about requests made to it.
    stats_node: Option<Arc<AccessorStats>>,

    /// The name of the protocol through which the pipeline is served.
    protocol_name: &'static str,

    /// The name of the pipeline.
    name: &'static str,

    /// A rewriter of monikers that the pipeline uses.
    moniker_rewriter: Option<MonikerRewriter>,

    /// Contains information about the configuration of the pipeline.
    _pipeline_node: Option<inspect::Node>,

    /// Whether the pipeline had an error when being created.
    has_error: bool,
}

impl Pipeline {
    /// Creates a pipeline for feedback. This applies static selectors configured under
    /// config/data/feedback to inspect exfiltration.
    pub fn feedback(
        data_repo: DataRepo,
        pipelines_path: &Path,
        parent_node: &inspect::Node,
    ) -> Self {
        let parameters = PipelineParameters {
            has_config: true,
            name: "feedback",
            empty_behavior: configs::EmptyBehavior::DoNotFilter,
            protocol_name: constants::FEEDBACK_ARCHIVE_ACCESSOR_NAME,
            moniker_rewriter: None,
        };
        Self::new(parameters, data_repo, pipelines_path, parent_node)
    }

    /// Creates a pipeline for legacy metrics. This applies static selectors configured
    /// under config/data/legacy_metrics to inspect exfiltration.
    pub fn legacy_metrics(
        data_repo: DataRepo,
        pipelines_path: &Path,
        parent_node: &inspect::Node,
    ) -> Self {
        let parameters = PipelineParameters {
            has_config: true,
            name: "legacy_metrics",
            empty_behavior: configs::EmptyBehavior::Disable,
            protocol_name: constants::LEGACY_METRICS_ARCHIVE_ACCESSOR_NAME,
            moniker_rewriter: Some(MonikerRewriter::new()),
        };
        Self::new(parameters, data_repo, pipelines_path, parent_node)
    }

    /// Creates a pipeline for all access. This pipeline is unique in that it has no statically
    /// configured selectors, meaning all diagnostics data is visible. This should not be used for
    /// production services.
    pub fn all_access(
        data_repo: DataRepo,
        pipelines_path: &Path,
        parent_node: &inspect::Node,
    ) -> Self {
        let parameters = PipelineParameters {
            has_config: false,
            name: "all",
            empty_behavior: configs::EmptyBehavior::Disable,
            protocol_name: ArchiveAccessorMarker::PROTOCOL_NAME,
            moniker_rewriter: None,
        };
        Self::new(parameters, data_repo, pipelines_path, parent_node)
    }

    /// Creates a pipeline for LoWPAN metrics. This applies static selectors configured
    /// under config/data/lowpan to inspect exfiltration.
    pub fn lowpan(data_repo: DataRepo, pipelines_path: &Path, parent_node: &inspect::Node) -> Self {
        let parameters = PipelineParameters {
            has_config: true,
            name: "lowpan",
            empty_behavior: configs::EmptyBehavior::Disable,
            protocol_name: constants::LOWPAN_ARCHIVE_ACCESSOR_NAME,
            moniker_rewriter: Some(MonikerRewriter::new()),
        };
        Self::new(parameters, data_repo, pipelines_path, parent_node)
    }

    #[cfg(test)]
    pub fn for_test(static_selectors: Option<Vec<Selector>>, data_repo: DataRepo) -> Self {
        Pipeline {
            _pipeline_node: None,
            moniker_to_static_matcher_map: HashMap::new(),
            static_selectors,
            stats_node: None,
            data_repo,
            moniker_rewriter: None,
            protocol_name: "test",
            name: "test",
            has_error: false,
        }
    }

    fn new(
        parameters: PipelineParameters,
        data_repo: DataRepo,
        pipelines_path: &Path,
        parent_node: &inspect::Node,
    ) -> Self {
        let mut _pipeline_node = None;
        let path = format!("{}/{}", pipelines_path.display(), parameters.name);
        let mut static_selectors = None;
        let mut has_error = false;
        if parameters.has_config {
            let node = parent_node.create_child(parameters.name);
            let mut config =
                configs::PipelineConfig::from_directory(&path, parameters.empty_behavior);
            config.record_to_inspect(&node);
            _pipeline_node = Some(node);
            if !config.disable_filtering {
                static_selectors = config.take_inspect_selectors();
            }
            has_error = Path::new(&path).is_dir() && config.has_error();
        }
        Pipeline {
            moniker_to_static_matcher_map: HashMap::new(),
            static_selectors,
            data_repo,
            _pipeline_node,
            stats_node: None,
            protocol_name: parameters.protocol_name,
            moniker_rewriter: parameters.moniker_rewriter,
            name: parameters.name,
            has_error,
        }
    }

    pub fn config_has_error(&self) -> bool {
        self.has_error
    }

    /// Serves the pipeline as a protocol in the outgoing svc directory.
    pub fn serve(
        mut self,
        service_fs: &mut ServiceFs<ServiceObj<'static, ()>>,
        listen_sender: mpsc::UnboundedSender<fasync::Task<()>>,
        stats_node_parent: &inspect::Node,
    ) -> Arc<RwLock<Self>> {
        let stats = Arc::new(AccessorStats::new(stats_node_parent.create_child(self.name)));
        let moniker_rewriter = self.moniker_rewriter.take().map(Arc::new);
        let protocol_name = self.protocol_name;
        self.stats_node = Some(stats.clone());
        let this = Arc::new(RwLock::new(self));
        let pipeline = this.clone();
        service_fs.dir("svc").add_fidl_service_at(protocol_name, move |stream| {
            let mut accessor = ArchiveAccessor::new(pipeline.clone(), stats.clone());
            if let Some(rewriter) = &moniker_rewriter {
                accessor.add_moniker_rewriter(rewriter.clone());
            }
            accessor.spawn_server(stream, listen_sender.clone());
        });
        this
    }

    pub async fn logs(
        &self,
        mode: StreamMode,
        selectors: Option<Vec<Selector>>,
    ) -> impl Stream<Item = Arc<LogsData>> {
        self.data_repo.logs_cursor(mode, selectors).await
    }

    pub fn remove(&mut self, relative_moniker: &[String]) {
        self.moniker_to_static_matcher_map.remove(relative_moniker.join("/").as_str());
    }

    pub fn add_inspect_artifacts(&mut self, relative_moniker: &[String]) -> Result<(), Error> {
        // Update the pipeline wrapper to be aware of the new inspect source if there
        // are are static selectors for the pipeline, and some of them are applicable to
        // the inspect source's relative moniker. Otherwise, ignore.
        if let Some(selectors) = &self.static_selectors {
            let matched_selectors =
                selectors::match_component_moniker_against_selectors(relative_moniker, selectors)
                    .map_err(Error::MatchComponentMoniker)?;

            match &matched_selectors[..] {
                [] => {}
                populated_vec => {
                    let hierarchy_matcher = (populated_vec).try_into()?;
                    self.moniker_to_static_matcher_map
                        .insert(relative_moniker.join("/").into_boxed_str(), hierarchy_matcher);
                }
            }
        }
        Ok(())
    }

    /// Return all of the DirectoryProxies that contain Inspect hierarchies
    /// which contain data that should be selected from.
    pub async fn fetch_inspect_data(
        &self,
        component_selectors: &Option<Vec<Selector>>,
    ) -> Vec<UnpopulatedInspectDataContainer> {
        let moniker_to_static_selector_opt =
            self.static_selectors.as_ref().map(|_| &self.moniker_to_static_matcher_map);

        self.data_repo
            .read()
            .await
            .fetch_inspect_data(component_selectors, moniker_to_static_selector_opt)
    }
}
