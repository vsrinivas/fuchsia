// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl::endpoints::{ProtocolMarker, Proxy},
    fidl_fuchsia_developer_remotecontrol::ServiceMatch,
    fidl_fuchsia_diagnostics::{Selector, StringSelector, TreeSelector},
    fidl_fuchsia_io as io, fidl_fuchsia_sys2 as fsys,
    selectors::match_selector_against_single_node,
    std::path::{Component, PathBuf},
    tracing::{info, warn},
};

lazy_static::lazy_static! {
    static ref EXEC_SELECTOR: StringSelector = StringSelector::ExactMatch("exec".to_owned());
    static ref SVC_SELECTOR: StringSelector = StringSelector::ExactMatch("svc".to_owned());
    static ref WILDCARD_SELECTOR: StringSelector = StringSelector::StringPattern("*".to_owned());
}

fn clone_selector(selector: &StringSelector) -> StringSelector {
    match selector {
        StringSelector::ExactMatch(s) => {
            return StringSelector::ExactMatch(s.to_owned());
        }
        StringSelector::StringPattern(s) => {
            return StringSelector::StringPattern(s.to_owned());
        }
        _ => {
            panic!("clone_selector got unexpected StringSelector variant");
        }
    }
}

#[derive(Copy, Clone, PartialEq)]
enum EntryType {
    Moniker,
    ComponentSubdir,
    ServiceName,
    HubPath,
}

#[derive(Clone)]
struct SelectorEntry<'a> {
    selector: &'a StringSelector,
    selector_type: EntryType,
}

impl SelectorEntry<'_> {
    pub fn new(selector: &StringSelector, selector_type: EntryType) -> SelectorEntry<'_> {
        return SelectorEntry { selector, selector_type };
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct PathEntry {
    pub hub_path: PathBuf,
    pub moniker: PathBuf,
    pub component_subdir: String,
    pub service: String,
    pub debug_hub_path: Option<PathBuf>,
}

impl PathEntry {
    fn new(hub_path: PathBuf, moniker: PathBuf) -> PathEntry {
        return PathEntry {
            hub_path,
            moniker,
            component_subdir: String::default(),
            service: String::default(),
            debug_hub_path: None,
        };
    }

    #[cfg(test)]
    fn new_with_service(
        hub_path: PathBuf,
        moniker: PathBuf,
        subdir: &str,
        service: &str,
    ) -> PathEntry {
        return PathEntry {
            hub_path,
            moniker,
            component_subdir: subdir.to_string(),
            service: service.to_string(),
            debug_hub_path: None,
        };
    }

    fn clone_push(&mut self, name: &str, path_type: EntryType) -> Self {
        let mut new_entry = self.clone();
        match path_type {
            EntryType::Moniker => {
                new_entry.push_moniker(name);
            }
            EntryType::ComponentSubdir => {
                new_entry.push_subdir(name);
            }
            EntryType::ServiceName => {
                new_entry.push_service(name);
            }
            EntryType::HubPath => {
                new_entry.push_hub_dir(name);
            }
        }

        new_entry
    }

    fn push_hub_dir(&mut self, name: &str) {
        self.hub_path.push(name);
    }

    fn push_moniker(&mut self, name: &str) {
        self.push_hub_dir(name);
        self.moniker.push(name);
    }

    fn push_subdir(&mut self, name: &str) {
        self.push_hub_dir(name);
        self.component_subdir = name.to_owned();
    }

    fn push_service(&mut self, name: &str) {
        self.push_hub_dir(name);
        self.service = name.to_owned();
    }

    fn set_debug_hub_path(&mut self, path: PathBuf) {
        self.debug_hub_path.replace(path);
    }
}

impl Into<ServiceMatch> for &PathEntry {
    fn into(self) -> ServiceMatch {
        ServiceMatch {
            moniker: self
                .moniker
                .components()
                .filter(|c| match c {
                    Component::Normal(_) => true,
                    _ => false,
                })
                .map(|c| {
                    match c {
                        Component::Normal(p) => p.to_string_lossy().into_owned(),
                        // The compiler forces an additional case here, even though the others have
                        // been filtered out above.
                        _ => String::default(),
                    }
                })
                .collect(),
            subdir: self.component_subdir.clone(),
            service: self.service.clone(),
        }
    }
}

async fn connect_and_read_dir(hub_path: &PathBuf) -> Result<Vec<files_async::DirEntry>, Error> {
    let path_str = hub_path.to_string_lossy();
    let proxy = io_util::open_directory_in_namespace(&path_str, io::OPEN_RIGHT_READABLE)?;
    files_async::readdir(&proxy).await.map_err(Into::into)
}

pub async fn get_matching_paths(root: &str, selector: &Selector) -> Result<Vec<PathEntry>, Error> {
    let segments = selector.component_selector.as_ref().unwrap().moniker_segments.as_ref().unwrap();
    let mut selectors = segments
        .iter()
        .map(|s| SelectorEntry::new(&s, EntryType::Moniker))
        .collect::<Vec<SelectorEntry<'_>>>();

    let node_path: StringSelector;
    let prop_path: StringSelector;

    match selector.tree_selector.as_ref().unwrap() {
        TreeSelector::SubtreeSelector(s) => {
            if s.node_path.is_empty() {}
            node_path = clone_selector(s.node_path.get(0).unwrap());
            selectors.extend(vec![
                SelectorEntry::new(&node_path, EntryType::ComponentSubdir),
                SelectorEntry::new(&WILDCARD_SELECTOR, EntryType::ServiceName),
            ]);
        }
        TreeSelector::PropertySelector(s) => {
            node_path = clone_selector(s.node_path.get(0).unwrap());
            prop_path = clone_selector(&s.target_properties);
            selectors.extend(vec![
                SelectorEntry::new(&node_path, EntryType::ComponentSubdir),
                SelectorEntry::new(&prop_path, EntryType::ServiceName),
            ]);
        }
        _ => {
            panic!("unexpected enum value");
        }
    }

    let mut paths = vec![PathEntry::new(PathBuf::from(root), PathBuf::from("/"))];
    for selector in selectors.iter() {
        let mut new_paths = vec![];
        for path in paths.clone().iter_mut() {
            if selector.selector_type == EntryType::Moniker {
                path.push_hub_dir("children");
            } else if selector.selector_type == EntryType::ComponentSubdir {
                if match_selector_against_single_node(&"expose".to_string(), selector.selector)? {
                    let hub_path = path.hub_path.clone();
                    let mut p = path.clone_push("resolved", EntryType::HubPath);
                    p.push_subdir("expose");
                    p.set_debug_hub_path(hub_path.join("debug"));
                    new_paths.push(p);
                }
                if match_selector_against_single_node(&"out".to_string(), selector.selector)? {
                    let mut p = path.clone_push("exec", EntryType::HubPath);
                    p.push_subdir("out");
                    p.push_hub_dir("svc");
                    new_paths.push(p);
                }
                if match_selector_against_single_node(&"in".to_string(), selector.selector)? {
                    let mut p = path.clone_push("exec", EntryType::HubPath);
                    p.push_subdir("in");
                    p.push_hub_dir("svc");
                    new_paths.push(p);
                }
                continue;
            }

            let entries = match connect_and_read_dir(&path.hub_path).await {
                Ok(e) => e,
                Err(_) => {
                    if path
                        .hub_path
                        .file_name()
                        .context("missing file name")?
                        .to_string_lossy()
                        .to_string()
                        == "expose".to_string()
                    {
                        let lifecycle_controller_path = path
                            .debug_hub_path
                            .as_ref()
                            .context("missing debug path")?
                            .join(fsys::LifecycleControllerMarker::NAME);
                        let node_proxy = io_util::open_node_in_namespace(
                            lifecycle_controller_path.to_str().expect("invalid chars"),
                            io::OPEN_RIGHT_READABLE,
                        )?;
                        let lifecycle_controller_proxy = fsys::LifecycleControllerProxy::new(
                            node_proxy.into_channel().expect("could not get channel from proxy"),
                        );
                        match lifecycle_controller_proxy.resolve(".").await {
                            Ok(_) => {
                                info!(
                                    "successfully resolved component {}",
                                    path.moniker.to_str().unwrap()
                                )
                            }
                            Err(e) => {
                                warn!(%e, directory=%path.hub_path.to_string_lossy(), "failed to resolve component.");
                                continue;
                            }
                        };
                    }

                    match connect_and_read_dir(&path.hub_path).await {
                        Ok(e) => e,
                        Err(e) => {
                            warn!(directory = ?path.hub_path, %e, "got error trying to read resolve directory after calling resolve. Ignoring this directory");
                            continue;
                        }
                    }
                }
            };

            for entry in entries {
                if match_selector_against_single_node(&entry.name, selector.selector)? {
                    let mut new_path = path.clone_push(&entry.name, selector.selector_type);
                    if selector.selector_type == EntryType::ComponentSubdir
                        && new_path.component_subdir != "expose"
                    {
                        new_path.push_hub_dir("svc");
                    }
                    new_paths.push(new_path);
                }
            }
        }
        if new_paths.is_empty() {
            return Ok(vec![]);
        }
        paths = new_paths;
    }

    return Ok(paths);
}

#[cfg(test)]
mod test {
    use {
        super::*,
        fuchsia_async as fasync,
        std::collections::HashSet,
        std::fs::{create_dir_all, write},
        std::path::PathBuf,
        tracing::info,
    };

    fn create_files(paths: Vec<&PathBuf>) {
        for path in paths.iter() {
            info!(?path, "Creating path");
            create_dir_all(&path.parent().unwrap()).unwrap();
            write(&path, "").unwrap();
        }
    }

    async fn exec_selector(base: PathBuf, selector_str: &str) -> Vec<PathEntry> {
        let selector = selectors::parse_selector(selector_str).unwrap();

        get_matching_paths(&base.to_string_lossy(), &selector).await.unwrap()
    }

    fn assert_same_paths(matches: Vec<PathEntry>, expected: Vec<PathEntry>) {
        assert_eq!(matches.len(), expected.len());

        let match_set: HashSet<&PathEntry> = matches.iter().collect();
        let expected_set: HashSet<&PathEntry> = expected.iter().collect();

        assert_eq!(match_set, expected_set);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_match_simple() {
        let temp = tempfile::tempdir().unwrap().into_path();
        let path = temp.join("children").join("a/children/b/exec/out/svc/myservice");
        create_files(vec![&path]);

        let matches = exec_selector(temp, "a/b:out:myservice").await;

        assert_same_paths(
            matches,
            vec![PathEntry::new_with_service(path, PathBuf::from("/a/b"), "out", "myservice")],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_match_wildcard_services() {
        let temp = tempfile::tempdir().unwrap().into_path();
        let base = temp.join("children");
        let not_match = base.join("a/children/b/exec/out/svc/notmyservice");
        let first_match = base.join("a/children/b/exec/out/svc/myservice");
        let second_match = base.join("a/children/b/exec/out/svc/myservice2");
        create_files(vec![&not_match, &first_match, &second_match]);

        let matches = exec_selector(temp, "a/b:out:my*").await;

        assert_same_paths(
            matches,
            vec![
                PathEntry::new_with_service(first_match, PathBuf::from("/a/b"), "out", "myservice"),
                PathEntry::new_with_service(
                    second_match,
                    PathBuf::from("/a/b"),
                    "out",
                    "myservice2",
                ),
            ],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_match_wildcard_moniker_and_service() {
        let temp = tempfile::tempdir().unwrap().into_path();
        let base = temp.join("children");
        let first_match = base.join("a/children/b/exec/out/svc/myservice");
        let second_match = base.join("a/children/b/exec/out/svc/myservice2");
        create_files(vec![
            &base.join("a/children/b/exec/out/svc/notmyservice"),
            &base.join("b/children/b/exec/out/svc/myservice"),
            &first_match,
            &second_match,
        ]);

        let matches = exec_selector(temp, "a/*:out:my*").await;

        assert_same_paths(
            matches,
            vec![
                PathEntry::new_with_service(first_match, PathBuf::from("/a/b"), "out", "myservice"),
                PathEntry::new_with_service(
                    second_match,
                    PathBuf::from("/a/b"),
                    "out",
                    "myservice2",
                ),
            ],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_nested_wildcard_moniker() {
        let temp = tempfile::tempdir().unwrap().into_path();
        let base = temp.join("children");
        let first_match = base.join("a/children/b/exec/out/svc/myservice");
        let second_match = base.join("c/children/d/exec/out/svc/myservice2");
        create_files(vec![
            &base.join("e/children/f/children/g/exec/in/svc/myservice"),
            &first_match,
            &second_match,
        ]);

        let matches = exec_selector(temp, "*/*:*:*").await;

        assert_same_paths(
            matches,
            vec![
                PathEntry::new_with_service(first_match, PathBuf::from("/a/b"), "out", "myservice"),
                PathEntry::new_with_service(
                    second_match,
                    PathBuf::from("/c/d"),
                    "out",
                    "myservice2",
                ),
            ],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_match_wildcard_service_dir() {
        let temp = tempfile::tempdir().unwrap().into_path();
        let base = temp.join("children");
        let first_match = base.join("a/children/b/exec/out/svc/myservice");
        let second_match = base.join("a/children/b/exec/in/svc/myservice");
        let third_match = base.join("a/children/b/resolved/expose/myservice");

        create_files(vec![
            &base.join("b/children/b/exec/in/svc/myservice"),
            &first_match,
            &second_match,
            &third_match,
        ]);

        let debug_hub_path = base.join("a/children/b/debug");
        let mut expose_entry =
            PathEntry::new_with_service(third_match, PathBuf::from("/a/b"), "expose", "myservice");
        expose_entry.set_debug_hub_path(debug_hub_path);

        let matches = exec_selector(temp, "a/b:*:myservice").await;

        assert_same_paths(
            matches,
            vec![
                PathEntry::new_with_service(first_match, PathBuf::from("/a/b"), "out", "myservice"),
                PathEntry::new_with_service(second_match, PathBuf::from("/a/b"), "in", "myservice"),
                expose_entry,
            ],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_subtree_selector_exact() {
        let temp = tempfile::tempdir().unwrap().into_path();
        let base = temp.join("children");
        let first_match = base.join("a/children/b/exec/out/svc/myservice");
        let second_match = base.join("a/children/b/exec/out/svc/myservice2");
        create_files(vec![
            &base.join("a/children/b/exec/in/svc/myservice"),
            &first_match,
            &second_match,
        ]);

        let matches = exec_selector(temp, "a/b:out").await;

        assert_same_paths(
            matches,
            vec![
                PathEntry::new_with_service(first_match, PathBuf::from("/a/b"), "out", "myservice"),
                PathEntry::new_with_service(
                    second_match,
                    PathBuf::from("/a/b"),
                    "out",
                    "myservice2",
                ),
            ],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_subtree_selector_wildcard() {
        let temp = tempfile::tempdir().unwrap().into_path();
        let base = temp.join("children");
        let first_match = base.join("a/children/b/exec/out/svc/myservice");
        let second_match = base.join("a/children/b/exec/in/svc/myservice2");
        create_files(vec![
            &base.join("b/children/b/exec/in/svc/myservice"),
            &first_match,
            &second_match,
        ]);

        let matches = exec_selector(temp, "a/b:*").await;

        assert_same_paths(
            matches,
            vec![
                PathEntry::new_with_service(first_match, PathBuf::from("/a/b"), "out", "myservice"),
                PathEntry::new_with_service(
                    second_match,
                    PathBuf::from("/a/b"),
                    "in",
                    "myservice2",
                ),
            ],
        );
    }
}
