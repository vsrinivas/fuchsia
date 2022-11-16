// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        configs, constants, diagnostics::AccessorStats, error::Error,
        moniker_rewriter::MonikerRewriter, ImmutableString,
    },
    async_lock::RwLock,
    diagnostics_hierarchy::InspectHierarchyMatcher,
    fidl::prelude::*,
    fidl_fuchsia_diagnostics::{self, ArchiveAccessorMarker, Selector},
    fuchsia_inspect as inspect,
    std::{collections::HashMap, convert::TryInto, ops::Deref, path::Path},
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
    /// The name of the protocol through which the pipeline is served.
    protocol_name: &'static str,

    /// A rewriter of monikers that the pipeline uses.
    moniker_rewriter: Option<MonikerRewriter>,

    /// Contains information about the configuration of the pipeline.
    _pipeline_node: Option<inspect::Node>,

    /// Contains information about the accessor requests done for this pipeline.
    stats: AccessorStats,

    /// Whether the pipeline had an error when being created.
    has_error: bool,

    mutable_state: RwLock<PipelineMutableState>,
}

impl Pipeline {
    /// Creates a pipeline for feedback. This applies static selectors configured under
    /// config/data/feedback to inspect exfiltration.
    pub fn feedback(
        pipelines_path: &Path,
        parent_node: &inspect::Node,
        accessor_stats_node: &inspect::Node,
    ) -> Self {
        let parameters = PipelineParameters {
            has_config: true,
            name: "feedback",
            empty_behavior: configs::EmptyBehavior::DoNotFilter,
            protocol_name: constants::FEEDBACK_ARCHIVE_ACCESSOR_NAME,
            moniker_rewriter: None,
        };
        Self::new(parameters, pipelines_path, parent_node, accessor_stats_node)
    }

    /// Creates a pipeline for legacy metrics. This applies static selectors configured
    /// under config/data/legacy_metrics to inspect exfiltration.
    pub fn legacy_metrics(
        pipelines_path: &Path,
        parent_node: &inspect::Node,
        accessor_stats_node: &inspect::Node,
    ) -> Self {
        let parameters = PipelineParameters {
            has_config: true,
            name: "legacy_metrics",
            empty_behavior: configs::EmptyBehavior::Disable,
            protocol_name: constants::LEGACY_METRICS_ARCHIVE_ACCESSOR_NAME,
            moniker_rewriter: Some(MonikerRewriter::new()),
        };
        Self::new(parameters, pipelines_path, parent_node, accessor_stats_node)
    }

    /// Creates a pipeline for all access. This pipeline is unique in that it has no statically
    /// configured selectors, meaning all diagnostics data is visible. This should not be used for
    /// production services.
    pub fn all_access(
        pipelines_path: &Path,
        parent_node: &inspect::Node,
        accessor_stats_node: &inspect::Node,
    ) -> Self {
        let parameters = PipelineParameters {
            has_config: false,
            name: "all",
            empty_behavior: configs::EmptyBehavior::Disable,
            protocol_name: ArchiveAccessorMarker::PROTOCOL_NAME,
            moniker_rewriter: None,
        };
        Self::new(parameters, pipelines_path, parent_node, accessor_stats_node)
    }

    /// Creates a pipeline for LoWPAN metrics. This applies static selectors configured
    /// under config/data/lowpan to inspect exfiltration.
    pub fn lowpan(
        pipelines_path: &Path,
        parent_node: &inspect::Node,
        accessor_stats_node: &inspect::Node,
    ) -> Self {
        let parameters = PipelineParameters {
            has_config: true,
            name: "lowpan",
            empty_behavior: configs::EmptyBehavior::Disable,
            protocol_name: constants::LOWPAN_ARCHIVE_ACCESSOR_NAME,
            moniker_rewriter: Some(MonikerRewriter::new()),
        };
        Self::new(parameters, pipelines_path, parent_node, accessor_stats_node)
    }

    #[cfg(test)]
    pub fn for_test(static_selectors: Option<Vec<Selector>>) -> Self {
        Pipeline {
            _pipeline_node: None,
            moniker_rewriter: None,
            protocol_name: "test",
            has_error: false,
            stats: AccessorStats::new(Default::default()),
            mutable_state: RwLock::new(PipelineMutableState {
                moniker_to_static_matcher_map: HashMap::new(),
                static_selectors,
            }),
        }
    }

    fn new(
        parameters: PipelineParameters,
        pipelines_path: &Path,
        parent_node: &inspect::Node,
        accessor_stats_node: &inspect::Node,
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
        let stats = AccessorStats::new(accessor_stats_node.create_child(parameters.name));
        Pipeline {
            _pipeline_node,
            stats,
            protocol_name: parameters.protocol_name,
            moniker_rewriter: parameters.moniker_rewriter,
            has_error,
            mutable_state: RwLock::new(PipelineMutableState {
                moniker_to_static_matcher_map: HashMap::new(),
                static_selectors,
            }),
        }
    }

    pub fn config_has_error(&self) -> bool {
        self.has_error
    }

    pub fn protocol_name(&self) -> &'static str {
        self.protocol_name
    }

    pub fn moniker_rewriter(&self) -> Option<&MonikerRewriter> {
        self.moniker_rewriter.as_ref()
    }

    pub fn accessor_stats(&self) -> &AccessorStats {
        &self.stats
    }
}

pub struct PipelineMutableState {
    /// Static selectors that the pipeline uses. Loaded from configuration.
    static_selectors: Option<Vec<Selector>>,

    /// A hierarchy matcher for any selector present in the static selectors.
    moniker_to_static_matcher_map: HashMap<ImmutableString, InspectHierarchyMatcher>,
}

impl Deref for Pipeline {
    type Target = RwLock<PipelineMutableState>;

    fn deref(&self) -> &Self::Target {
        &self.mutable_state
    }
}

impl PipelineMutableState {
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

    pub fn static_selectors_matchers(
        &self,
    ) -> Option<&HashMap<ImmutableString, InspectHierarchyMatcher>> {
        if self.static_selectors.is_some() {
            return Some(&self.moniker_to_static_matcher_map);
        }
        None
    }
}
