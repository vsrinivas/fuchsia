// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_diagnostics::{Selector, StringSelector, TreeSelector},
    fidl_fuchsia_io as io,
    selectors::{
        convert_string_selector_to_regex, sanitize_string_for_selectors, WILDCARD_REGEX_EQUIVALENT,
    },
    std::path::PathBuf,
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
enum SelectorType {
    Topology,
    ComponentNamespace,
    HubPath,
}

#[derive(Clone)]
struct SelectorEntry<'a> {
    selector: &'a StringSelector,
    selector_type: SelectorType,
}

impl SelectorEntry<'_> {
    pub fn new(selector: &StringSelector, selector_type: SelectorType) -> SelectorEntry<'_> {
        return SelectorEntry { selector, selector_type };
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct PathEntry {
    pub hub_path: PathBuf,
    pub topological_path: PathBuf,
}

impl PathEntry {
    fn new(hub_path: PathBuf, topological_path: PathBuf) -> PathEntry {
        return PathEntry { hub_path, topological_path };
    }

    fn clone_push(&mut self, name: &str, path_type: SelectorType) -> Self {
        let mut new_entry = self.clone();
        match path_type {
            SelectorType::Topology => {
                new_entry.push_topological_dir(name);
            }
            SelectorType::ComponentNamespace => {
                new_entry.push_topological_dir(name);
            }
            SelectorType::HubPath => {
                new_entry.push_hub_dir(name);
            }
        }

        new_entry
    }

    fn push_hub_dir(&mut self, name: &str) {
        self.hub_path.push(name);
    }

    fn push_topological_dir(&mut self, name: &str) {
        self.push_hub_dir(name);
        self.topological_path.push(name);
    }

    pub fn topological_str(&self) -> String {
        self.topological_path.to_string_lossy().into_owned()
    }
}

pub async fn get_matching_paths(root: &str, selector: &Selector) -> Result<Vec<PathEntry>, Error> {
    let segments = selector.component_selector.as_ref().unwrap().moniker_segments.as_ref().unwrap();
    let mut selectors = segments
        .iter()
        .map(|s| SelectorEntry::new(&s, SelectorType::Topology))
        .collect::<Vec<SelectorEntry<'_>>>();

    selectors.push(SelectorEntry::new(&EXEC_SELECTOR, SelectorType::HubPath));

    let node_path: StringSelector;
    let prop_path: StringSelector;
    match selector.tree_selector.as_ref().unwrap() {
        TreeSelector::SubtreeSelector(s) => {
            if s.node_path.is_empty() {}
            node_path = clone_selector(s.node_path.get(0).unwrap());
            selectors.extend(vec![
                SelectorEntry::new(&node_path, SelectorType::ComponentNamespace),
                SelectorEntry::new(&SVC_SELECTOR, SelectorType::HubPath),
                SelectorEntry::new(&WILDCARD_SELECTOR, SelectorType::ComponentNamespace),
            ]);
        }
        TreeSelector::PropertySelector(s) => {
            node_path = clone_selector(s.node_path.get(0).unwrap());
            prop_path = clone_selector(&s.target_properties);
            selectors.extend(vec![
                SelectorEntry::new(&node_path, SelectorType::ComponentNamespace),
                SelectorEntry::new(&SVC_SELECTOR, SelectorType::HubPath),
                SelectorEntry::new(&prop_path, SelectorType::ComponentNamespace),
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
            if selector.selector_type == SelectorType::Topology {
                path.push_hub_dir("children");
            }

            let re = regex::Regex::new(
                // The Regex library will insert implicit ".*" on both sides of a regex.
                // We don't want that behavior - users can insert * if they want
                // fuzzy matches.
                &format!(
                    "^{}$",
                    convert_string_selector_to_regex(selector.selector, WILDCARD_REGEX_EQUIVALENT)
                        .unwrap()
                ),
            )
            .unwrap();

            let path_str = path.hub_path.to_string_lossy();
            let proxy = match io_util::open_directory_in_namespace(
                &path_str,
                io::OPEN_RIGHT_READABLE,
            ) {
                Ok(p) => p,
                Err(e) => {
                    log::warn!("got error trying to read directory {:?}. Ignoring this directory. Error was: {}", path_str, e);
                    continue;
                }
            };
            let entries = match files_async::readdir(&proxy).await {
                Ok(p) => p,
                Err(e) => {
                    log::warn!("got error trying to read entries in {:?}. Ignoring this directory. Error was: {}", path_str, e);
                    continue;
                }
            };

            for entry in entries {
                if re.is_match(&sanitize_string_for_selectors(&entry.name)) {
                    new_paths.push(path.clone_push(&entry.name, selector.selector_type));
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
    };

    fn create_files(paths: Vec<&PathBuf>) {
        for path in paths.iter() {
            log::info!("Creating {:?}", path);
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

        assert_same_paths(matches, vec![PathEntry::new(path, PathBuf::from("/a/b/out/myservice"))]);
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
                PathEntry::new(first_match, PathBuf::from("/a/b/out/myservice")),
                PathEntry::new(second_match, PathBuf::from("/a/b/out/myservice2")),
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
                PathEntry::new(first_match, PathBuf::from("/a/b/out/myservice")),
                PathEntry::new(second_match, PathBuf::from("/a/b/out/myservice2")),
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
                PathEntry::new(first_match, PathBuf::from("/a/b/out/myservice")),
                PathEntry::new(second_match, PathBuf::from("/c/d/out/myservice2")),
            ],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_match_wildcard_service_dir() {
        let temp = tempfile::tempdir().unwrap().into_path();
        let base = temp.join("children");
        let first_match = base.join("a/children/b/exec/out/svc/myservice");
        let second_match = base.join("a/children/b/exec/exposed/svc/myservice");
        let third_match = base.join("a/children/b/exec/in/svc/myservice");
        create_files(vec![
            &base.join("b/children/b/exec/in/svc/myservice"),
            &first_match,
            &second_match,
            &third_match,
        ]);

        let matches = exec_selector(temp, "a/b:*:myservice").await;

        assert_same_paths(
            matches,
            vec![
                PathEntry::new(first_match, PathBuf::from("/a/b/out/myservice")),
                PathEntry::new(second_match, PathBuf::from("/a/b/exposed/myservice")),
                PathEntry::new(third_match, PathBuf::from("/a/b/in/myservice")),
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
                PathEntry::new(first_match, PathBuf::from("/a/b/out/myservice")),
                PathEntry::new(second_match, PathBuf::from("/a/b/out/myservice2")),
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
                PathEntry::new(first_match, PathBuf::from("/a/b/out/myservice")),
                PathEntry::new(second_match, PathBuf::from("/a/b/in/myservice2")),
            ],
        );
    }
}
