// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        events::types::ComponentIdentifier,
        inspect::container::UnpopulatedInspectDataContainer,
        lifecycle::container::LifecycleDataContainer,
        logs::{
            redact::{RedactedItem, Redactor},
            Message,
        },
        repository::DataRepo,
    },
    anyhow::Error,
    diagnostics_hierarchy::InspectHierarchyMatcher,
    fidl_fuchsia_diagnostics::{self, Selector, StreamMode},
    futures::prelude::*,
    selectors,
    std::collections::HashMap,
    std::convert::TryInto,
    std::sync::Arc,
};

/// Overlay that mediates connections between servers and the central
/// data repository. The overlay is provided static configurations that
/// make it unique to a specific pipeline, and uses those static configurations
/// to offer filtered access to the central repository.
pub struct Pipeline {
    static_pipeline_selectors: Option<Vec<Arc<Selector>>>,
    log_redactor: Arc<Redactor>,
    moniker_to_static_matcher_map: HashMap<String, InspectHierarchyMatcher>,
    data_repo: DataRepo,
}

impl Pipeline {
    pub fn new(
        static_pipeline_selectors: Option<Vec<Arc<Selector>>>,
        log_redactor: Redactor,
        data_repo: DataRepo,
    ) -> Self {
        Pipeline {
            moniker_to_static_matcher_map: HashMap::new(),
            static_pipeline_selectors,
            log_redactor: Arc::new(log_redactor),
            data_repo,
        }
    }

    #[cfg(test)]
    pub fn for_test(
        static_pipeline_selectors: Option<Vec<Arc<Selector>>>,
        data_repo: DataRepo,
    ) -> Self {
        Pipeline {
            moniker_to_static_matcher_map: HashMap::new(),
            static_pipeline_selectors,
            log_redactor: Arc::new(Redactor::noop()),
            data_repo,
        }
    }

    pub fn logs(&self, mode: StreamMode) -> impl Stream<Item = RedactedItem<Message>> {
        self.log_redactor.clone().redact_stream(self.data_repo.cursor(mode))
    }

    pub fn remove(&mut self, component_id: &ComponentIdentifier) {
        self.moniker_to_static_matcher_map
            .remove(&component_id.relative_moniker_for_selectors().join("/"));
    }

    pub fn add_inspect_artifacts(&mut self, identifier: ComponentIdentifier) -> Result<(), Error> {
        let relative_moniker = identifier.relative_moniker_for_selectors();

        // Update the pipeline wrapper to be aware of the new inspect source if there
        // are are static selectors for the pipeline, and some of them are applicable to
        // the inspect source's relative moniker. Otherwise, ignore.
        if let Some(selectors) = &self.static_pipeline_selectors {
            let matched_selectors = selectors::match_component_moniker_against_selectors(
                &relative_moniker,
                &selectors,
            )?;

            match &matched_selectors[..] {
                [] => {}
                populated_vec => {
                    let hierarchy_matcher = (populated_vec).try_into()?;
                    self.moniker_to_static_matcher_map
                        .insert(relative_moniker.join("/"), hierarchy_matcher);
                }
            }
        }
        Ok(())
    }

    pub fn fetch_lifecycle_event_data(&self) -> Vec<LifecycleDataContainer> {
        self.data_repo.read().fetch_lifecycle_event_data()
    }

    /// Return all of the DirectoryProxies that contain Inspect hierarchies
    /// which contain data that should be selected from.
    pub fn fetch_inspect_data(
        &self,
        component_selectors: &Option<Vec<Arc<Selector>>>,
    ) -> Vec<UnpopulatedInspectDataContainer> {
        let moniker_to_static_selector_opt =
            self.static_pipeline_selectors.as_ref().map(|_| &self.moniker_to_static_matcher_map);

        self.data_repo
            .read()
            .fetch_inspect_data(component_selectors, moniker_to_static_selector_opt)
    }
}
