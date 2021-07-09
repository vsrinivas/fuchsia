// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    crate::Only,
    futures::future::{join_all, BoxFuture, FutureExt},
};

static SPACER: &str = "  ";
static WIDTH_CS_TREE: usize = 19;

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
    pub fn parse(name: String, hub_dir: Directory) -> BoxFuture<'static, Component> {
        async move {
            let is_running = hub_dir.exists("exec").await;

            // Recurse on the CML children
            let mut future_children = vec![];
            let children_dir = hub_dir
                .open_dir("children")
                .expect(&format!("Could not open `children` dir for {}", name));
            for child_name in children_dir
                .entries()
                .await
                .expect(&format!("Could not get entries of `children` dir for {}", name))
            {
                let hub_dir = children_dir
                    .open_dir(&child_name)
                    .expect(&format!("Could not open child hub {} of parent {}", child_name, name));
                let future_child = Component::parse(child_name, hub_dir);
                future_children.push(future_child);
            }

            let mut children = join_all(future_children).await;

            if name == "appmgr" {
                // Get all CMX components + realms
                let realm_dir =
                    hub_dir.open_dir("exec/out/hub").expect("Could not open appmgr hub");
                let component = Component::parse_cmx("".to_string(), realm_dir).await;
                children.extend(component.children);
            }

            Component { name, children, is_cmx: false, is_running }
        }
        .boxed()
    }

    fn parse_cmx(realm_name: String, realm_dir: Directory) -> BoxFuture<'static, Component> {
        async move {
            // Get all CMX child components
            let children_dir = realm_dir
                .open_dir("c")
                .expect(&format!("Could not open component dir for realm {}", realm_name));
            let child_component_names = children_dir.entries().await.expect(&format!(
                "Could not get entries in component dir for realm {}",
                realm_name
            ));
            let mut children = vec![];
            for child_component_name in child_component_names {
                // CMX components are leaf nodes (no children).
                // CMX components are always running if they exist in tree.
                children.push(Component {
                    name: child_component_name,
                    is_cmx: true,
                    is_running: true,
                    children: vec![],
                })
            }

            // Recurse on the CMX child realms
            let mut future_realm_lists = vec![];
            let child_realms_dir = realm_dir
                .open_dir("r")
                .expect(&format!("Could not open realm dir for realm {}", realm_name));
            for child_realm_name in child_realms_dir
                .entries()
                .await
                .expect(&format!("Could not get entries of realm dir for realm {}", realm_name))
            {
                let job_ids_dir = child_realms_dir.open_dir(&child_realm_name).expect(&format!(
                    "Could not open child realm hub {} of parent realm {}",
                    child_realm_name, realm_name
                ));
                let future_realm_list = Component::parse_cmx_job_ids(child_realm_name, job_ids_dir);
                future_realm_lists.push(future_realm_list);
            }
            let realm_lists: Vec<Vec<Component>> = join_all(future_realm_lists).await;
            let realms: Vec<Component> = realm_lists.into_iter().flatten().collect();
            children.extend(realms);

            // Consider a realm as a CMX component that has children.
            // This is not technically true, but works as a generalization.
            Component { name: realm_name, children, is_cmx: true, is_running: true }
        }
        .boxed()
    }

    fn parse_cmx_job_ids(
        realm_name: String,
        job_ids_dir: Directory,
    ) -> BoxFuture<'static, Vec<Component>> {
        async move {
            // Recurse on the job_ids
            let mut future_realms = vec![];
            for job_id in job_ids_dir
                .entries()
                .await
                .expect(&format!("Could not get entries of job id dir for realm {}", realm_name))
            {
                let realm_dir = job_ids_dir.open_dir(&job_id).expect(&format!(
                    "Could not open job id dir {} for realm {}",
                    job_id, realm_name
                ));
                let future_realm = Component::parse_cmx(realm_name.clone(), realm_dir);
                future_realms.push(future_realm)
            }

            join_all(future_realms).await
        }
        .boxed()
    }

    fn should_print(&self, only: &Only) -> bool {
        if only == &Only::CML && self.is_cmx {
            return false;
        } else if only == &Only::CMX && !self.is_cmx {
            return false;
        }

        if only == &Only::Running && !self.is_running {
            return false;
        } else if only == &Only::Stopped && self.is_running {
            return false;
        }

        return true;
    }

    pub fn print(&self, only: &Only, verbose: bool, indent: usize) {
        let space = SPACER.repeat(indent);

        if self.should_print(&only) {
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
            child.print(only, verbose, indent + 1);
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
        let component = Component::parse(".".to_string(), root_dir).await;

        assert!(component.children.is_empty());
        assert!(!component.is_cmx);
        assert!(!component.is_running);
        assert_eq!(component.name, ".");
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
        let component = Component::parse(".".to_string(), root_dir).await;

        assert!(!component.is_cmx);
        assert!(component.is_running);
        assert_eq!(component.name, ".");
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
        //          |- c
        //             |- foo.cmx
        fs::create_dir(root.join("children")).unwrap();
        fs::create_dir_all(root.join("exec/out/hub/c/foo.cmx")).unwrap();
        fs::create_dir_all(root.join("exec/out/hub/r")).unwrap();
        let root_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let component = Component::parse("appmgr".to_string(), root_dir).await;

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
        let component = Component::parse(".".to_string(), root_dir).await;

        assert!(!component.is_cmx);
        assert!(!component.is_running);
        assert_eq!(component.name, ".");
        assert_eq!(component.children.len(), 1);

        let child = component.children.get(0).unwrap();
        assert_eq!(child.name, "foo");
        assert!(!child.is_running);
        assert!(!child.is_cmx);
        assert!(child.children.is_empty());
    }
}
