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

        let url = hub_dir.read_file("url").await.unwrap();
        let id = hub_dir.read_file("id").await.unwrap().parse::<u32>().unwrap();
        let component_type = hub_dir.read_file("component_type").await.unwrap();

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
            let runtime_dir = exec_dir.open_dir("runtime").await;
            if runtime_dir.exists("elf").await {
                let elf_runtime_dir = runtime_dir.open_dir("elf").await;
                let job_id =
                    elf_runtime_dir.read_file("job_id").await.unwrap().parse::<u32>().unwrap();
                let process_id =
                    elf_runtime_dir.read_file("process_id").await.unwrap().parse::<u32>().unwrap();
                Some(ElfRuntime { job_id, process_id })
            } else {
                None
            }
        } else {
            None
        };

        let in_dir = exec_dir.open_dir("in").await;

        let merkle_root = if in_dir.exists("pkg").await {
            let pkg_dir = in_dir.open_dir("pkg").await;
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

        let incoming_capabilities = { get_capabilities(in_dir).await };

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

        let exposed_capabilities = exec_dir.open_dir("expose").await.entries().await;

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
        std::io::Write,
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

        let exec_dir = Directory::from_namespace(exec.to_path_buf());
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

        let exec_dir = Directory::from_namespace(exec.to_path_buf());
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

        let exec_dir = Directory::from_namespace(exec.to_path_buf());
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

        let exec_dir = Directory::from_namespace(exec.to_path_buf());
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

        let exec_dir = Directory::from_namespace(exec.to_path_buf());
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

        let exec_dir = Directory::from_namespace(exec.to_path_buf());
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

        let exec_dir = Directory::from_namespace(exec.to_path_buf());
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

        let exec_dir = Directory::from_namespace(exec.to_path_buf());
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

        let exec_dir = Directory::from_namespace(exec.to_path_buf());
        let execution = Execution::new(exec_dir).await;

        assert_eq!(execution.exposed_capabilities, vec!["pkgfs".to_string()]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn v2_component_loads_component_type_and_id_and_name_and_url() {
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

        let v2_component = V2Component::explore(root.to_path_buf()).await;

        assert!(v2_component.appmgr_root_v1_realm.is_none());
        assert_eq!(v2_component.children, vec![]);
        assert_eq!(v2_component.component_type, "static");
        assert!(v2_component.execution.is_none());
        assert_eq!(v2_component.id, 0);
        assert_eq!(v2_component.name, "<root>");
        assert_eq!(v2_component.url, "fuchsia-boot:///#meta/root.cm");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn v2_component_loads_children() {
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

        let v2_component = V2Component::explore(root.to_path_buf()).await;

        assert_eq!(
            v2_component.children,
            vec![V2Component {
                name: "bootstrap".to_string(),
                url: "fuchsia-boot:///#meta/bootstrap.cm".to_string(),
                id: 0,
                component_type: "static".to_string(),
                appmgr_root_v1_realm: None,
                execution: None,
                children: vec![],
            }],
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn v2_component_loads_execution() {
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

        let v2_component = V2Component::explore(root.to_path_buf()).await;

        assert_eq!(
            v2_component.execution.unwrap().elf_runtime.unwrap(),
            ElfRuntime { job_id: 12345, process_id: 67890 }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn appmgr_explores_v1_realm() {
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

        let v2_component = V2Component::explore(root.to_path_buf()).await;

        let v1_hub_dir = Directory::from_namespace(appmgr.join("exec/out/hub"));
        assert!(v2_component.appmgr_root_v1_realm.is_none());

        assert_eq!(
            v2_component.children,
            vec![V2Component {
                name: "appmgr".to_string(),
                url: "fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm".to_string(),
                id: 0,
                component_type: "static".to_string(),
                appmgr_root_v1_realm: Some(V1Realm::create(v1_hub_dir).await),
                execution: Some(Execution {
                    elf_runtime: None,
                    merkle_root: None,
                    incoming_capabilities: vec![],
                    outgoing_capabilities: Some(vec!["hub".to_string()]),
                    exposed_capabilities: vec![],
                }),
                children: vec![],
            }],
        );

        assert_eq!(v2_component.component_type, "static");
        assert!(v2_component.execution.is_none());
        assert_eq!(v2_component.id, 0);
        assert_eq!(v2_component.name, "<root>");
        assert_eq!(v2_component.url, "fuchsia-boot:///#meta/root.cm");
    }
}
