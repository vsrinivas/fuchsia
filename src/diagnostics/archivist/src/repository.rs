// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        container::ComponentDiagnostics,
        events::types::ComponentIdentifier,
        inspect::container::{InspectArtifactsContainer, UnpopulatedInspectDataContainer},
        lifecycle::container::{LifecycleArtifactsContainer, LifecycleDataContainer},
        logs::{
            redact::{RedactedItem, Redactor},
            LogManager, Message,
        },
    },
    anyhow::{format_err, Error},
    diagnostics_hierarchy::{trie, InspectHierarchyMatcher},
    fidl_fuchsia_diagnostics::{self, Selector, StreamMode},
    fidl_fuchsia_io::{DirectoryProxy, CLONE_FLAG_SAME_RIGHTS},
    fuchsia_zircon as zx,
    futures::prelude::*,
    io_util,
    parking_lot::RwLock,
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
    data_repo: Arc<RwLock<DataRepo>>,
}

impl Pipeline {
    pub fn new(
        static_pipeline_selectors: Option<Vec<Arc<Selector>>>,
        log_redactor: Redactor,
        data_repo: Arc<RwLock<DataRepo>>,
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
        data_repo: Arc<RwLock<DataRepo>>,
    ) -> Self {
        Pipeline {
            moniker_to_static_matcher_map: HashMap::new(),
            static_pipeline_selectors,
            log_redactor: Arc::new(Redactor::noop()),
            data_repo,
        }
    }

    pub fn logs(&self, mode: StreamMode) -> impl Stream<Item = RedactedItem<Message>> {
        let repo = self.data_repo.read();
        self.log_redactor.clone().redact_stream(repo.log_manager.cursor(mode))
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

/// DataRepo manages storage of all state needed in order
/// for the inspect reader to retrieve inspect data when a read is requested.
pub struct DataRepo {
    pub data_directories: trie::Trie<String, ComponentDiagnostics>,
    log_manager: LogManager,
}

impl DataRepo {
    pub fn new(log_manager: LogManager) -> Self {
        DataRepo { log_manager, data_directories: trie::Trie::new() }
    }

    #[cfg(test)]
    pub fn for_test() -> Self {
        Self::new(LogManager::new())
    }

    pub fn remove(&mut self, component_id: &ComponentIdentifier) {
        self.data_directories.remove(component_id.unique_key());
    }

    pub fn add_new_component(
        &mut self,
        identifier: ComponentIdentifier,
        component_url: impl Into<String>,
        event_timestamp: zx::Time,
        component_start_time: Option<zx::Time>,
    ) -> Result<(), Error> {
        let relative_moniker = identifier.relative_moniker_for_selectors();

        let lifecycle_artifact_container = LifecycleArtifactsContainer {
            event_timestamp: event_timestamp,
            component_start_time: component_start_time,
        };

        let key = identifier.unique_key();

        let diag_repo_entry_opt = self.data_directories.get_mut(key.clone());
        match diag_repo_entry_opt {
            Some(diag_repo_entry) => {
                let diag_repo_entry_values: &mut [ComponentDiagnostics] =
                    diag_repo_entry.get_values_mut();

                match &mut diag_repo_entry_values[..] {
                    [] => {
                        // An entry with no values implies that the somehow we observed the
                        // creation of a component lower in the topology before observing this
                        // one. If this is the case, just instantiate as though it's our first
                        // time encountering this moniker segment.
                        self.data_directories.insert(
                            key,
                            ComponentDiagnostics {
                                relative_moniker: relative_moniker,
                                component_url: component_url.into(),
                                lifecycle: Some(lifecycle_artifact_container),
                                inspect: None,
                            },
                        )
                    }
                    [existing_diagnostics_artifact_container] => {
                        // Races may occur between seeing diagnostics ready and seeing
                        // creation lifecycle events. Handle this here.
                        // TODO(fxbug.dev/52047): Remove once caching handles ordering issues.
                        if existing_diagnostics_artifact_container.lifecycle.is_none() {
                            existing_diagnostics_artifact_container.lifecycle =
                                Some(lifecycle_artifact_container);
                        }
                    }
                    _ => {
                        return Err(format_err!(
                            concat!(
                                "Encountered a diagnostics data repository node with more",
                                "than one artifact container, moniker: {:?}."
                            ),
                            key
                        ));
                    }
                }
            }
            // This case is expected to be the most common case. We've seen a creation
            // lifecycle event and it promotes the instantiation of a new data repository entry.
            None => self.data_directories.insert(
                key,
                ComponentDiagnostics {
                    relative_moniker: relative_moniker,
                    component_url: component_url.into(),
                    lifecycle: Some(lifecycle_artifact_container),
                    inspect: None,
                },
            ),
        }
        Ok(())
    }

    pub fn add_inspect_artifacts(
        &mut self,
        identifier: ComponentIdentifier,
        component_url: impl Into<String>,
        directory_proxy: DirectoryProxy,
        event_timestamp: zx::Time,
    ) -> Result<(), Error> {
        let relative_moniker = identifier.relative_moniker_for_selectors();
        let key = identifier.unique_key();

        let inspect_container = InspectArtifactsContainer {
            component_diagnostics_proxy: Arc::new(directory_proxy),
            event_timestamp,
        };

        self.insert_inspect_artifact_container(
            inspect_container,
            key,
            relative_moniker,
            component_url.into(),
        )
    }

    // Inserts an InspectArtifactsContainer into the data repository.
    fn insert_inspect_artifact_container(
        &mut self,
        inspect_container: InspectArtifactsContainer,
        key: Vec<String>,
        relative_moniker: Vec<String>,
        component_url: String,
    ) -> Result<(), Error> {
        let diag_repo_entry_opt = self.data_directories.get_mut(key.clone());
        match diag_repo_entry_opt {
            Some(diag_repo_entry) => {
                let diag_repo_entry_values: &mut [ComponentDiagnostics] =
                    diag_repo_entry.get_values_mut();

                match &mut diag_repo_entry_values[..] {
                    [] => {
                        // An entry with no values implies that the somehow we observed the
                        // creation of a component lower in the topology before observing this
                        // one. If this is the case, just instantiate as though it's our first
                        // time encountering this moniker segment.
                        self.data_directories.insert(
                            key,
                            ComponentDiagnostics {
                                relative_moniker: relative_moniker,
                                component_url,
                                lifecycle: None,
                                inspect: Some(inspect_container),
                            },
                        )
                    }
                    [existing_diagnostics_artifact_container] => {
                        // Races may occur between synthesized and real diagnostics_ready
                        // events, so we must handle de-duplication here.
                        // TODO(fxbug.dev/52047): Remove once caching handles ordering issues.
                        if existing_diagnostics_artifact_container.inspect.is_none() {
                            // This is expected to be the most common case. We've encountered the
                            // diagnostics_ready event for a component that has already been
                            // observed to be started/existing. We now must update the diagnostics
                            // artifact container with the inspect artifacts that accompanied the
                            // diagnostics_ready event.
                            existing_diagnostics_artifact_container.inspect =
                                Some(inspect_container);
                        }
                    }
                    _ => {
                        return Err(format_err!(
                            concat!(
                                "Encountered a diagnostics data repository node with more",
                                "than one artifact container, moniker: {:?}."
                            ),
                            key
                        ));
                    }
                }
            }
            // This case is expected to be uncommon; we've encountered a diagnostics_ready
            // event before a start or existing event!
            None => self.data_directories.insert(
                key,
                ComponentDiagnostics {
                    relative_moniker: relative_moniker,
                    component_url,
                    lifecycle: None,
                    inspect: Some(inspect_container),
                },
            ),
        }
        Ok(())
    }

    pub fn fetch_lifecycle_event_data(&self) -> Vec<LifecycleDataContainer> {
        self.data_directories.iter().fold(
            Vec::new(),
            |mut acc, (_, diagnostics_artifacts_container_opt)| {
                match diagnostics_artifacts_container_opt {
                    None => acc,
                    Some(diagnostics_artifacts_container) => {
                        if let Some(lifecycle_artifacts) =
                            &diagnostics_artifacts_container.lifecycle
                        {
                            acc.push(LifecycleDataContainer::from_lifecycle_artifact(
                                lifecycle_artifacts,
                                diagnostics_artifacts_container.relative_moniker.clone(),
                                diagnostics_artifacts_container.component_url.clone(),
                            ));
                        }

                        if let Some(inspect_artifacts) = &diagnostics_artifacts_container.inspect {
                            acc.push(LifecycleDataContainer::from_inspect_artifact(
                                inspect_artifacts,
                                diagnostics_artifacts_container.relative_moniker.clone(),
                                diagnostics_artifacts_container.component_url.clone(),
                            ));
                        }

                        acc
                    }
                }
            },
        )
    }

    /// Return all of the DirectoryProxies that contain Inspect hierarchies
    /// which contain data that should be selected from.
    pub fn fetch_inspect_data(
        &self,
        component_selectors: &Option<Vec<Arc<Selector>>>,
        moniker_to_static_matcher_map: Option<&HashMap<String, InspectHierarchyMatcher>>,
    ) -> Vec<UnpopulatedInspectDataContainer> {
        return self
            .data_directories
            .iter()
            .filter_map(|(_, diagnostics_artifacts_container_opt)| {
                let (diagnostics_artifacts_container, inspect_artifacts) =
                    match &diagnostics_artifacts_container_opt {
                        Some(diagnostics_artifacts_container) => {
                            match &diagnostics_artifacts_container.inspect {
                                Some(inspect_artifacts) => {
                                    (diagnostics_artifacts_container, inspect_artifacts)
                                }
                                None => return None,
                            }
                        }
                        None => return None,
                    };

                let optional_hierarchy_matcher = match moniker_to_static_matcher_map {
                    Some(map) => {
                        match map.get(&diagnostics_artifacts_container.relative_moniker.join("/")) {
                            Some(inspect_matcher) => Some(inspect_matcher),
                            // Return early if there were static selectors, and none were for this
                            // moniker.
                            None => return None,
                        }
                    }
                    None => None,
                };

                // Verify that the dynamic selectors contain an entry that applies to
                // this moniker as well.
                if !match component_selectors {
                    Some(component_selectors) => component_selectors.iter().any(|s| {
                        selectors::match_component_moniker_against_selector(
                            &diagnostics_artifacts_container.relative_moniker,
                            s,
                        )
                        .ok()
                        .unwrap_or(false)
                    }),
                    None => true,
                } {
                    return None;
                }

                // This artifact contains inspect and matches a passed selector.
                io_util::clone_directory(
                    &inspect_artifacts.component_diagnostics_proxy,
                    CLONE_FLAG_SAME_RIGHTS,
                )
                .ok()
                .map(|directory| UnpopulatedInspectDataContainer {
                    relative_moniker: diagnostics_artifacts_container.relative_moniker.clone(),
                    component_url: diagnostics_artifacts_container.component_url.clone(),
                    component_diagnostics_proxy: directory,
                    inspect_matcher: optional_hierarchy_matcher.cloned(),
                })
            })
            .collect();
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::events::types::{ComponentIdentifier, LegacyIdentifier, RealmPath},
        diagnostics_hierarchy::trie::TrieIterableNode,
        fidl_fuchsia_io::DirectoryMarker,
        fuchsia_async as fasync, fuchsia_zircon as zx,
    };

    const TEST_URL: &'static str = "fuchsia-pkg://test";

    #[fasync::run_singlethreaded(test)]
    async fn inspect_repo_disallows_duplicated_dirs() {
        let mut inspect_repo = DataRepo::for_test();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });
        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        inspect_repo
            .add_inspect_artifacts(component_id.clone(), TEST_URL, proxy, zx::Time::from_nanos(0))
            .expect("add to repo");

        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        inspect_repo
            .add_inspect_artifacts(component_id.clone(), TEST_URL, proxy, zx::Time::from_nanos(0))
            .expect("add to repo");

        let key = component_id.unique_key();
        assert_eq!(inspect_repo.data_directories.get(key).unwrap().get_values().len(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn data_repo_updates_existing_entry_to_hold_inspect_data() {
        let mut data_repo = DataRepo::for_test();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });

        data_repo
            .add_new_component(component_id.clone(), TEST_URL, zx::Time::from_nanos(0), None)
            .expect("instantiated new component.");

        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        data_repo
            .add_inspect_artifacts(component_id.clone(), TEST_URL, proxy, zx::Time::from_nanos(0))
            .expect("add to repo");

        let key = component_id.unique_key();
        assert_eq!(data_repo.data_directories.get(key.clone()).unwrap().get_values().len(), 1);
        let entry = &data_repo.data_directories.get(key.clone()).unwrap().get_values()[0];
        assert!(entry.inspect.is_some());
        assert_eq!(entry.component_url, TEST_URL);
    }

    #[fasync::run_singlethreaded(test)]
    async fn data_repo_tolerates_duplicate_new_component_insertions() {
        let mut data_repo = DataRepo::for_test();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });

        data_repo
            .add_new_component(component_id.clone(), TEST_URL, zx::Time::from_nanos(0), None)
            .expect("instantiated new component.");

        let duplicate_new_component_insertion = data_repo.add_new_component(
            component_id.clone(),
            TEST_URL,
            zx::Time::from_nanos(1),
            Some(zx::Time::from_nanos(0)),
        );

        assert!(duplicate_new_component_insertion.is_ok());

        let key = component_id.unique_key();
        let repo_values = data_repo.data_directories.get(key.clone()).unwrap().get_values();
        assert_eq!(repo_values.len(), 1);
        let entry = &repo_values[0];
        assert!(entry.lifecycle.is_some());
        let lifecycle_container = entry.lifecycle.as_ref().unwrap();
        assert!(lifecycle_container.component_start_time.is_none());
        assert_eq!(entry.relative_moniker, component_id.relative_moniker_for_selectors());
        assert_eq!(entry.component_url, TEST_URL);
    }

    #[fasync::run_singlethreaded(test)]
    async fn running_components_provide_start_time() {
        let mut data_repo = DataRepo::for_test();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });

        let component_insertion = data_repo.add_new_component(
            component_id.clone(),
            TEST_URL,
            zx::Time::from_nanos(1),
            Some(zx::Time::from_nanos(0)),
        );

        assert!(component_insertion.is_ok());

        let key = component_id.unique_key();
        let repo_values = data_repo.data_directories.get(key.clone()).unwrap().get_values();
        assert_eq!(repo_values.len(), 1);
        let entry = &repo_values[0];
        assert!(entry.lifecycle.is_some());
        let lifecycle_container = entry.lifecycle.as_ref().unwrap();
        assert!(lifecycle_container.component_start_time.is_some());
        assert_eq!(lifecycle_container.component_start_time.unwrap().into_nanos(), 0);
        assert_eq!(entry.relative_moniker, component_id.relative_moniker_for_selectors());
        assert_eq!(entry.component_url, TEST_URL);
    }

    #[fasync::run_singlethreaded(test)]
    async fn data_repo_tolerant_of_new_component_calls_if_diagnostics_ready_already_processed() {
        let mut data_repo = DataRepo::for_test();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });

        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        data_repo
            .add_inspect_artifacts(component_id.clone(), TEST_URL, proxy, zx::Time::from_nanos(0))
            .expect("add to repo");

        let false_new_component_result = data_repo.add_new_component(
            component_id.clone(),
            TEST_URL,
            zx::Time::from_nanos(0),
            None,
        );
        assert!(false_new_component_result.is_ok());

        // We shouldn't have overwritten the entry. There should still be an inspect
        // artifacts container.
        let key = component_id.unique_key();
        assert_eq!(data_repo.data_directories.get(key.clone()).unwrap().get_values().len(), 1);
        let entry = &data_repo.data_directories.get(key.clone()).unwrap().get_values()[0];
        assert_eq!(entry.component_url, TEST_URL);
        assert!(entry.inspect.is_some());
        assert!(entry.lifecycle.is_some());
    }

    #[fasync::run_singlethreaded(test)]
    async fn diagnostics_repo_cant_have_more_than_one_diagnostics_data_container_per_component() {
        let mut data_repo = DataRepo::for_test();
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });

        data_repo
            .add_new_component(component_id.clone(), TEST_URL, zx::Time::from_nanos(0), None)
            .expect("insertion will succeed.");

        let key = component_id.unique_key();
        assert_eq!(data_repo.data_directories.get(key.clone()).unwrap().get_values().len(), 1);

        let mutable_values =
            data_repo.data_directories.get_mut(key.clone()).unwrap().get_values_mut();
        mutable_values.push(ComponentDiagnostics {
            relative_moniker: component_id.relative_moniker_for_selectors(),
            component_url: TEST_URL.to_string(),
            inspect: None,
            lifecycle: None,
        });

        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        assert!(data_repo
            .add_inspect_artifacts(component_id.clone(), TEST_URL, proxy, zx::Time::from_nanos(0))
            .is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn data_repo_filters_inspect_by_selectors() {
        let data_repo = Arc::new(RwLock::new(DataRepo::for_test()));
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path: realm_path.clone(),
            component_name: "foo.cmx".into(),
        });

        data_repo
            .write()
            .add_new_component(component_id.clone(), TEST_URL, zx::Time::from_nanos(0), None)
            .expect("insertion will succeed.");

        data_repo
            .write()
            .add_inspect_artifacts(
                component_id.clone(),
                TEST_URL,
                io_util::open_directory_in_namespace("/tmp", io_util::OPEN_RIGHT_READABLE)
                    .expect("open root"),
                zx::Time::from_nanos(0),
            )
            .expect("add inspect artifacts");

        let component_id2 = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id: "12345".to_string(),
            realm_path,
            component_name: "foo2.cmx".into(),
        });

        data_repo
            .write()
            .add_new_component(component_id2.clone(), TEST_URL, zx::Time::from_nanos(0), None)
            .expect("insertion will succeed.");

        data_repo
            .write()
            .add_inspect_artifacts(
                component_id2.clone(),
                TEST_URL,
                io_util::open_directory_in_namespace("/tmp", io_util::OPEN_RIGHT_READABLE)
                    .expect("open root"),
                zx::Time::from_nanos(0),
            )
            .expect("add inspect artifacts");

        assert_eq!(2, data_repo.read().fetch_inspect_data(&None, None).len());

        let selectors = Some(vec![Arc::new(
            selectors::parse_selector("a/b/foo.cmx:root").expect("parse selector"),
        )]);
        assert_eq!(1, data_repo.read().fetch_inspect_data(&selectors, None).len());

        let selectors = Some(vec![Arc::new(
            selectors::parse_selector("a/b/f*.cmx:root").expect("parse selector"),
        )]);
        assert_eq!(2, data_repo.read().fetch_inspect_data(&selectors, None).len());

        let selectors = Some(vec![Arc::new(
            selectors::parse_selector("foo.cmx:root").expect("parse selector"),
        )]);
        assert_eq!(0, data_repo.read().fetch_inspect_data(&selectors, None).len());
    }
}
