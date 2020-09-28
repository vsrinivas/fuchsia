// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        container::{
            DiagnosticsArtifactsContainer, InspectArtifactsContainer, LifecycleArtifactsContainer,
            LifecycleDataContainer, UnpopulatedInspectDataContainer,
        },
        events::types::ComponentIdentifier,
        logs::LogManager,
    },
    anyhow::{format_err, Error},
    fidl_fuchsia_diagnostics::{self, Selector},
    fidl_fuchsia_io::{DirectoryProxy, CLONE_FLAG_SAME_RIGHTS},
    fuchsia_inspect_node_hierarchy::trie,
    fuchsia_zircon as zx, io_util, selectors,
    std::convert::TryInto,
    std::sync::Arc,
};

pub type DiagnosticsDataTrie = trie::Trie<String, DiagnosticsArtifactsContainer>;

/// DiagnosticsDataRepository manages storage of all state needed in order
/// for the inspect reader to retrieve inspect data when a read is requested.
pub struct DiagnosticsDataRepository {
    pub data_directories: DiagnosticsDataTrie,
    log_manager: LogManager,

    /// Optional static selectors. For the all_access reader, there
    /// are no provided selectors. For all other pipelines, a non-empty
    /// vector is required.
    pub static_selectors: Option<Vec<Arc<Selector>>>,
}

impl DiagnosticsDataRepository {
    pub fn new(log_manager: LogManager, static_selectors: Option<Vec<Arc<Selector>>>) -> Self {
        DiagnosticsDataRepository {
            log_manager,
            data_directories: DiagnosticsDataTrie::new(),
            static_selectors: static_selectors,
        }
    }

    pub fn log_manager(&self) -> LogManager {
        self.log_manager.clone()
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
                let diag_repo_entry_values: &mut [DiagnosticsArtifactsContainer] =
                    diag_repo_entry.get_values_mut();

                match &mut diag_repo_entry_values[..] {
                    [] => {
                        // An entry with no values implies that the somehow we observed the
                        // creation of a component lower in the topology before observing this
                        // one. If this is the case, just instantiate as though it's our first
                        // time encountering this moniker segment.
                        self.data_directories.insert(
                            key,
                            DiagnosticsArtifactsContainer {
                                relative_moniker: relative_moniker,
                                component_url: component_url.into(),
                                lifecycle_artifacts_container: Some(lifecycle_artifact_container),
                                inspect_artifacts_container: None,
                            },
                        )
                    }
                    [existing_diagnostics_artifact_container] => {
                        // Races may occur between seeing diagnostics ready and seeing
                        // creation lifecycle events. Handle this here.
                        // TODO(fxbug.dev/52047): Remove once caching handles ordering issues.
                        if existing_diagnostics_artifact_container
                            .lifecycle_artifacts_container
                            .is_none()
                        {
                            existing_diagnostics_artifact_container.lifecycle_artifacts_container =
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
                DiagnosticsArtifactsContainer {
                    relative_moniker: relative_moniker,
                    component_url: component_url.into(),
                    lifecycle_artifacts_container: Some(lifecycle_artifact_container),
                    inspect_artifacts_container: None,
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

        // Create an optional inspect artifact container. If the option is None, this implies
        // that there existed static selectors, and none of them matched the relative moniker
        // of the component being inserted. So we can abort insertion.
        let inspect_artifact_container = match &self.static_selectors {
            Some(selectors) => {
                let matched_selectors = selectors::match_component_moniker_against_selectors(
                    &relative_moniker,
                    &selectors,
                )?;
                match &matched_selectors[..] {
                    [] => None,
                    populated_vec => Some(InspectArtifactsContainer {
                        component_diagnostics_proxy: Arc::new(directory_proxy),
                        inspect_matcher: Some((populated_vec).try_into()?),
                        event_timestamp,
                    }),
                }
            }
            None => Some(InspectArtifactsContainer {
                component_diagnostics_proxy: Arc::new(directory_proxy),
                inspect_matcher: None,
                event_timestamp,
            }),
        };

        match inspect_artifact_container {
            Some(inspect_container) => self.insert_inspect_artifact_container(
                inspect_container,
                key,
                relative_moniker,
                component_url.into(),
            ),
            // The Inspect artifact container being None here implies that
            // there were valid static selectors and none of them applied to
            // the component currently being processed.
            None => {
                return Ok(());
            }
        }
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
                let diag_repo_entry_values: &mut [DiagnosticsArtifactsContainer] =
                    diag_repo_entry.get_values_mut();

                match &mut diag_repo_entry_values[..] {
                    [] => {
                        // An entry with no values implies that the somehow we observed the
                        // creation of a component lower in the topology before observing this
                        // one. If this is the case, just instantiate as though it's our first
                        // time encountering this moniker segment.
                        self.data_directories.insert(
                            key,
                            DiagnosticsArtifactsContainer {
                                relative_moniker: relative_moniker,
                                component_url,
                                lifecycle_artifacts_container: None,
                                inspect_artifacts_container: Some(inspect_container),
                            },
                        )
                    }
                    [existing_diagnostics_artifact_container] => {
                        // Races may occur between synthesized and real diagnostics_ready
                        // events, so we must handle de-duplication here.
                        // TODO(fxbug.dev/52047): Remove once caching handles ordering issues.
                        if existing_diagnostics_artifact_container
                            .inspect_artifacts_container
                            .is_none()
                        {
                            // This is expected to be the most common case. We've encountered the
                            // diagnostics_ready event for a component that has already been
                            // observed to be started/existing. We now must update the diagnostics
                            // artifact container with the inspect artifacts that accompanied the
                            // diagnostics_ready event.
                            existing_diagnostics_artifact_container.inspect_artifacts_container =
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
                DiagnosticsArtifactsContainer {
                    relative_moniker: relative_moniker,
                    component_url,
                    lifecycle_artifacts_container: None,
                    inspect_artifacts_container: Some(inspect_container),
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
                            &diagnostics_artifacts_container.lifecycle_artifacts_container
                        {
                            acc.push(LifecycleDataContainer::from_lifecycle_artifact(
                                lifecycle_artifacts,
                                diagnostics_artifacts_container.relative_moniker.clone(),
                                diagnostics_artifacts_container.component_url.clone(),
                            ));
                        }

                        if let Some(inspect_artifacts) =
                            &diagnostics_artifacts_container.inspect_artifacts_container
                        {
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
    ) -> Vec<UnpopulatedInspectDataContainer> {
        return self
            .data_directories
            .iter()
            .filter_map(|(_, diagnostics_artifacts_container_opt)| {
                let (diagnostics_artifacts_container, inspect_artifacts) =
                    match &diagnostics_artifacts_container_opt {
                        Some(diagnostics_artifacts_container) => {
                            match &diagnostics_artifacts_container.inspect_artifacts_container {
                                Some(inspect_artifacts) => {
                                    (diagnostics_artifacts_container, inspect_artifacts)
                                }
                                None => return None,
                            }
                        }
                        None => return None,
                    };

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
                    inspect_matcher: inspect_artifacts.inspect_matcher.clone(),
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
        fidl_fuchsia_io::DirectoryMarker,
        fuchsia_async as fasync,
        fuchsia_inspect_node_hierarchy::trie::TrieIterableNode,
        fuchsia_zircon as zx,
    };

    const TEST_URL: &'static str = "fuchsia-pkg://test";

    #[fasync::run_singlethreaded(test)]
    async fn inspect_repo_disallows_duplicated_dirs() {
        let mut inspect_repo = DiagnosticsDataRepository::new(LogManager::new(), None);
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
        let mut data_repo = DiagnosticsDataRepository::new(LogManager::new(), None);
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
        assert!(entry.inspect_artifacts_container.is_some());
        assert_eq!(entry.component_url, TEST_URL);
    }

    #[fasync::run_singlethreaded(test)]
    async fn data_repo_tolerates_duplicate_new_component_insertions() {
        let mut data_repo = DiagnosticsDataRepository::new(LogManager::new(), None);
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
        assert!(entry.lifecycle_artifacts_container.is_some());
        let lifecycle_container = entry.lifecycle_artifacts_container.as_ref().unwrap();
        assert!(lifecycle_container.component_start_time.is_none());
        assert_eq!(entry.relative_moniker, component_id.relative_moniker_for_selectors());
        assert_eq!(entry.component_url, TEST_URL);
    }

    #[fasync::run_singlethreaded(test)]
    async fn running_components_provide_start_time() {
        let mut data_repo = DiagnosticsDataRepository::new(LogManager::new(), None);
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
        assert!(entry.lifecycle_artifacts_container.is_some());
        let lifecycle_container = entry.lifecycle_artifacts_container.as_ref().unwrap();
        assert!(lifecycle_container.component_start_time.is_some());
        assert_eq!(lifecycle_container.component_start_time.unwrap().into_nanos(), 0);
        assert_eq!(entry.relative_moniker, component_id.relative_moniker_for_selectors());
        assert_eq!(entry.component_url, TEST_URL);
    }

    #[fasync::run_singlethreaded(test)]
    async fn data_repo_tolerant_of_new_component_calls_if_diagnostics_ready_already_processed() {
        let mut data_repo = DiagnosticsDataRepository::new(LogManager::new(), None);
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
        assert!(entry.inspect_artifacts_container.is_some());
        assert!(entry.lifecycle_artifacts_container.is_some());
    }

    #[fasync::run_singlethreaded(test)]
    async fn diagnostics_repo_cant_have_more_than_one_diagnostics_data_container_per_component() {
        let mut data_repo = DiagnosticsDataRepository::new(LogManager::new(), None);
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
        mutable_values.push(DiagnosticsArtifactsContainer {
            relative_moniker: component_id.relative_moniker_for_selectors(),
            component_url: TEST_URL.to_string(),
            inspect_artifacts_container: None,
            lifecycle_artifacts_container: None,
        });

        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");

        assert!(data_repo
            .add_inspect_artifacts(component_id.clone(), TEST_URL, proxy, zx::Time::from_nanos(0))
            .is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn data_repo_filters_inspect_by_selectors() {
        let mut data_repo = DiagnosticsDataRepository::new(LogManager::new(), None);
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path: realm_path.clone(),
            component_name: "foo.cmx".into(),
        });

        data_repo
            .add_new_component(component_id.clone(), TEST_URL, zx::Time::from_nanos(0), None)
            .expect("insertion will succeed.");

        data_repo
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
            .add_new_component(component_id2.clone(), TEST_URL, zx::Time::from_nanos(0), None)
            .expect("insertion will succeed.");

        data_repo
            .add_inspect_artifacts(
                component_id2.clone(),
                TEST_URL,
                io_util::open_directory_in_namespace("/tmp", io_util::OPEN_RIGHT_READABLE)
                    .expect("open root"),
                zx::Time::from_nanos(0),
            )
            .expect("add inspect artifacts");

        assert_eq!(2, data_repo.fetch_inspect_data(&None).len());

        let selectors = Some(vec![Arc::new(
            selectors::parse_selector("a/b/foo.cmx:root").expect("parse selector"),
        )]);
        assert_eq!(1, data_repo.fetch_inspect_data(&selectors).len());

        let selectors = Some(vec![Arc::new(
            selectors::parse_selector("a/b/f*.cmx:root").expect("parse selector"),
        )]);
        assert_eq!(2, data_repo.fetch_inspect_data(&selectors).len());

        let selectors = Some(vec![Arc::new(
            selectors::parse_selector("foo.cmx:root").expect("parse selector"),
        )]);
        assert_eq!(0, data_repo.fetch_inspect_data(&selectors).len());
    }
}
