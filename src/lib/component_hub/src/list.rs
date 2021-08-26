// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    anyhow::Result,
    futures::future::{join, join_all, BoxFuture, FutureExt},
    std::str::FromStr,
};

static SPACER: &str = "  ";
static WIDTH_CS_TREE: usize = 19;

/// Filters that can be applied when listing components
#[derive(Debug, PartialEq)]
pub enum ListFilter {
    CMX,
    CML,
    Running,
    Stopped,
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
            _ => Err("List filter should be 'cmx', 'cml', 'running', or 'stopped'"),
        }
    }
}

/// Basic information about a component for the `list` command.
pub struct Component {
    // Name of the component. This gets printed out.
    pub name: String,

    // True if component is of appmgr/CMX type.
    // False if it is of the component_manager/CML type.
    pub is_cmx: bool,

    // CML components may not always be running.
    // Always true for CMX components.
    pub is_running: bool,

    // Children of this component.
    pub children: Vec<Component>,
}

impl Component {
    pub fn parse(name: String, hub_dir: Directory) -> BoxFuture<'static, Result<Component>> {
        async move {
            let is_running = hub_dir.exists("exec").await?;

            // Recurse on the CML children
            let mut future_children = vec![];
            let children_dir = hub_dir.open_dir("children")?;
            for child_name in children_dir.entries().await? {
                let hub_dir = children_dir.open_dir(&child_name)?;
                let future_child = Component::parse(child_name, hub_dir);
                future_children.push(future_child);
            }

            let results = join_all(future_children).await;
            let mut children = vec![];
            for result in results {
                let component = result?;
                children.push(component);
            }

            if name == "appmgr" {
                // Get all CMX components + realms
                let realm_dir = hub_dir.open_dir("exec/out/hub")?;
                let component = Component::parse_cmx_realm("".to_string(), realm_dir).await?;
                children.extend(component.children);
            }

            Ok(Component { name, children, is_cmx: false, is_running })
        }
        .boxed()
    }

    fn parse_cmx_component(name: String, dir: Directory) -> BoxFuture<'static, Result<Component>> {
        async move {
            // Runner CMX components may have child components
            let children = if dir.exists("c").await? {
                let children_dir = dir.open_dir("c")?;
                Component::parse_cmx_components_in_c_dir(children_dir).await?
            } else {
                vec![]
            };

            // CMX components are always running if they exist in tree.
            Ok(Component { name, children, is_cmx: true, is_running: true })
        }
        .boxed()
    }

    fn parse_cmx_realm(
        realm_name: String,
        realm_dir: Directory,
    ) -> BoxFuture<'static, Result<Component>> {
        async move {
            let children_dir = realm_dir.open_dir("c")?;
            let realms_dir = realm_dir.open_dir("r")?;

            let future_children = Component::parse_cmx_components_in_c_dir(children_dir);
            let future_realms = Component::parse_cmx_realms_in_r_dir(realms_dir);

            let (children, realms) = join(future_children, future_realms).await;
            let mut children = children?;
            let mut realms = realms?;

            children.append(&mut realms);

            // Consider a realm as a CMX component that has children.
            // This is not technically true, but works as a generalization.
            Ok(Component { name: realm_name, children, is_cmx: true, is_running: true })
        }
        .boxed()
    }

    async fn parse_cmx_components_in_c_dir(children_dir: Directory) -> Result<Vec<Component>> {
        // Get all CMX child components
        let child_component_names = children_dir.entries().await?;
        let mut future_children = vec![];
        for child_component_name in child_component_names {
            let job_ids_dir = children_dir.open_dir(&child_component_name)?;
            let child_dirs = Component::open_all_job_ids(job_ids_dir).await?;
            for child_dir in child_dirs {
                let future_child =
                    Component::parse_cmx_component(child_component_name.clone(), child_dir);
                future_children.push(future_child);
            }
        }

        let results = join_all(future_children).await;
        results.into_iter().collect()
    }

    async fn parse_cmx_realms_in_r_dir(realms_dir: Directory) -> Result<Vec<Component>> {
        // Get all CMX child realms
        let mut future_realms = vec![];
        for child_realm_name in realms_dir.entries().await? {
            let job_ids_dir = realms_dir.open_dir(&child_realm_name)?;
            let child_realm_dirs = Component::open_all_job_ids(job_ids_dir).await?;
            for child_realm_dir in child_realm_dirs {
                let future_realm =
                    Component::parse_cmx_realm(child_realm_name.clone(), child_realm_dir);
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
            let dir = job_ids_dir.open_dir(&job_id)?;
            dirs.push(dir);
        }
        Ok(dirs)
    }

    fn should_print(&self, list_filter: &ListFilter) -> bool {
        if list_filter == &ListFilter::CML && self.is_cmx {
            return false;
        } else if list_filter == &ListFilter::CMX && !self.is_cmx {
            return false;
        }

        if list_filter == &ListFilter::Running && !self.is_running {
            return false;
        } else if list_filter == &ListFilter::Stopped && self.is_running {
            return false;
        }

        return true;
    }

    pub fn print(&self, list_filter: &ListFilter, verbose: bool, indent: usize) {
        let space = SPACER.repeat(indent);

        if self.should_print(&list_filter) {
            if verbose {
                let component_type = if self.is_cmx { "CMX" } else { "CML" };

                let state = if self.is_running { "Running" } else { "Stopped" };

                println!(
                    "{:<width_type$}{:<width_state$}{}{}",
                    component_type,
                    state,
                    space,
                    self.name,
                    width_type = WIDTH_CS_TREE,
                    width_state = WIDTH_CS_TREE
                );
            } else {
                println!("{}{}", space, self.name);
            }
        }

        for child in &self.children {
            child.print(list_filter, verbose, indent + 1);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {std::fs, tempfile::TempDir};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn no_children() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        fs::create_dir(root.join("children")).unwrap();
        let root_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let component = Component::parse("/".to_string(), root_dir).await.unwrap();

        assert!(component.children.is_empty());
        assert!(!component.is_cmx);
        assert!(!component.is_running);
        assert_eq!(component.name, "/");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn running() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        // |- exec
        fs::create_dir(root.join("children")).unwrap();
        fs::create_dir(root.join("exec")).unwrap();
        let root_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let component = Component::parse("/".to_string(), root_dir).await.unwrap();

        assert!(!component.is_cmx);
        assert!(component.is_running);
        assert_eq!(component.name, "/");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn single_cmx_child() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // appmgr
        // |- children
        // |- exec
        //    |- out
        //       |- hub
        //          |- r
        //          |- c
        //             |- foo.cmx
        //                |- 123
        fs::create_dir(root.join("children")).unwrap();
        fs::create_dir_all(root.join("exec/out/hub/c/foo.cmx/123")).unwrap();
        fs::create_dir_all(root.join("exec/out/hub/r")).unwrap();
        let root_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let component = Component::parse("appmgr".to_string(), root_dir).await.unwrap();

        assert!(!component.is_cmx);
        assert!(component.is_running);
        assert_eq!(component.name, "appmgr");
        assert_eq!(component.children.len(), 1);

        let child = component.children.get(0).unwrap();
        assert_eq!(child.name, "foo.cmx");
        assert!(child.is_running);
        assert!(child.is_cmx);
        assert!(child.children.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn runner_cmx() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // appmgr
        // |- children
        // |- exec
        //    |- out
        //       |- hub
        //          |- r
        //          |- c
        //             |- foo.cmx
        //                |- 123
        //                   |- c
        //                      |- bar.cmx
        //                         |- 456
        fs::create_dir(root.join("children")).unwrap();
        fs::create_dir_all(root.join("exec/out/hub/c/foo.cmx/123/c/bar.cmx/456")).unwrap();
        fs::create_dir_all(root.join("exec/out/hub/r")).unwrap();
        let root_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let component = Component::parse("appmgr".to_string(), root_dir).await.unwrap();

        assert!(!component.is_cmx);
        assert!(component.is_running);
        assert_eq!(component.name, "appmgr");
        assert_eq!(component.children.len(), 1);

        let child = component.children.get(0).unwrap();
        assert_eq!(child.name, "foo.cmx");
        assert!(child.is_running);
        assert!(child.is_cmx);
        assert_eq!(child.children.len(), 1);

        let child = child.children.get(0).unwrap();
        assert_eq!(child.name, "bar.cmx");
        assert!(child.is_running);
        assert!(child.is_cmx);
        assert_eq!(child.children.len(), 0);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn single_cml_child() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        //    |- test
        //       |- children
        fs::create_dir_all(root.join("children/foo/children")).unwrap();
        let root_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let component = Component::parse("/".to_string(), root_dir).await.unwrap();

        assert!(!component.is_cmx);
        assert!(!component.is_running);
        assert_eq!(component.name, "/");
        assert_eq!(component.children.len(), 1);

        let child = component.children.get(0).unwrap();
        assert_eq!(child.name, "foo");
        assert!(!child.is_running);
        assert!(!child.is_cmx);
        assert!(child.children.is_empty());
    }
}
