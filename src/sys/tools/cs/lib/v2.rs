// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    crate::v1::V1Realm,
    crate::{get_capabilities, get_capabilities_timeout, Only, Subcommand, WIDTH_CS_TREE},
    anyhow::{format_err, Error},
    futures::future::{join_all, BoxFuture, FutureExt},
};

static SPACER: &str = "  ";

fn explore(
    name: String,
    hub_dir: Directory,
    subcommand: Subcommand,
) -> BoxFuture<'static, V2Component> {
    async move {
        // Recurse on the children
        let mut future_children = vec![];
        let children_dir = hub_dir.open_dir("children").expect("open_dir(`children`) failed!");
        for child_name in
            children_dir.entries().await.expect("Could not get entries from directory.")
        {
            let child_dir = children_dir
                .open_dir(&child_name)
                .expect(format!("open_dir(`{}`) failed!", child_name).as_str());
            let future_child = explore(child_name, child_dir, subcommand.clone());
            future_children.push(future_child);
        }
        let children = join_all(future_children).await;

        // If this component is appmgr, use it to explore the v1 component world
        let appmgr_root_v1_realm = if name == "appmgr" {
            let v1_hub_dir = hub_dir
                .open_dir("exec/out/hub")
                .expect("open_dir() failed: failed to open appmgr hub (`exec/out/hub`)!");
            Some(V1Realm::create(v1_hub_dir, subcommand.clone()).await)
        } else {
            None
        };

        // Get details if subcommand is ffx component show
        let details = if subcommand == Subcommand::Show {
            let (url, id, component_type) = futures::join!(
                hub_dir.read_file("url"),
                hub_dir.read_file("id"),
                hub_dir.read_file("component_type"),
            );

            let url = url.expect("read_file(`url`) failed!");

            let id =
                id.expect("read_file(`id`) failed!").parse::<u32>().expect("parse(`id`) failed!");

            let component_type = component_type.expect("read_file(`component_type`) failed!");

            Some(Details { url, id, component_type })
        } else {
            None
        };

        // Get the execution state
        let execution = if hub_dir.exists("exec").await {
            let exec_dir = hub_dir.open_dir("exec").expect("open_dir(`exec`) failed!");
            Some(Execution::new(exec_dir).await)
        } else {
            None
        };

        V2Component { name, children, appmgr_root_v1_realm, details, execution }
    }
    .boxed()
}

#[derive(Debug, Eq, PartialEq)]
pub struct ElfRuntime {
    pub job_id: u32,
    pub process_id: u32,
    pub process_start_time: i64,
    pub process_start_time_utc_estimate: Option<String>,
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
            if let Ok(runtime_dir) = exec_dir.open_dir("runtime") {
                let entries = runtime_dir.entries().await;
                if let Ok(entries) = entries {
                    if entries.iter().any(|s| s == "elf") {
                        if let Ok(elf_runtime_dir) = runtime_dir.open_dir("elf") {
                            let (
                                job_id,
                                process_id,
                                process_start_time,
                                process_start_time_utc_estimate,
                            ) = futures::join!(
                                elf_runtime_dir.read_file("job_id"),
                                elf_runtime_dir.read_file("process_id"),
                                elf_runtime_dir.read_file("process_start_time"),
                                elf_runtime_dir.read_file("process_start_time_utc_estimate"),
                            );

                            let job_id = job_id
                                .expect("read_file(`job_id`) failed!")
                                .parse::<u32>()
                                .expect("parse(`job_id`) failed!");

                            let process_id = process_id
                                .expect("read_file(`process_id`) failed!")
                                .parse::<u32>()
                                .expect("parse(`process_id`) failed!");

                            let process_start_time = process_start_time
                                .expect("read_file(`process_start_time`) failed!")
                                .parse::<i64>()
                                .expect("parse(`process_start_time`) failed!");

                            let process_start_time_utc_estimate =
                                process_start_time_utc_estimate.ok();

                            Some(ElfRuntime {
                                job_id,
                                process_id,
                                process_start_time,
                                process_start_time_utc_estimate,
                            })
                        } else {
                            println!("WARNING: Could not open elf directory");
                            None
                        }
                    } else {
                        None
                    }
                } else {
                    None
                }
            } else {
                println!("WARNING: Could not open runtime directory");
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

        let exposed_capabilities =
            expose_dir.entries().await.expect("Could not get entries from directory.");

        let incoming_capabilities = get_capabilities(in_dir).await;

        let outgoing_capabilities = if exec_dir.exists("out").await {
            let out_dir = exec_dir.open_dir("out").expect("open_dir(`out`) failed!");
            get_capabilities_timeout(out_dir).await.ok()
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
            println!("Process Start Time (ticks): {}", runtime.process_start_time);
            let process_start_time_utc_estimate = if let Some(process_start_time_utc_estimate) =
                runtime.process_start_time_utc_estimate.as_ref()
            {
                process_start_time_utc_estimate.clone()
            } else {
                "(not available)".to_string()
            };
            println!("Process Start Time (estimate): {}", process_start_time_utc_estimate);
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
    pub execution: Option<Execution>,
}

#[derive(Debug, Eq, PartialEq)]
pub struct Details {
    pub url: String,
    pub id: u32,
    pub component_type: String,
}

impl V2Component {
    pub async fn explore(hub_dir: Directory, subcommand: Subcommand) -> Self {
        // '.' is representation of the root.
        explore(".".to_string(), hub_dir, subcommand.clone()).await
    }

    pub fn print_tree(&self, only: Only, verbose: bool) {
        if verbose {
            println!(
                "{:<width_type$}{:<width_running_stopped$}Component Name",
                "Component Type",
                "Execution State",
                width_type = WIDTH_CS_TREE,
                width_running_stopped = WIDTH_CS_TREE
            );
        }
        self.print_tree_recursive(1, only, verbose);
    }

    fn print_tree_recursive(&self, level: usize, only: Only, verbose: bool) {
        let space = SPACER.repeat(level - 1);
        let running_or_stopped = match self.execution {
            Some(_) => "Running",
            None => "Stopped",
        };
        match only {
            Only::CMX => {}
            Only::CML => {
                if verbose {
                    println!(
                        "{:<width_type$}{:<width_running_stopped$}{}{}",
                        "CML",
                        running_or_stopped,
                        space,
                        self.name,
                        width_type = WIDTH_CS_TREE,
                        width_running_stopped = WIDTH_CS_TREE
                    );
                } else {
                    println!("{}{}", space, self.name);
                }
            }
            Only::Running => {
                if self.execution.is_some() {
                    if verbose {
                        println!(
                            "{:<width_type$}{:<width_running_stopped$}{}{}",
                            "CML",
                            "Running",
                            space,
                            self.name,
                            width_type = WIDTH_CS_TREE,
                            width_running_stopped = WIDTH_CS_TREE
                        );
                    } else {
                        println!("{}{}", space, self.name);
                    }
                }
            }
            Only::Stopped => {
                if self.execution == None {
                    if verbose {
                        if self.execution == None {
                            println!(
                                "{:<width_type$}{:<width_running_stopped$}{}{}",
                                "CML",
                                "Stopped",
                                space,
                                self.name,
                                width_type = WIDTH_CS_TREE,
                                width_running_stopped = WIDTH_CS_TREE
                            );
                        }
                    } else {
                        println!("{}{}", space, self.name);
                    }
                }
            }
            Only::All => {
                if verbose {
                    println!(
                        "{:<width_type$}{:<width_running_stopped$}{}{}",
                        "CML",
                        running_or_stopped,
                        space,
                        self.name,
                        width_type = WIDTH_CS_TREE,
                        width_running_stopped = WIDTH_CS_TREE
                    );
                } else {
                    println!("{}{}", space, self.name);
                }
            }
        }
        for child in &self.children {
            child.print_tree_recursive(level + 1, only, verbose);
        }

        // If this component is appmgr, generate tree for all v1 components
        if let Some(v1_realm) = &self.appmgr_root_v1_realm {
            v1_realm.print_tree_recursive(level + 1, only, verbose);
        }
    }

    pub fn print_details(&self, filter: &str) -> Result<(), Error> {
        if !self.print_details_recursive("", filter) {
            return Err(format_err!(
                "either it is not in the tree, or is part of a subtree that hasn't been resolved yet."
            ));
        };
        Ok(())
    }

    fn print_details_recursive(&self, moniker_prefix: &str, filter: &str) -> bool {
        let mut did_print = false;
        if let Some(details) = &self.details {
            let moniker = format!("{}{}", moniker_prefix, self.name);

            // Print if the filter matches
            if filter.is_empty() || details.url.contains(filter) || self.name.contains(filter) {
                println!("Moniker: {}", moniker);
                println!("URL: {}", details.url);
                println!("Type: v2 {} component", details.component_type);

                if let Some(execution) = &self.execution {
                    println!("Execution State: Running");
                    execution.print_details();
                } else {
                    println!("Execution State: Stopped");
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

    pub fn print_components_exposing_capability(&self, capability: &str) -> bool {
        if !self.print_components_exposing_capability_recursive("", &capability) {
            println!("No components exposing this capability.");
            return false;
        };
        true
    }

    fn print_components_exposing_capability_recursive(
        &self,
        moniker_prefix: &str,
        capability: &str,
    ) -> bool {
        let mut did_print = false;
        if let Some(execution) = &self.execution {
            let moniker = format!("{}{}", moniker_prefix, self.name);

            // Print if the capability is exposed by this component
            if execution.exposed_capabilities.contains(&capability.to_string()) {
                println!("{}", moniker);
                did_print = true;
            }

            // Recurse on children
            let moniker_prefix = format!("{}/", moniker);
            for child in &self.children {
                did_print |= child
                    .print_components_exposing_capability_recursive(&moniker_prefix, &capability);
            }
        }

        did_print
    }

    pub fn get_process_id_recursive(&self, url: &str, pids: &mut Vec<u32>) -> Vec<u32> {
        let mut process_ids = vec![];
        if let Some(details) = &self.details {
            if details.url == url {
                if let Some(execution) = &self.execution {
                    if let Some(elf_runtime) = &execution.elf_runtime {
                        process_ids.push(elf_runtime.process_id);
                    }
                }
            }
        } else {
            eprintln!("Error! `V2Component` doesn't have `Details`!");
        }

        for child in &self.children {
            let tmp_pids = child.get_process_id_recursive(url, pids);
            process_ids.extend(tmp_pids);
        }

        // If this component is appmgr, search for matching url in all v1 components
        if let Some(v1_realm) = &self.appmgr_root_v1_realm {
            let pids = v1_realm.get_process_id_recursive(url, pids);
            process_ids.extend(pids);
        }

        process_ids
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
        // .
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
        // .
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
        // .
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
        File::create(exec.join("runtime/elf/process_start_time"))
            .unwrap()
            .write_all("11121314".as_bytes())
            .unwrap();
        File::create(exec.join("runtime/elf/process_start_time_utc_estimate"))
            .unwrap()
            .write_all("who knows?".as_bytes())
            .unwrap();

        let exec_dir = Directory::from_namespace(exec.to_path_buf())
            .expect("from_namespace() failed: failed to open `exec` directory!");
        let execution = Execution::new(exec_dir).await;

        assert_eq!(
            execution.elf_runtime.unwrap(),
            ElfRuntime {
                job_id: 12345,
                process_id: 67890,
                process_start_time: 11121314,
                process_start_time_utc_estimate: Some("who knows?".to_string())
            }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn execution_loads_merkle_root() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
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
        // .
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
        // .
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
        // .
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
        // .
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
        // .
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
        // .
        // |- children
        fs::create_dir(root.join("children")).unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, Subcommand::List).await;

        assert!(v2_component.appmgr_root_v1_realm.is_none());
        assert_eq!(v2_component.children, vec![]);
        assert!(v2_component.details.is_none());
        assert_eq!(v2_component.name, ".");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn explore_loads_children() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
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
        let v2_component = V2Component::explore(root_dir, Subcommand::List).await;

        assert_eq!(
            v2_component.children,
            vec![V2Component {
                name: "bootstrap".to_string(),
                children: vec![],
                appmgr_root_v1_realm: None,
                details: None,
                execution: None,
            }],
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn explore_appmgr_explores_v1_realm() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
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
        let v2_component = V2Component::explore(root_dir, Subcommand::List).await;

        let v1_hub_dir = Directory::from_namespace(appmgr.join("exec/out/hub"))
            .expect("from_namespace() failed: failed to create directory!");
        assert!(v2_component.appmgr_root_v1_realm.is_none());

        assert_eq!(
            v2_component.children,
            vec![V2Component {
                name: "appmgr".to_string(),
                children: vec![],
                appmgr_root_v1_realm: Some(V1Realm::create(v1_hub_dir, Subcommand::List).await),
                details: None,
                execution: Some(Execution {
                    elf_runtime: None,
                    merkle_root: None,
                    incoming_capabilities: vec![],
                    outgoing_capabilities: Some(vec!["hub".to_string()]),
                    exposed_capabilities: vec![],
                }),
            }],
        );

        assert!(v2_component.details.is_none());
        assert_eq!(v2_component.name, ".");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn explore_with_subcommand_show_loads_component_type_and_id_and_name_and_url() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
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
        let v2_component = V2Component::explore(root_dir, Subcommand::Show).await;

        assert_eq!(v2_component.name, ".");
        assert_eq!(v2_component.children, vec![]);
        assert!(v2_component.appmgr_root_v1_realm.is_none());
        assert!(v2_component.execution.is_none());
        assert_eq!(
            v2_component.details,
            Some(Details {
                url: "fuchsia-boot:///#meta/root.cm".to_string(),
                id: 0,
                component_type: "static".to_string(),
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn explore_with_subcommand_show_appmgr_explores_v1_realm() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
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
        let v2_component = V2Component::explore(root_dir, Subcommand::Show).await;

        let v1_hub_dir = Directory::from_namespace(appmgr.join("exec/out/hub"))
            .expect("from_namespace() failed: failed to open appmgr v1 hub directory!");
        assert!(v2_component.appmgr_root_v1_realm.is_none());

        assert_eq!(
            v2_component.children,
            vec![V2Component {
                name: "appmgr".to_string(),
                children: vec![],
                appmgr_root_v1_realm: Some(V1Realm::create(v1_hub_dir, Subcommand::Show).await),
                details: Some(Details {
                    url: "fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm".to_string(),
                    id: 0,
                    component_type: "static".to_string(),
                }),
                execution: Some(Execution {
                    elf_runtime: None,
                    merkle_root: None,
                    incoming_capabilities: vec![],
                    outgoing_capabilities: Some(vec!["hub".to_string()]),
                    exposed_capabilities: vec![],
                }),
            }],
        );

        assert_eq!(v2_component.name, ".");
        assert!(v2_component.execution.is_none());
        assert_eq!(
            v2_component.details,
            Some(Details {
                url: "fuchsia-boot:///#meta/root.cm".to_string(),
                id: 0,
                component_type: "static".to_string(),
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn explore_with_subcommand_show_loads_children() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
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
        let v2_component = V2Component::explore(root_dir, Subcommand::Show).await;

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
                }),
                execution: None,
            }],
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn explore_with_subcommand_list_loads_execution() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
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
        File::create(exec.join("runtime/elf/process_start_time"))
            .unwrap()
            .write_all("11121314".as_bytes())
            .unwrap();
        File::create(exec.join("runtime/elf/process_start_time_utc_estimate"))
            .unwrap()
            .write_all("bleep".as_bytes())
            .unwrap();
        File::create(root.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, Subcommand::List).await;

        assert_eq!(
            v2_component.execution.unwrap().elf_runtime.unwrap(),
            ElfRuntime {
                job_id: 12345,
                process_id: 67890,
                process_start_time: 11121314,
                process_start_time_utc_estimate: Some("bleep".to_string())
            }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn explore_with_subcommand_select_loads_execution() {
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
        File::create(exec.join("runtime/elf/process_start_time"))
            .unwrap()
            .write_all("11121314".as_bytes())
            .unwrap();
        File::create(root.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, Subcommand::Select).await;

        assert_eq!(
            v2_component.execution.unwrap().elf_runtime.unwrap(),
            ElfRuntime {
                job_id: 12345,
                process_id: 67890,
                process_start_time: 11121314,
                process_start_time_utc_estimate: None
            }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn print_components_exposing_capability_finds_components() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- children
        // |- component_type
        // |- exec
        //    |- expose
        //       |- diagnostics
        //       |- fuchsia.diagnostics.internal.LogStatsController
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
        File::create(exec.join("expose/diagnostics")).unwrap();
        File::create(exec.join("expose/fuchsia.diagnostics.internal.LogStatsController")).unwrap();
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
        File::create(exec.join("runtime/elf/process_start_time"))
            .unwrap()
            .write_all("11121314".as_bytes())
            .unwrap();
        File::create(root.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, Subcommand::Select).await;

        assert_eq!(v2_component.print_components_exposing_capability("diagnostics"), true);
        assert_eq!(
            v2_component.print_components_exposing_capability(
                "fuchsia.diagnostics.internal.LogStatsController"
            ),
            true
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn print_components_exposing_capability_does_not_find_components() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- children
        // |- component_type
        // |- exec
        //    |- expose
        //       |- diagnostics
        //       |- fuchsia.diagnostics.internal.LogStatsController
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
        File::create(exec.join("expose/diagnostics")).unwrap();
        File::create(exec.join("expose/fuchsia.diagnostics.internal.LogStatsController")).unwrap();
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
        File::create(exec.join("runtime/elf/process_start_time"))
            .unwrap()
            .write_all("11121314".as_bytes())
            .unwrap();
        File::create(root.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, Subcommand::Select).await;

        assert_eq!(v2_component.print_components_exposing_capability("asdfgh"), false);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn print_details_recursive_finds_v2_filter() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
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
        let v2_component = V2Component::explore(root_dir, Subcommand::Show).await;

        assert!(v2_component.print_details("bootstrap").is_ok());
        assert!(v2_component.print_details("archivist").is_ok());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn print_details_recursive_finds_v1_filter() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
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
        let v2_component = V2Component::explore(root_dir, Subcommand::Show).await;

        assert!(v2_component.print_details("core").is_ok());
        assert!(v2_component.print_details("appmgr").is_ok());
        assert!(v2_component.print_details("sysmgr").is_ok());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn print_details_recursive_cannot_find_the_filter() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
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
        let v2_component = V2Component::explore(root_dir, Subcommand::Show).await;

        assert!(v2_component.print_details("asdfgh").is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_process_id_recursive_gets_process_id() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- children
        // |- component_type
        // |- exec
        //    |- expose
        //       |- diagnostics
        //       |- fuchsia.diagnostics.internal.LogStatsController
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
        File::create(exec.join("expose/diagnostics")).unwrap();
        File::create(exec.join("expose/fuchsia.diagnostics.internal.LogStatsController")).unwrap();
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
        File::create(exec.join("runtime/elf/process_start_time"))
            .unwrap()
            .write_all("11121314".as_bytes())
            .unwrap();
        File::create(root.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, Subcommand::Show).await;

        assert_eq!(
            v2_component.get_process_id_recursive("fuchsia-boot:///#meta/root.cm", &mut vec![]),
            vec![67890]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_process_id_recursive_does_not_get_process_id_with_no_matching_url() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- children
        // |- component_type
        // |- exec
        //    |- expose
        //       |- diagnostics
        //       |- fuchsia.diagnostics.internal.LogStatsController
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
        File::create(exec.join("expose/diagnostics")).unwrap();
        File::create(exec.join("expose/fuchsia.diagnostics.internal.LogStatsController")).unwrap();
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
        File::create(exec.join("runtime/elf/process_start_time"))
            .unwrap()
            .write_all("11121314".as_bytes())
            .unwrap();
        File::create(root.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, Subcommand::Show).await;

        assert_eq!(v2_component.get_process_id_recursive("random-string", &mut vec![]), []);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_process_id_recursive_does_not_get_process_id_when_no_process_id_exists() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- children
        // |- component_type
        // |- exec
        //    |- expose
        //       |- diagnostics
        //       |- fuchsia.diagnostics.internal.LogStatsController
        //    |- in
        // |- id
        // |- url
        fs::create_dir(root.join("children")).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        let exec = root.join("exec");
        fs::create_dir(&exec).unwrap();
        fs::create_dir(exec.join("expose")).unwrap();
        File::create(exec.join("expose/diagnostics")).unwrap();
        File::create(exec.join("expose/fuchsia.diagnostics.internal.LogStatsController")).unwrap();
        fs::create_dir(exec.join("in")).unwrap();
        File::create(root.join("id")).unwrap().write_all("0".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root hub directory!");
        let v2_component = V2Component::explore(root_dir, Subcommand::Show).await;

        assert_eq!(
            v2_component.get_process_id_recursive("fuchsia-boot:///#meta/root.cm", &mut vec![]),
            []
        );
    }
}
