// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    anyhow::Result,
    futures::future::{join, join3, join_all, BoxFuture, FutureExt},
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMoniker, ChildMonikerBase},
    std::str::FromStr,
};

#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};

/// Filters that can be applied when listing components
#[derive(Debug, PartialEq)]
pub enum ListFilter {
    CMX,
    CML,
    Running,
    Stopped,

    /// Filters components that are an ancestor of the component with the given name.
    /// Includes the named component.
    Ancestor(String),
    /// Filters components that are a descendant of the component with the given name.
    /// Includes the named component.
    Descendant(String),
    /// Filters components that are a relative (either an ancestor or a descendant) of the
    /// component with the given name. Includes the named component.
    Relative(String),
    None,
}

impl FromStr for ListFilter {
    type Err = &'static str;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "cmx" => Ok(ListFilter::CMX),
            "cml" => Ok(ListFilter::CML),
            "running" => Ok(ListFilter::Running),
            "stopped" => Ok(ListFilter::Stopped),
            filter => match filter.split_once(":") {
                Some((function, arg)) => match function {
                    "ancestor" | "ancestors" => Ok(ListFilter::Ancestor(arg.to_string())),
                    "descendant" | "descendants" => Ok(ListFilter::Descendant(arg.to_string())),
                    "relative" | "relatives" => Ok(ListFilter::Relative(arg.to_string())),
                    _ => Err("unknown function for list filter."),
                },
                None => Err("list filter should be 'cmx', 'cml', 'running', 'stopped', 'ancestors:<component_name>', 'descendants:<component_name>', or 'relatives:<component_name>'."),
            },
        }
    }
}

/// Basic information about a component for the `list` command.
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
pub struct Component {
    // Name (ie `ChildMoniker`) of the component. This gets printed out.
    pub name: String,

    // Moniker of the component.
    pub moniker: AbsoluteMoniker,

    // True if component is of appmgr/CMX type.
    // False if it is of the component_manager/CML type.
    pub is_cmx: bool,

    // CML components may not always be running.
    // Always true for CMX components.
    pub is_running: bool,

    // URL of the component.
    pub url: String,

    // Children of this component.
    pub children: Vec<Component>,

    // Names of all ancestors of this component, ordered from oldest to youngest.
    pub ancestors: Vec<String>,
}

impl Component {
    /// Recursive helper method for parse that accumulates the ancestors of the component.
    fn parse_recursive(
        child_moniker: String,
        abs_moniker: AbsoluteMoniker,
        hub_dir: Directory,
        ancestors: Vec<String>,
    ) -> BoxFuture<'static, Result<Component>> {
        async move {
            let exec_fut = hub_dir.exists("exec");
            let moniker_fut = hub_dir.read_file("moniker");
            let url_fut = hub_dir.read_file("url");
            let (exec_res, moniker_res, url_res) = join3(exec_fut, moniker_fut, url_fut).await;
            // TODO(fxbug.dev/96102): enforce the presence of the moniker file and remove
            // the `unwrap_or()` fallback behavior
            let (is_running, child_moniker, url) =
                (exec_res?, moniker_res.unwrap_or(child_moniker), url_res?);

            // Add this component to the list of ancestors for its children.
            let mut child_ancestors = ancestors.clone();
            child_ancestors.extend([child_moniker.clone()]);

            // Recurse on the CML children
            let mut future_children = vec![];
            let children_dir = hub_dir.open_dir_readable("children")?;
            for child_dir in children_dir.entries().await? {
                let hub_dir = children_dir.open_dir_readable(&child_dir)?;
                let future_moniker = abs_moniker.child(ChildMoniker::parse(&child_dir)?);
                let future_child = Component::parse_recursive(
                    child_dir,
                    future_moniker,
                    hub_dir,
                    child_ancestors.clone(),
                );
                future_children.push(future_child);
            }

            let results = join_all(future_children).await;
            let mut children = vec![];
            for result in results {
                let component = result?;
                children.push(component);
            }

            if child_moniker == "appmgr" {
                // Get all CMX components + realms
                let realm_dir = hub_dir.open_dir_readable("exec/out/hub")?;
                let component = Component::parse_cmx_realm(
                    "".to_string(),
                    abs_moniker.clone(),
                    url.clone(),
                    realm_dir,
                    child_ancestors,
                )
                .await?;
                children.extend(component.children);
            }

            Ok(Component {
                name: child_moniker,
                moniker: abs_moniker,
                children,
                is_cmx: false,
                url,
                is_running,
                ancestors,
            })
        }
        .boxed()
    }

    pub fn parse(
        root_moniker: String,
        moniker: AbsoluteMoniker,
        hub_dir: Directory,
    ) -> BoxFuture<'static, Result<Component>> {
        Component::parse_recursive(root_moniker, moniker, hub_dir, Vec::<String>::new())
    }

    fn parse_cmx_component(
        name: String,
        moniker: AbsoluteMoniker,
        dir: Directory,
        ancestors: Vec<String>,
    ) -> BoxFuture<'static, Result<Component>> {
        async move {
            // Runner CMX components may have child components

            let url = dir.read_file("url").await?;

            let children = if dir.exists("c").await? {
                let children_dir = dir.open_dir_readable("c")?;

                let mut child_ancestors = ancestors.clone();
                child_ancestors.extend([name.clone()]);

                Component::parse_cmx_components_in_c_dir(
                    children_dir,
                    moniker.clone(),
                    child_ancestors,
                )
                .await?
            } else {
                vec![]
            };

            // CMX components are always running if they exist in tree.
            Ok(Component {
                name,
                moniker,
                children,
                is_cmx: true,
                url,
                is_running: true,
                ancestors,
            })
        }
        .boxed()
    }

    fn parse_cmx_realm(
        realm_name: String,
        moniker: AbsoluteMoniker,
        url: String,
        realm_dir: Directory,
        ancestors: Vec<String>,
    ) -> BoxFuture<'static, Result<Component>> {
        async move {
            let children_dir = realm_dir.open_dir_readable("c")?;
            let realms_dir = realm_dir.open_dir_readable("r")?;

            let future_children = Component::parse_cmx_components_in_c_dir(
                children_dir,
                moniker.clone(),
                ancestors.clone(),
            );
            let future_realms = Component::parse_cmx_realms_in_r_dir(
                realms_dir,
                moniker.clone(),
                ancestors.clone(),
            );

            let (children, realms) = join(future_children, future_realms).await;
            let mut children = children?;
            let mut realms = realms?;

            children.append(&mut realms);

            // Consider a realm as a CMX component that has children.
            // This is not technically true, but works as a generalization.
            Ok(Component {
                name: realm_name,
                moniker,
                children,
                is_cmx: true,
                url,
                is_running: true,
                ancestors,
            })
        }
        .boxed()
    }

    async fn parse_cmx_components_in_c_dir(
        children_dir: Directory,
        moniker: AbsoluteMoniker,
        ancestors: Vec<String>,
    ) -> Result<Vec<Component>> {
        // Get all CMX child components
        let child_component_names = children_dir.entries().await?;
        let mut future_children = vec![];
        for child_component_name in child_component_names {
            let child_moniker = ChildMoniker::parse(&child_component_name)?;
            let child_moniker = moniker.child(child_moniker);
            let job_ids_dir = children_dir.open_dir_readable(&child_component_name)?;
            let child_dirs = Component::open_all_job_ids(job_ids_dir).await?;
            for child_dir in child_dirs {
                let future_child = Component::parse_cmx_component(
                    child_component_name.clone(),
                    child_moniker.clone(),
                    child_dir,
                    ancestors.clone(),
                );
                future_children.push(future_child);
            }
        }

        let results = join_all(future_children).await;
        results.into_iter().collect()
    }

    async fn parse_cmx_realms_in_r_dir(
        realms_dir: Directory,
        moniker: AbsoluteMoniker,
        ancestors: Vec<String>,
    ) -> Result<Vec<Component>> {
        // Get all CMX child realms
        let mut future_realms = vec![];
        for child_realm_name in realms_dir.entries().await? {
            let child_moniker = ChildMoniker::parse(&child_realm_name)?;
            let child_moniker = moniker.child(child_moniker);
            let job_ids_dir = realms_dir.open_dir_readable(&child_realm_name)?;
            let child_realm_dirs = Component::open_all_job_ids(job_ids_dir).await?;
            for child_realm_dir in child_realm_dirs {
                let future_realm = Component::parse_cmx_realm(
                    child_realm_name.clone(),
                    child_moniker.clone(),
                    "".to_string(), // cmx realms don't have a url
                    child_realm_dir,
                    ancestors.clone(),
                );
                future_realms.push(future_realm);
            }
        }
        let results = join_all(future_realms).await;
        results.into_iter().collect()
    }

    async fn open_all_job_ids(job_ids_dir: Directory) -> Result<Vec<Directory>> {
        // Recurse on the job_ids
        let mut dirs = vec![];
        for job_id in job_ids_dir.entries().await? {
            let dir = job_ids_dir.open_dir_readable(&job_id)?;
            dirs.push(dir);
        }
        Ok(dirs)
    }

    pub fn should_include(&self, list_filter: &ListFilter) -> bool {
        match list_filter {
            ListFilter::CML => !self.is_cmx,
            ListFilter::CMX => self.is_cmx,
            ListFilter::Running => self.is_running,
            ListFilter::Stopped => !self.is_running,

            ListFilter::Descendant(name) => {
                self.name == name.to_string() || self.has_ancestor(name.to_string())
            }

            // These algorithms will walk most of the graph for most of the components.
            // This works for topologies with only a few layers but could be optimized.
            ListFilter::Relative(name) => {
                self.name == name.to_string()
                    || self.has_ancestor(name.to_string())
                    || self.has_descendant(name.to_string())
            }
            ListFilter::Ancestor(name) => {
                self.name == name.to_string() || self.has_descendant(name.to_string())
            }

            ListFilter::None => true,
        }
    }

    fn has_ancestor(&self, component_name: String) -> bool {
        self.ancestors.contains(&component_name)
    }

    fn has_descendant(&self, component_name: String) -> bool {
        self.children
            .iter()
            .any(|c| c.name == component_name || c.has_descendant(component_name.clone()))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        std::{fs, path::Path},
        tempfile::TempDir,
    };

    // Creates component files for components in the exec directory.
    fn create_exec_path(root: &Path, path: &'static str) {
        let mut parent_dir = root.to_path_buf();
        let mut parent_name = root.to_str().unwrap();
        for dir_name in path.split_terminator("/") {
            let dir = parent_dir.join(dir_name);
            fs::create_dir_all(&dir).unwrap();
            if parent_name.ends_with(".cmx") {
                fs::write(dir.join("url"), "fuchsia-test://test.com/".to_owned() + parent_name)
                    .unwrap();
            }
            parent_dir = dir;
            parent_name = dir_name;
        }
    }

    // Creates component files for components that contain a children directory.
    fn create_child_path(root: &Path, path: &'static str) {
        let mut parent_dir = root.to_path_buf();
        let mut path_iter = path.split_terminator("/").peekable();
        while let Some(dir_name) = path_iter.next() {
            let dir = parent_dir.join(dir_name);
            fs::create_dir_all(&dir).unwrap();
            if let Some(&"children") = path_iter.peek() {
                fs::write(dir.join("url"), "fuchsia-test://test.com/".to_owned() + dir_name)
                    .unwrap();
                fs::write(dir.join("moniker"), dir_name).unwrap();
            }
            parent_dir = dir;
        }
    }

    /// Gets the names of each component in `component`'s tree that is
    /// includable for the given `filter`.
    fn filter_includable(component: &Component, filter: ListFilter) -> Vec<String> {
        fn filter_includable_recursive(
            component: &Component,
            filter: &ListFilter,
            names: &mut Vec<String>,
        ) {
            if component.should_include(&filter) {
                names.extend([component.name.clone()]);
            }

            for child in &component.children {
                filter_includable_recursive(&child, &filter, names);
            }
        }

        let mut names = Vec::<String>::new();
        filter_includable_recursive(&component, &filter, &mut names);
        names
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn no_children() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        // |- url
        fs::create_dir_all(&root.join("children")).unwrap();
        fs::write(root.join("moniker"), "/").unwrap();
        fs::write(root.join("url"), "fuchsia-test://test.com/tmp").unwrap();
        let root_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let component =
            Component::parse("/".to_string(), AbsoluteMoniker::root(), root_dir).await.unwrap();

        assert!(component.children.is_empty());
        assert!(!component.is_cmx);
        assert!(!component.is_running);
        assert_eq!(component.name, "/");
        assert_eq!(component.url, "fuchsia-test://test.com/tmp");
        assert!(component.ancestors.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn running() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        // |- url
        // |- exec
        fs::create_dir_all(&root.join("children")).unwrap();
        fs::create_dir_all(&root.join("exec")).unwrap();
        fs::write(root.join("moniker"), "/").unwrap();
        fs::write(root.join("url"), "fuchsia-test://test.com/tmp").unwrap();
        let root_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let component =
            Component::parse("/".to_string(), AbsoluteMoniker::root(), root_dir).await.unwrap();

        assert!(!component.is_cmx);
        assert!(component.is_running);
        assert_eq!(component.name, "/");
        assert_eq!(component.url, "fuchsia-test://test.com/tmp");
        assert!(component.ancestors.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn single_cmx_child() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // appmgr
        // |- children
        // |- url
        // |- exec
        //    |- out
        //       |- hub
        //          |- r
        //          |- c
        //             |- foo.cmx
        //                |- 123
        //                   |- url
        create_child_path(&root, "appmgr/children");
        create_exec_path(&root, "appmgr/exec/out/hub/c/foo.cmx/123");
        create_exec_path(&root, "appmgr/exec/out/hub/r");
        let root_dir = Directory::from_namespace(root.join("appmgr")).unwrap();
        let component = Component::parse(
            "appmgr".to_string(),
            AbsoluteMoniker::parse_str("/appmgr").unwrap(),
            root_dir,
        )
        .await
        .unwrap();

        assert!(!component.is_cmx);
        assert!(component.is_running);
        assert_eq!(component.name, "appmgr");
        assert_eq!(component.url, "fuchsia-test://test.com/appmgr");
        assert_eq!(component.children.len(), 1);
        assert!(component.ancestors.is_empty());

        let child = component.children.get(0).unwrap();
        assert_eq!(child.name, "foo.cmx");
        assert_eq!(child.url, "fuchsia-test://test.com/foo.cmx");
        assert!(child.is_running);
        assert!(child.is_cmx);
        assert!(child.children.is_empty());
        assert_eq!(child.ancestors, ["appmgr"]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn runner_cmx() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // appmgr
        // |- children
        // |- url
        // |- exec
        //    |- out
        //       |- hub
        //          |- r
        //          |- c
        //             |- foo.cmx
        //                |- 123
        //                   |- url
        //                   |- c
        //                      |- bar.cmx
        //                         |- 456
        //                            |- url
        create_child_path(&root, "appmgr/children");
        create_exec_path(&root, "appmgr/exec/out/hub/c/foo.cmx/123/c/bar.cmx/456");
        create_exec_path(&root, "appmgr/exec/out/hub/r");

        let root_dir = Directory::from_namespace(root.join("appmgr")).unwrap();
        let component = Component::parse(
            "appmgr".to_string(),
            AbsoluteMoniker::parse_str("/appmgr").unwrap(),
            root_dir,
        )
        .await
        .unwrap();

        assert!(!component.is_cmx);
        assert!(component.is_running);
        assert_eq!(component.name, "appmgr");
        assert_eq!(component.url, "fuchsia-test://test.com/appmgr");
        assert_eq!(component.children.len(), 1);
        assert!(component.ancestors.is_empty());

        let child = component.children.get(0).unwrap();
        assert_eq!(child.name, "foo.cmx");
        assert_eq!(child.url, "fuchsia-test://test.com/foo.cmx");
        assert!(child.is_running);
        assert!(child.is_cmx);
        assert_eq!(child.children.len(), 1);
        assert_eq!(child.ancestors, ["appmgr"]);

        let child = child.children.get(0).unwrap();
        assert_eq!(child.name, "bar.cmx");
        assert_eq!(child.url, "fuchsia-test://test.com/bar.cmx");
        assert!(child.is_running);
        assert!(child.is_cmx);
        assert_eq!(child.children.len(), 0);
        assert_eq!(child.ancestors, ["appmgr", "foo.cmx"]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn single_cml_child() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- url
        // |- children
        //    |- foo
        //       |- url
        //       |- children
        fs::write(root.join("moniker"), "/").unwrap();
        fs::write(root.join("url"), "fuchsia-test://test.com/tmp").unwrap();
        create_child_path(&root, "children/foo/children");
        let root_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let component =
            Component::parse("/".to_string(), AbsoluteMoniker::root(), root_dir).await.unwrap();

        assert!(!component.is_cmx);
        assert!(!component.is_running);
        assert_eq!(component.name, "/");
        assert_eq!(component.url, "fuchsia-test://test.com/tmp");
        assert_eq!(component.children.len(), 1);
        assert!(component.ancestors.is_empty());

        let child = component.children.get(0).unwrap();
        assert_eq!(child.name, "foo");
        assert_eq!(child.url, "fuchsia-test://test.com/foo");
        assert!(!child.is_running);
        assert!(!child.is_cmx);
        assert!(child.children.is_empty());
        assert_eq!(child.ancestors, ["/"]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn should_read_component_name_from_file() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        fs::create_dir_all(&root.join("children/")).unwrap();
        fs::write(root.join("moniker"), "my_name").unwrap();
        fs::write(root.join("url"), "fuchsia-test://test.com/tmp").unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let component = Component::parse(
            "fallback_name".to_string(),
            AbsoluteMoniker::parse_str("/my_name").unwrap(),
            root_dir,
        )
        .await
        .unwrap();
        assert_eq!(component.name, "my_name");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn should_read_dir_name_if_missing_file() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        fs::create_dir_all(&root.join("children/")).unwrap();
        fs::write(root.join("url"), "fuchsia-test://test.com/tmp").unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let component = Component::parse(
            "fallback_name".to_string(),
            AbsoluteMoniker::parse_str("/fallback_name").unwrap(),
            root_dir,
        )
        .await
        .unwrap();
        assert_eq!(component.name, "fallback_name");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn should_include_cmx() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // appmgr
        // |- children
        // |- url
        // |- exec
        //    |- out
        //       |- hub
        //          |- r
        //          |- c
        //             |- foo.cmx
        //                |- 123
        //                   |- url
        //                   |- c
        //                      |- bar.cmx
        //                         |- 456
        //                            |- url
        create_child_path(&root, "appmgr/children");
        create_exec_path(&root, "appmgr/exec/out/hub/c/foo.cmx/123/c/bar.cmx/456");
        create_exec_path(&root, "appmgr/exec/out/hub/r");

        let root_dir = Directory::from_namespace(root.join("appmgr")).unwrap();
        let component = Component::parse(
            "appmgr".to_string(),
            AbsoluteMoniker::parse_str("/appmgr").unwrap(),
            root_dir,
        )
        .await
        .unwrap();

        assert_eq!(filter_includable(&component, ListFilter::CMX), ["foo.cmx", "bar.cmx"]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn should_include_cml() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // appmgr
        // |- children
        // |- url
        // |- exec
        //    |- out
        //       |- hub
        //          |- r
        //          |- c
        //             |- foo.cmx
        //                |- 123
        //                   |- url
        //                   |- c
        //                      |- bar.cmx
        //                         |- 456
        //                            |- url
        create_child_path(&root, "appmgr/children");
        create_exec_path(&root, "appmgr/exec/out/hub/c/foo.cmx/123/c/bar.cmx/456");
        create_exec_path(&root, "appmgr/exec/out/hub/r");

        let root_dir = Directory::from_namespace(root.join("appmgr")).unwrap();
        let component = Component::parse(
            "appmgr".to_string(),
            AbsoluteMoniker::parse_str("/appmgr").unwrap(),
            root_dir,
        )
        .await
        .unwrap();

        assert_eq!(filter_includable(&component, ListFilter::CML), ["appmgr"]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn should_include_running() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // appmgr
        // |- children
        // |- url
        // |- exec
        //    |- out
        //       |- hub
        //          |- r
        //          |- c
        //             |- foo.cmx
        //                |- 123
        //                   |- url
        //                   |- c
        //                      |- bar.cmx
        //                         |- 456
        //                            |- url
        create_child_path(&root, "appmgr/children");
        create_exec_path(&root, "appmgr/exec/out/hub/c/foo.cmx/123/c/bar.cmx/456");
        create_exec_path(&root, "appmgr/exec/out/hub/r");

        let root_dir = Directory::from_namespace(root.join("appmgr")).unwrap();
        let component = Component::parse(
            "appmgr".to_string(),
            AbsoluteMoniker::parse_str("/appmgr").unwrap(),
            root_dir,
        )
        .await
        .unwrap();

        assert_eq!(
            filter_includable(&component, ListFilter::Running),
            ["appmgr", "foo.cmx", "bar.cmx"]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn should_include_stopped() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // appmgr
        // |- children
        // |- url
        // |- exec
        //    |- out
        //       |- hub
        //          |- r
        //          |- c
        //             |- foo.cmx
        //                |- 123
        //                   |- url
        //                   |- c
        //                      |- bar.cmx
        //                         |- 456
        //                            |- url
        create_child_path(&root, "appmgr/children");
        create_exec_path(&root, "appmgr/exec/out/hub/c/foo.cmx/123/c/bar.cmx/456");
        create_exec_path(&root, "appmgr/exec/out/hub/r");

        let root_dir = Directory::from_namespace(root.join("appmgr")).unwrap();
        let component = Component::parse(
            "appmgr".to_string(),
            AbsoluteMoniker::parse_str("/appmgr").unwrap(),
            root_dir,
        )
        .await
        .unwrap();

        assert_eq!(filter_includable(&component, ListFilter::Stopped), Vec::<String>::new());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn should_include_relative() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // ancestor
        // |- url
        // |- children
        //    |- branch1
        //        |- url
        //        |- children
        //    |- parent
        //        |- url
        //        |- children
        //            |- branch2
        //                |- url
        //                |- children
        //            |- foo
        //                |- url
        //                |- children
        //                    |- branch3
        //                        |- url
        //                        |- children
        //                    |- child
        //                        |- url
        //                        |- children
        //                            |- branch4
        //                                |- url
        //                                |- children
        //                            |- descendant
        //                                |- url
        //                                |- children
        create_child_path(
            &root,
            "ancestor/children/parent/children/foo/children/child/children/descendant/children",
        );
        create_child_path(&root, "ancestor/children/branch1/children");
        create_child_path(&root, "ancestor/children/parent/children/branch2/children");
        create_child_path(&root, "ancestor/children/parent/children/foo/children/branch3/children");
        create_child_path(
            &root,
            "ancestor/children/parent/children/foo/children/child/children/branch4/children",
        );
        let root_dir = Directory::from_namespace(root.join("ancestor")).unwrap();
        let component = Component::parse(
            "ancestor".to_string(),
            AbsoluteMoniker::parse_str("/ancestor").unwrap(),
            root_dir,
        )
        .await
        .unwrap();

        assert_eq!(
            filter_includable(&component, ListFilter::Relative("foo".to_string())),
            ["ancestor", "parent", "foo", "branch3", "child", "branch4", "descendant"]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn should_include_ancestor() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // ancestor
        // |- url
        // |- children
        //    |- branch1
        //        |- url
        //        |- children
        //    |- parent
        //        |- url
        //        |- children
        //            |- branch2
        //                |- url
        //                |- children
        //            |- foo
        //                |- url
        //                |- children
        //                    |- branch3
        //                        |- url
        //                        |- children
        //                    |- child
        //                        |- url
        //                        |- children
        //                            |- branch4
        //                                |- url
        //                                |- children
        //                            |- descendant
        //                                |- url
        //                                |- children
        create_child_path(
            &root,
            "ancestor/children/parent/children/foo/children/child/children/descendant/children",
        );
        create_child_path(&root, "ancestor/children/branch1/children");
        create_child_path(&root, "ancestor/children/parent/children/branch2/children");
        create_child_path(&root, "ancestor/children/parent/children/foo/children/branch3/children");
        create_child_path(
            &root,
            "ancestor/children/parent/children/foo/children/child/children/branch4/children",
        );

        let root_dir = Directory::from_namespace(root.join("ancestor")).unwrap();
        let component = Component::parse(
            "ancestor".to_string(),
            AbsoluteMoniker::parse_str("/ancestor").unwrap(),
            root_dir,
        )
        .await
        .unwrap();

        assert_eq!(
            filter_includable(&component, ListFilter::Ancestor("foo".to_string())),
            ["ancestor", "parent", "foo"]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn should_include_no_filter() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // ancestor
        // |- url
        // |- children
        //    |- branch1
        //        |- url
        //        |- children
        //    |- parent
        //        |- url
        //        |- children
        //            |- branch2
        //                |- url
        //                |- children
        //            |- foo
        //                |- url
        //                |- children
        //                    |- branch3
        //                        |- url
        //                        |- children
        //                    |- child
        //                        |- url
        //                        |- children
        //                            |- branch4
        //                                |- url
        //                                |- children
        //                            |- descendant
        //                                |- url
        //                                |- children
        create_child_path(
            &root,
            "ancestor/children/parent/children/foo/children/child/children/descendant/children",
        );
        create_child_path(&root, "ancestor/children/branch1/children");
        create_child_path(&root, "ancestor/children/parent/children/branch2/children");
        create_child_path(&root, "ancestor/children/parent/children/foo/children/branch3/children");
        create_child_path(
            &root,
            "ancestor/children/parent/children/foo/children/child/children/branch4/children",
        );
        let root_dir = Directory::from_namespace(root.join("ancestor")).unwrap();
        let component = Component::parse(
            "ancestor".to_string(),
            AbsoluteMoniker::parse_str("/ancestor").unwrap(),
            root_dir,
        )
        .await
        .unwrap();

        assert_eq!(
            filter_includable(&component, ListFilter::None),
            [
                "ancestor",
                "branch1",
                "parent",
                "branch2",
                "foo",
                "branch3",
                "child",
                "branch4",
                "descendant"
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn should_include_descendant() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // ancestor
        // |- url
        // |- children
        //    |- branch1
        //        |- url
        //        |- children
        //    |- parent
        //        |- url
        //        |- children
        //            |- branch2
        //                |- url
        //                |- children
        //            |- foo
        //                |- url
        //                |- children
        //                    |- branch3
        //                        |- url
        //                        |- children
        //                    |- child
        //                        |- url
        //                        |- children
        //                            |- branch4
        //                                |- url
        //                                |- children
        //                            |- descendant
        //                                |- url
        //                                |- children
        create_child_path(
            &root,
            "ancestor/children/parent/children/foo/children/child/children/descendant/children",
        );
        create_child_path(&root, "ancestor/children/branch1/children");
        create_child_path(&root, "ancestor/children/parent/children/branch2/children");
        create_child_path(&root, "ancestor/children/parent/children/foo/children/branch3/children");
        create_child_path(
            &root,
            "ancestor/children/parent/children/foo/children/child/children/branch4/children",
        );
        let root_dir = Directory::from_namespace(root.join("ancestor")).unwrap();
        let component = Component::parse(
            "ancestor".to_string(),
            AbsoluteMoniker::parse_str("/ancestor/parent/foo/child/descendant").unwrap(),
            root_dir,
        )
        .await
        .unwrap();

        assert_eq!(
            filter_includable(&component, ListFilter::Descendant("foo".to_string())),
            ["foo", "branch3", "child", "branch4", "descendant"]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn list_filter_from_str() {
        assert_eq!(ListFilter::from_str("cmx"), Ok(ListFilter::CMX));
        assert_eq!(ListFilter::from_str("cml"), Ok(ListFilter::CML));
        assert_eq!(ListFilter::from_str("running"), Ok(ListFilter::Running));
        assert_eq!(ListFilter::from_str("stopped"), Ok(ListFilter::Stopped));

        assert_eq!(
            ListFilter::from_str("relative:component"),
            Ok(ListFilter::Relative("component".to_string()))
        );
        assert_eq!(
            ListFilter::from_str("relative:collection:element"),
            Ok(ListFilter::Relative("collection:element".to_string()))
        );

        assert_eq!(
            ListFilter::from_str("relatives:component"),
            Ok(ListFilter::Relative("component".to_string()))
        );
        assert_eq!(
            ListFilter::from_str("relatives:collection:element"),
            Ok(ListFilter::Relative("collection:element".to_string()))
        );

        assert_eq!(
            ListFilter::from_str("descendant:component"),
            Ok(ListFilter::Descendant("component".to_string()))
        );
        assert_eq!(
            ListFilter::from_str("descendant:collection:element"),
            Ok(ListFilter::Descendant("collection:element".to_string()))
        );

        assert_eq!(
            ListFilter::from_str("descendants:component"),
            Ok(ListFilter::Descendant("component".to_string()))
        );
        assert_eq!(
            ListFilter::from_str("descendants:collection:element"),
            Ok(ListFilter::Descendant("collection:element".to_string()))
        );

        assert_eq!(
            ListFilter::from_str("ancestor:component"),
            Ok(ListFilter::Ancestor("component".to_string()))
        );
        assert_eq!(
            ListFilter::from_str("ancestor:collection:element"),
            Ok(ListFilter::Ancestor("collection:element".to_string()))
        );

        assert_eq!(
            ListFilter::from_str("ancestors:component"),
            Ok(ListFilter::Ancestor("component".to_string()))
        );
        assert_eq!(
            ListFilter::from_str("ancestors:collection:element"),
            Ok(ListFilter::Ancestor("collection:element".to_string()))
        );

        assert!(ListFilter::from_str("nonsense").is_err());
    }
}
