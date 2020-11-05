// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    crate::v1::V1Realm,
    futures::future::{BoxFuture, FutureExt},
    std::path::PathBuf,
};

static SPACER: &str = "  ";

async fn get_capabilities(capability_dir: Directory) -> Vec<String> {
    let mut entries = capability_dir.entries().await;

    for (index, name) in entries.iter().enumerate() {
        if name == "svc" {
            entries.remove(index);
            let svc_dir = capability_dir.open_dir("svc").await;
            let mut svc_entries = svc_dir.entries().await;
            entries.append(&mut svc_entries);
            break;
        }
    }

    entries.sort_unstable();
    entries
}

fn explore(name: String, hub_path: PathBuf) -> BoxFuture<'static, V2Component> {
    async move {
        let hub_dir = Directory::from_namespace(hub_path.clone());

        let url = hub_dir.read_file("url").await;
        let id = hub_dir.read_file("id").await.parse::<u32>().unwrap();
        let component_type = hub_dir.read_file("component_type").await;

        // Get the execution state
        let execution = if hub_dir.exists("exec").await {
            let exec_dir = hub_dir.open_dir("exec").await;
            Some(Execution::new(exec_dir).await)
        } else {
            None
        };

        // Recurse on the children
        let mut children: Vec<V2Component> = vec![];
        let child_dir = hub_dir.open_dir("children").await;
        for child_name in child_dir.entries().await {
            let child_path = hub_path.join("children").join(&child_name);
            let child = explore(child_name, child_path).await;
            children.push(child);
        }

        // If this component is appmgr, use it to explore the v1 component world
        let appmgr_root_v1_realm = if name == "appmgr" {
            let v1_hub_path = hub_path.join("exec/out/hub");
            let v1_hub_dir = Directory::from_namespace(v1_hub_path);
            Some(V1Realm::create(v1_hub_dir).await)
        } else {
            None
        };

        V2Component { name, url, id, component_type, children, execution, appmgr_root_v1_realm }
    }
    .boxed()
}

#[derive(Debug, Eq)]
pub struct ElfRuntime {
    pub job_id: u32,
    pub process_id: u32,
}

impl PartialEq for ElfRuntime {
    // For simplicity sake, assume that job id
    // and process id are irrelevant when
    // determining if components are the same.
    fn eq(&self, _other: &Self) -> bool {
        true
    }
}

#[derive(Debug, Eq, PartialEq)]
pub struct Execution {
    pub elf_runtime: Option<ElfRuntime>,
    pub incoming_capabilities: Vec<String>,
    pub outgoing_capabilities: Option<Vec<String>>,
    pub exposed_capabilities: Vec<String>,
}

impl Execution {
    async fn new(exec_dir: Directory) -> Self {
        // Get the ELF runtime
        let elf_runtime = if exec_dir.exists("runtime").await {
            let runtime_dir = exec_dir.open_dir("runtime").await;
            if runtime_dir.exists("elf").await {
                let elf_runtime_dir = runtime_dir.open_dir("elf").await;
                let job_id = elf_runtime_dir.read_file("job_id").await.parse::<u32>().unwrap();
                let process_id =
                    elf_runtime_dir.read_file("process_id").await.parse::<u32>().unwrap();
                Some(ElfRuntime { job_id, process_id })
            } else {
                None
            }
        } else {
            None
        };

        let incoming_capabilities = {
            let in_dir = exec_dir.open_dir("in").await;
            get_capabilities(in_dir).await
        };

        let outgoing_capabilities = if exec_dir.exists("out").await {
            if let Some(out_dir) = exec_dir.open_dir_timeout("out").await {
                Some(get_capabilities(out_dir).await)
            } else {
                // The directory exists, but it couldn't be opened.
                // This is probably because it isn't being served.
                None
            }
        } else {
            // The directory doesn't exist. This is probably because
            // there is no runtime on the component.
            None
        };

        let exposed_capabilities = {
            let expose_dir = exec_dir.open_dir("expose").await;
            get_capabilities(expose_dir).await
        };

        Execution {
            elf_runtime,
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
    pub url: String,
    pub id: u32,
    pub component_type: String,
    pub children: Vec<Self>,
    pub execution: Option<Execution>,
    pub appmgr_root_v1_realm: Option<V1Realm>,
}

impl V2Component {
    pub async fn explore(hub_path: PathBuf) -> Self {
        explore("<root>".to_string(), hub_path).await
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

    pub fn print_details(&self, filter: &str) {
        self.print_details_recursive("", filter)
    }

    fn print_details_recursive(&self, moniker_prefix: &str, filter: &str) {
        let moniker = format!("{}{}:{}", moniker_prefix, self.name, self.id);

        // Print if the filter matches
        if filter.is_empty() || self.url.contains(filter) || self.name.contains(filter) {
            println!("Moniker: {}", moniker);
            println!("URL: {}", self.url);
            println!("Type: v2 {} component", self.component_type);

            if let Some(execution) = &self.execution {
                execution.print_details();
            }

            println!("");
        }

        // Recurse on children
        let moniker_prefix = format!("{}/", moniker);
        for child in &self.children {
            child.print_details_recursive(&moniker_prefix, filter);
        }

        // If this component is appmgr, generate details for all v1 components
        if let Some(v1_realm) = &self.appmgr_root_v1_realm {
            v1_realm.print_details_recursive(&moniker_prefix, filter);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        std::fs::{self, File},
        tempfile::TempDir,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_capabilities() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // <root>
        // |- fuchsia.foo
        // |- hub
        // |- svc
        //    |- fuchsia.bar
        File::create(root.join("fuchsia.foo")).unwrap();
        File::create(root.join("hub")).unwrap();
        fs::create_dir(root.join("svc")).unwrap();
        File::create(root.join("svc").join("fuchsia.bar")).unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf());
        let capabilities = get_capabilities(root_dir).await;
        assert_eq!(
            capabilities,
            vec!["fuchsia.bar".to_string(), "fuchsia.foo".to_string(), "hub".to_string()]
        );
    }
}
