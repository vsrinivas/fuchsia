// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    crate::v1::V1Realm,
    crate::{get_capabilities, get_capabilities_timeout, IncludeDetails},
    anyhow::{format_err, Error},
    futures::future::{join_all, BoxFuture, FutureExt},
};

static SPACER: &str = "  ";

fn explore(
    name: String,
    hub_dir: Directory,
    include_details: IncludeDetails,
) -> BoxFuture<'static, V2Component> {
    async move {
        // Recurse on the children
        let mut future_children = vec![];
        let children_dir = hub_dir.open_dir("children").expect("open_dir(`children`) failed!");
        for child_name in children_dir.entries().await {
            let child_dir = children_dir
                .open_dir(&child_name)
                .expect(format!("open_dir(`{}`) failed!", child_name).as_str());
            let future_child = explore(child_name, child_dir, include_details.clone());
            future_children.push(future_child);
        }
        let children = join_all(future_children).await;

        // If this component is appmgr, use it to explore the v1 component world
        let appmgr_root_v1_realm = if name == "appmgr" {
            let v1_hub_dir = hub_dir
                .open_dir("exec/out/hub")
                .expect("open_dir() failed: failed to open appmgr hub (`exec/out/hub`)!");
            Some(V1Realm::create(v1_hub_dir, include_details.clone()).await)
        } else {
            None
        };

        // Get details if include_details is true
        let details = if include_details == IncludeDetails::Yes {
            let (url, id, component_type) = futures::join!(
                hub_dir.read_file("url"),
                hub_dir.read_file("id"),
                hub_dir.read_file("component_type"),
            );

            let url = url.expect("read_file(`url`) failed!");

            let id =
                id.expect("read_file(`id`) failed!").parse::<u32>().expect("parse(`id`) failed!");

            let component_type = component_type.expect("read_file(`component_type`) failed!");

            // Get the execution state
            let execution = if hub_dir.exists("exec").await {
                let exec_dir = hub_dir.open_dir("exec").expect("open_dir(`exec`) failed!");
                Some(Execution::new(exec_dir).await)
            } else {
                None
            };

            Some(Details { url, id, component_type, execution })
        } else {
            None
        };

        V2Component { name, children, appmgr_root_v1_realm, details }
    }
    .boxed()
}

#[derive(Debug, Eq, PartialEq)]
pub struct ElfRuntime {
    pub job_id: u32,
    pub process_id: u32,
}

#[derive(Debug, Eq, PartialEq)]
pub struct Execution {
    pub elf_runtime: Option<ElfRuntime>,
    pub merkle_root: Option<String>,
    pub incoming_capabilities: Vec<String>,
    pub outgoing_capabilities: Option<Vec<String>>,
    pub exposed_capabilities: Vec<String>,
}

impl Execution {
    async fn new(exec_dir: Directory) -> Self {
        // Get the ELF runtime
        let elf_runtime = if exec_dir.exists("runtime").await {
            let runtime_dir = exec_dir.open_dir("runtime").expect("open_dir(`runtime`) failed!");
            if runtime_dir.exists("elf").await {
                let elf_runtime_dir = runtime_dir.open_dir("elf").expect("open_dir(`elf`) failed!");

                let (job_id, process_id) = futures::join!(
                    elf_runtime_dir.read_file("job_id"),
                    elf_runtime_dir.read_file("process_id"),
                );

                let job_id = job_id
                    .expect("read_file(`job_id`) failed!")
                    .parse::<u32>()
                    .expect("parse(`job_id`) failed!");

                let process_id = process_id
                    .expect("read_file(`process_id`) failed!")
                    .parse::<u32>()
                    .expect("parse(`process_id`) failed!");

                Some(ElfRuntime { job_id, process_id })
            } else {
                None
            }
        } else {
            None
        };

        let in_dir = exec_dir.open_dir("in").expect("open_dir(`in`) failed!");

        let merkle_root = if in_dir.exists("pkg").await {
            let pkg_dir = in_dir.open_dir("pkg").expect("open_dir(`pkg`) failed!");
            if pkg_dir.exists("meta").await {
                match pkg_dir.read_file("meta").await {
                    Ok(file) => Some(file),
                    Err(_) => None,
                }
            } else {
                None
            }
        } else {
            None
        };

        let expose_dir = exec_dir.open_dir("expose").expect("open_dir(`expose`) failed!");

        let (exposed_capabilities, incoming_capabilities) =
            futures::join!(expose_dir.entries(), get_capabilities(in_dir),);

        let outgoing_capabilities = if exec_dir.exists("out").await {
            let out_dir = exec_dir.open_dir("out").expect("open_dir(`out`) failed!");
            get_capabilities_timeout(out_dir).await
        } else {
            // The directory doesn't exist. This is probably because
            // there is no runtime on the component.
            None
        };

        Execution {
            elf_runtime,
            merkle_root,
            incoming_capabilities,
            outgoing_capabilities,
            exposed_capabilities,
        }
    }

    fn print_details(&self) {
        if let Some(runtime) = &self.elf_runtime {
            println!("Job ID: {}", runtime.job_id);
            println!("Process ID: {}", runtime.process_id);
        }

        if let Some(merkle_root) = &self.merkle_root {
            println!("Merkle root: {}", merkle_root);
        }

        println!("Incoming Capabilities ({}):", self.incoming_capabilities.len());
        for capability in &self.incoming_capabilities {
            println!("{}{}", SPACER, capability);
        }

        if let Some(outgoing_capabilities) = &self.outgoing_capabilities {
            println!("Outgoing Capabilities ({}):", outgoing_capabilities.len());
            for capability in outgoing_capabilities {
                println!("{}{}", SPACER, capability);
            }
        }

        println!("Exposed Capabilities ({}):", self.exposed_capabilities.len());
        for capability in &self.exposed_capabilities {
            println!("{}{}", SPACER, capability);
        }
    }
}

#[derive(Debug, Eq, PartialEq)]
pub struct V2Component {
    pub name: String,
    pub children: Vec<Self>,
    pub appmgr_root_v1_realm: Option<V1Realm>,
    pub details: Option<Details>,
}

#[derive(Debug, Eq, PartialEq)]
pub struct Details {
    pub url: String,
    pub id: u32,
    pub component_type: String,
    pub execution: Option<Execution>,
}

impl V2Component {
    pub async fn explore(hub_dir: Directory, include_details: IncludeDetails) -> Self {
        explore("<root>".to_string(), hub_dir, include_details.clone()).await
    }

    pub fn print_tree(&self) {
        self.print_tree_recursive(1);
    }

    fn print_tree_recursive(&self, level: usize) {
        let space = SPACER.repeat(level - 1);
        println!("{}{}", space, self.name);
        for child in &self.children {
            child.print_tree_recursive(level + 1);
        }

        // If this component is appmgr, generate tree for all v1 components
        if let Some(v1_realm) = &self.appmgr_root_v1_realm {
            v1_realm.print_tree_recursive(level + 1);
        }
    }

    pub fn print_details(&self, filter: &str) -> Result<(), Error> {
        if !self.print_details_recursive("", filter) {
            return Err(format_err!(
                "filter should be a component name or component (partial) url."
            ));
        };
        Ok(())
    }

    fn print_details_recursive(&self, moniker_prefix: &str, filter: &str) -> bool {
        let mut did_print = false;
        if let Some(details) = &self.details {
            let moniker = format!("{}{}:{}", moniker_prefix, self.name, details.id);

            // Print if the filter matches
            if filter.is_empty() || details.url.contains(filter) || self.name.contains(filter) {
                println!("Moniker: {}", moniker);
                println!("URL: {}", details.url);
                println!("Type: v2 {} component", details.component_type);

                if let Some(execution) = &details.execution {
                    execution.print_details();
                }

                println!("");
                did_print = true;
            }

            // Recurse on children
            let moniker_prefix = format!("{}/", moniker);
            for child in &self.children {
                did_print |= child.print_details_recursive(&moniker_prefix, filter);
            }

            // If this component is appmgr, generate details for all v1 components
            if let Some(v1_realm) = &self.appmgr_root_v1_realm {
                did_print |= v1_realm.print_details_recursive(&moniker_prefix, filter);
            }
        } else {
            eprintln!("Error! `V2Component` doesn't have `Details`!");
        }

        did_print
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        std::fs::{self, File},
        std::io::Write,
        tempfile::TempDir,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn execution_loads_empty_directories_with_out_and_runtime() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- exec
        //    |- expose
        //    |- in
        //    |- out
        //    |- runtime
        let exec = root.join("exec");
        fs::create_dir(&exec).unwrap();
        fs::create_dir(exec.join("expose")).unwrap();
        fs::create_dir(exec.join("in")).unwrap();
        fs::create_dir(exec.join("out")).unwrap();
        fs::create_dir(exec.join("runtime")).unwrap();

        let exec_dir = Directory::from_namespace(exec.to_path_buf())
            .expect("from_namespace() failed: failed to open `exec` directory!");
        let execution = Execution::new(exec_dir).await;

        assert!(execution.elf_runtime.is_none());
        assert!(execution.merkle_root.is_none());
        assert_eq!(execution.incoming_capabilities, Vec::<String>::new());
        assert_eq!(execution.outgoing_capabilities.unwrap(), Vec::<String>::new());
        assert_eq!(execution.exposed_capabilities, Vec::<String>::new());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn execution_loads_empty_directories_without_out_and_runtime() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- exec
        //    |- expose
        //    |- in
        let exec = root.join("exec");
        fs::create_dir(&exec).unwrap();
        fs::create_dir(exec.join("expose")).unwrap();
        fs::create_dir(exec.join("in")).unwrap();

        let exec_dir = Directory::from_namespace(exec.to_path_buf())
            .expect("from_namespace() failed: failed to open `exec` directory!");
        let execution = Execution::new(exec_dir).await;

        assert!(execution.elf_runtime.is_none());
        assert!(execution.merkle_root.is_none());
        assert_eq!(execution.incoming_capabilities, Vec::<String>::new());
        assert!(execution.outgoing_capabilities.is_none());
        assert_eq!(execution.exposed_capabilities, Vec::<String>::new());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn execution_loads_elf_runtime() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- exec
        //    |- expose
        //    |- in
        //    |- runtime
        //       |- elf
        //          |- job-id
        //          |- process-id
        let exec = root.join("exec");
        fs::create_dir(&exec).unwrap();
        fs::create_dir(exec.join("expose")).unwrap();
        fs::create_dir(exec.join("in")).unwrap();
        fs::create_dir(exec.join("runtime")).unwrap();
        fs::create_dir(exec.join("runtime/elf")).unwrap();
        File::create(exec.join("runtime/elf/job_id"))
            .unwrap()
            .write_all("12345".as_bytes())
            .unwrap();
        File::create(exec.join("runtime/elf/process_id"))
            .unwrap()
            .write_all("67890".as_bytes())
            .unwrap();

        let exec_dir = Directory::from_namespace(exec.to_path_buf())
            .expect("from_namespace() failed: failed to open `exec` directory!");
        let execution = Execution::new(exec_dir).await;

        assert_eq!(execution.elf_runtime.unwrap(), ElfRuntime { job_id: 12345, process_id: 67890 });
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn execution_loads_merkle_root() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- exec
        //    |- expose
        //    |- in
        //       |- pkg
        //          |- meta
        let exec = root.join("exec");
        fs::create_dir(&exec).unwrap();
        fs::create_dir(exec.join("expose")).unwrap();
        fs::create_dir(exec.join("in")).unwrap();
        fs::create_dir(exec.join("in/pkg")).unwrap();
        File::create(exec.join("in/pkg/meta"))
            .unwrap()
            .write_all(
                "284714fdf0a8125949946c2609be45d67899cbf104d7b9a020b51b8da540ec93".as_bytes(),
            )
            .unwrap();

        let exec_dir = Directory::from_namespace(exec.to_path_buf())
            .expect("from_namespace() failed: failed to open `exec` directory!");
        let execution = Execution::new(exec_dir).await;

        assert_eq!(
            execution.merkle_root.unwrap(),
            "284714fdf0a8125949946c2609be45d67899cbf104d7b9a020b51b8da540ec93"
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn execution_does_not_load_merkle_root_without_pkg_directory() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- exec
        //    |- expose
        //    |- in
        let exec = root.join("exec");
        fs::create_dir(&exec).unwrap();
        fs::create_dir(exec.join("expose")).unwrap();
        fs::create_dir(exec.join("in")).unwrap();

        let exec_dir = Directory::from_namespace(exec.to_path_buf())
            .expect("from_namespace() failed: failed to open `exec` directory!");
        let execution = Execution::new(exec_dir).await;

        assert_eq!(execution.merkle_root, None);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn execution_does_not_load_merkle_root_if_meta_file_cannot_be_opened() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- exec
        //    |- expose
        //    |- in
        //       |- pkg
        //          |- meta
        let exec = root.join("exec");
        fs::create_dir(&exec).unwrap();
        fs::create_dir(exec.join("expose")).unwrap();
        fs::create_dir(exec.join("in")).unwrap();
        fs::create_dir(exec.join("in/pkg")).unwrap();
        fs::create_dir(exec.join("in/pkg/meta")).unwrap();

        let exec_dir = Directory::from_namespace(exec.to_path_buf())
            .expect("from_namespace() failed: failed to open `exec` directory!");
        let execution = Execution::new(exec_dir).await;

        assert_eq!(execution.merkle_root, None);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn execution_loads_incoming_capabilities() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- exec
        //    |- expose
        //    |- in
        //       |- pkg
        let exec = root.join("exec");
        fs::create_dir(&exec).unwrap();
        fs::create_dir(exec.join("expose")).unwrap();
        fs::create_dir(exec.join("in")).unwrap();
        fs::create_dir(exec.join("in/pkg")).unwrap();

        let exec_dir = Directory::from_namespace(exec.to_path_buf())
            .expect("from_namespace() failed: failed to open `exec` directory!");
        let execution = Execution::new(exec_dir).await;

        assert_eq!(execution.incoming_capabilities, vec!["pkg".to_string()]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn execution_loads_outgoing_capabilities() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- exec
        //    |- expose
        //    |- in
        //    |- out
        //       |- fidl.examples.routing.echo.Echo
        let exec = root.join("exec");
        fs::create_dir(&exec).unwrap();
        fs::create_dir(exec.join("expose")).unwrap();
        fs::create_dir(exec.join("in")).unwrap();
        fs::create_dir(exec.join("out")).unwrap();
        fs::create_dir(exec.join("out/fidl.examples.routing.echo.Echo")).unwrap();

        let exec_dir = Directory::from_namespace(exec.to_path_buf())
            .expect("from_namespace() failed: failed to open `exec` directory!");
        let execution = Execution::new(exec_dir).await;

        assert_eq!(
            execution.outgoing_capabilities.unwrap(),
            vec!["fidl.examples.routing.echo.Echo".to_string()]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn execution_loads_exposed_capabilities() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- exec
        //    |- expose
        //       |- pkgfs
        //    |- in
        //    |- out
        let exec = root.join("exec");
        fs::create_dir(&exec).unwrap();
        fs::create_dir(exec.join("expose")).unwrap();
        fs::create_dir(exec.join("expose/pkgfs")).unwrap();
        fs::create_dir(exec.join("in")).unwrap();
        fs::create_dir(exec.join("out")).unwrap();

        let exec_dir = Directory::from_namespace(exec.to_path_buf())
            .expect("from_namespace() failed: failed to open `exec` directory!");
        let execution = Execution::new(exec_dir).await;

        assert_eq!(execution.exposed_capabilities, vec!["pkgfs".to_string()]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn explore_loads_name() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- children
        fs::create_dir(root.join("children")).unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, IncludeDetails::No).await;

        assert!(v2_component.appmgr_root_v1_realm.is_none());
        assert_eq!(v2_component.children, vec![]);
        assert!(v2_component.details.is_none());
        assert_eq!(v2_component.name, "<root>");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn explore_loads_children() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- children
        //    |- bootstrap
        //       |- children
        //       |- component_type
        //       |- id
        //       |- url
        fs::create_dir(root.join("children")).unwrap();
        let bootstrap = root.join("children/bootstrap");
        fs::create_dir(&bootstrap).unwrap();
        fs::create_dir(bootstrap.join("children")).unwrap();
        File::create(bootstrap.join("component_type"))
            .unwrap()
            .write_all("static".as_bytes())
            .unwrap();
        File::create(bootstrap.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(bootstrap.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/bootstrap.cm".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, IncludeDetails::No).await;

        assert_eq!(
            v2_component.children,
            vec![V2Component {
                name: "bootstrap".to_string(),
                children: vec![],
                appmgr_root_v1_realm: None,
                details: None,
            }],
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn explore_appmgr_explores_v1_realm() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- children
        //       |- appmgr
        //          |- children
        //          |- component_type
        //          |- exec
        //             |- expose
        //             |- in
        //             |- out
        //                |- hub
        //                   |- c
        //                   |- job-id
        //                   |- name
        //                   |- r
        //          |- id
        //          |- url
        // |- component_type
        // |- id
        // |- url
        fs::create_dir(root.join("children")).unwrap();
        let appmgr = root.join("children/appmgr");
        fs::create_dir(root.join(&appmgr)).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        fs::create_dir(appmgr.join("children")).unwrap();
        File::create(appmgr.join("component_type"))
            .unwrap()
            .write_all("static".as_bytes())
            .unwrap();

        let exec = appmgr.join("exec");
        fs::create_dir(&exec).unwrap();
        fs::create_dir(exec.join("expose")).unwrap();
        fs::create_dir(exec.join("in")).unwrap();
        fs::create_dir(exec.join("out")).unwrap();

        let hub = exec.join("out/hub");
        fs::create_dir(&hub).unwrap();
        fs::create_dir(hub.join("c")).unwrap();
        File::create(hub.join("job-id")).unwrap().write_all("12345".as_bytes()).unwrap();
        File::create(hub.join("name")).unwrap().write_all("sysmgr.cmx".as_bytes()).unwrap();
        fs::create_dir(hub.join("r")).unwrap();

        File::create(appmgr.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(appmgr.join("url"))
            .unwrap()
            .write_all("fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, IncludeDetails::No).await;

        let v1_hub_dir = Directory::from_namespace(appmgr.join("exec/out/hub"))
            .expect("from_namespace() failed: failed to create directory!");
        assert!(v2_component.appmgr_root_v1_realm.is_none());

        assert_eq!(
            v2_component.children,
            vec![V2Component {
                name: "appmgr".to_string(),
                children: vec![],
                appmgr_root_v1_realm: Some(V1Realm::create(v1_hub_dir, IncludeDetails::No).await),
                details: None,
            }],
        );

        assert!(v2_component.details.is_none());
        assert_eq!(v2_component.name, "<root>");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn explore_with_details_loads_component_type_and_id_and_name_and_url() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- children
        // |- component_type
        // |- id
        // |- url
        fs::create_dir(root.join("children")).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, IncludeDetails::Yes).await;

        assert_eq!(v2_component.name, "<root>");
        assert_eq!(v2_component.children, vec![]);
        assert!(v2_component.appmgr_root_v1_realm.is_none());
        assert_eq!(
            v2_component.details,
            Some(Details {
                url: "fuchsia-boot:///#meta/root.cm".to_string(),
                id: 0,
                component_type: "static".to_string(),
                execution: None,
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn explore_with_details_appmgr_explores_v1_realm() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- children
        //       |- appmgr
        //          |- children
        //          |- component_type
        //          |- exec
        //             |- expose
        //             |- in
        //             |- out
        //                |- hub
        //                   |- c
        //                   |- job-id
        //                   |- name
        //                   |- r
        //          |- id
        //          |- url
        // |- component_type
        // |- id
        // |- url
        fs::create_dir(root.join("children")).unwrap();
        let appmgr = root.join("children/appmgr");
        fs::create_dir(&appmgr).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        fs::create_dir(appmgr.join("children")).unwrap();
        File::create(appmgr.join("component_type"))
            .unwrap()
            .write_all("static".as_bytes())
            .unwrap();

        let exec = appmgr.join("exec");
        fs::create_dir(&exec).unwrap();
        fs::create_dir(exec.join("expose")).unwrap();
        fs::create_dir(exec.join("in")).unwrap();
        fs::create_dir(exec.join("out")).unwrap();

        let hub = exec.join("out/hub");
        fs::create_dir(&hub).unwrap();
        fs::create_dir(hub.join("c")).unwrap();
        File::create(hub.join("job-id")).unwrap().write_all("12345".as_bytes()).unwrap();
        File::create(hub.join("name")).unwrap().write_all("sysmgr.cmx".as_bytes()).unwrap();
        fs::create_dir(hub.join("r")).unwrap();

        File::create(appmgr.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(appmgr.join("url"))
            .unwrap()
            .write_all("fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, IncludeDetails::Yes).await;

        let v1_hub_dir = Directory::from_namespace(appmgr.join("exec/out/hub"))
            .expect("from_namespace() failed: failed to open appmgr v1 hub directory!");
        assert!(v2_component.appmgr_root_v1_realm.is_none());

        assert_eq!(
            v2_component.children,
            vec![V2Component {
                name: "appmgr".to_string(),
                children: vec![],
                appmgr_root_v1_realm: Some(V1Realm::create(v1_hub_dir, IncludeDetails::Yes).await),
                details: Some(Details {
                    url: "fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm".to_string(),
                    id: 0,
                    component_type: "static".to_string(),
                    execution: Some(Execution {
                        elf_runtime: None,
                        merkle_root: None,
                        incoming_capabilities: vec![],
                        outgoing_capabilities: Some(vec!["hub".to_string()]),
                        exposed_capabilities: vec![],
                    }),
                })
            }],
        );

        assert_eq!(v2_component.name, "<root>");
        assert_eq!(
            v2_component.details,
            Some(Details {
                url: "fuchsia-boot:///#meta/root.cm".to_string(),
                id: 0,
                component_type: "static".to_string(),
                execution: None,
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn explore_with_details_loads_children() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- children
        //    |- bootstrap
        //       |- children
        //       |- component_type
        //       |- id
        //       |- url
        // |- component_type
        // |- id
        // |- url
        fs::create_dir(root.join("children")).unwrap();
        let bootstrap = root.join("children/bootstrap");
        fs::create_dir(&bootstrap).unwrap();
        fs::create_dir(bootstrap.join("children")).unwrap();
        File::create(bootstrap.join("component_type"))
            .unwrap()
            .write_all("static".as_bytes())
            .unwrap();
        File::create(bootstrap.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(bootstrap.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/bootstrap.cm".as_bytes())
            .unwrap();

        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, IncludeDetails::Yes).await;

        assert_eq!(
            v2_component.children,
            vec![V2Component {
                name: "bootstrap".to_string(),
                children: vec![],
                appmgr_root_v1_realm: None,
                details: Some(Details {
                    url: "fuchsia-boot:///#meta/bootstrap.cm".to_string(),
                    id: 0,
                    component_type: "static".to_string(),
                    execution: None,
                })
            }],
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn explore_with_details_loads_execution() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- children
        // |- component_type
        // |- exec
        //    |- expose
        //    |- in
        //    |- runtime
        //       |- elf
        //          |- job-id
        //          |- process-id
        // |- id
        // |- url
        fs::create_dir(root.join("children")).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        let exec = root.join("exec");
        fs::create_dir(&exec).unwrap();
        fs::create_dir(exec.join("expose")).unwrap();
        fs::create_dir(exec.join("in")).unwrap();
        fs::create_dir(exec.join("runtime")).unwrap();
        fs::create_dir(exec.join("runtime/elf")).unwrap();
        File::create(exec.join("runtime/elf/job_id"))
            .unwrap()
            .write_all("12345".as_bytes())
            .unwrap();
        File::create(exec.join("runtime/elf/process_id"))
            .unwrap()
            .write_all("67890".as_bytes())
            .unwrap();
        File::create(root.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, IncludeDetails::Yes).await;

        assert_eq!(
            v2_component.details.unwrap().execution.unwrap().elf_runtime.unwrap(),
            ElfRuntime { job_id: 12345, process_id: 67890 }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn print_details_recursive_finds_v2_filter() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- children
        //    |- bootstrap
        //       |- children
        //          |- archivist
        //              |- children
        //              |- component_type
        //              |- id
        //              |- url
        //       |- component_type
        //       |- id
        //       |- url
        // |- component_type
        // |- id
        // |- url
        fs::create_dir(root.join("children")).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        let bootstrap = root.join("children/bootstrap");
        fs::create_dir(&bootstrap).unwrap();
        fs::create_dir(bootstrap.join("children")).unwrap();
        File::create(bootstrap.join("component_type"))
            .unwrap()
            .write_all("static".as_bytes())
            .unwrap();
        File::create(bootstrap.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(bootstrap.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/bootstrap.cm".as_bytes())
            .unwrap();

        let archivist = bootstrap.join("children/archivist");
        fs::create_dir(&archivist).unwrap();
        fs::create_dir(archivist.join("children")).unwrap();
        File::create(archivist.join("component_type"))
            .unwrap()
            .write_all("static".as_bytes())
            .unwrap();
        File::create(archivist.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(archivist.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/archivist.cm".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, IncludeDetails::Yes).await;

        assert!(v2_component.print_details("bootstrap").is_ok());
        assert!(v2_component.print_details("archivist").is_ok());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn print_details_recursive_finds_v1_filter() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- children
        //    |- core
        //       |- children
        //          |- appmgr
        //             |- exec
        //                |- expose
        //                |- in
        //                |- out
        //                   |- hub
        //                      |- c
        //                         |- sysmgr.cmx
        //                            |- 13579
        //                               |- c
        //                               |- job-id
        //                               |- name
        //                               |- r
        //                               |- in
        //                               |- url
        //                      |- job-id
        //                      |- name
        //                      |- r
        //             |- children
        //             |- component_type
        //             |- id
        //             |- url
        //       |- component_type
        //       |- id
        //       |- url
        // |- component_type
        // |- id
        // |- url
        fs::create_dir(root.join("children")).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        let core = root.join("children/core");
        fs::create_dir(&core).unwrap();
        fs::create_dir(core.join("children")).unwrap();
        File::create(core.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(core.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(core.join("url"))
            .unwrap()
            .write_all("fuchsia-pkg://fuchsia.com/core#meta/core.cm".as_bytes())
            .unwrap();

        let appmgr = core.join("children/appmgr");
        fs::create_dir(&appmgr).unwrap();
        fs::create_dir(appmgr.join("children")).unwrap();
        File::create(appmgr.join("component_type"))
            .unwrap()
            .write_all("static".as_bytes())
            .unwrap();
        File::create(appmgr.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(appmgr.join("url"))
            .unwrap()
            .write_all("fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm".as_bytes())
            .unwrap();

        let exec = appmgr.join("exec");
        fs::create_dir(&exec).unwrap();
        fs::create_dir(exec.join("expose")).unwrap();
        fs::create_dir(exec.join("in")).unwrap();
        fs::create_dir(exec.join("out")).unwrap();

        let hub = exec.join("out/hub");
        fs::create_dir(&hub).unwrap();
        fs::create_dir(hub.join("c")).unwrap();
        File::create(hub.join("job-id")).unwrap().write_all("12345".as_bytes()).unwrap();
        File::create(hub.join("name")).unwrap().write_all("sysmgr.cmx".as_bytes()).unwrap();
        fs::create_dir(hub.join("r")).unwrap();

        let sysmgr = hub.join("c/sysmgr");
        fs::create_dir(&sysmgr).unwrap();
        let child_dir_name = sysmgr.join("13579");
        fs::create_dir(&child_dir_name).unwrap();
        fs::create_dir(child_dir_name.join("c")).unwrap();
        fs::create_dir(child_dir_name.join("in")).unwrap();
        fs::create_dir(child_dir_name.join("r")).unwrap();
        File::create(child_dir_name.join("job-id")).unwrap().write_all("12345".as_bytes()).unwrap();
        File::create(child_dir_name.join("name")).unwrap().write_all("sysmgr".as_bytes()).unwrap();
        File::create(child_dir_name.join("url"))
            .unwrap()
            .write_all("fuchsia-pkg://fuchsia.com/sysmgr#meta/sysmgr.cm".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, IncludeDetails::Yes).await;

        assert!(v2_component.print_details("core").is_ok());
        assert!(v2_component.print_details("appmgr").is_ok());
        assert!(v2_component.print_details("sysmgr").is_ok());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn print_details_recursive_cannot_find_the_filter() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- children
        //    |- bootstrap
        //       |- children
        //       |- component_type
        //       |- id
        //       |- url
        // |- component_type
        // |- id
        // |- url
        fs::create_dir(root.join("children")).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        let bootstrap = root.join("children/bootstrap");
        fs::create_dir(&bootstrap).unwrap();
        fs::create_dir(bootstrap.join("children")).unwrap();
        File::create(bootstrap.join("component_type"))
            .unwrap()
            .write_all("static".as_bytes())
            .unwrap();
        File::create(bootstrap.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(bootstrap.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/bootstrap.cm".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, IncludeDetails::Yes).await;

        assert!(v2_component.print_details("asdfgh").is_err());
    }
}
