// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    crate::{get_capabilities, get_capabilities_timeout, Only, Subcommand, WIDTH_CS_TREE},
    futures::future::{join_all, BoxFuture, FutureExt},
    std::sync::Arc,
};

static SPACER: &str = "  ";
static UNKNOWN: &str = "UNKNOWN";

/// Used as a helper function to traverse <realm id>/r/, <realm id>/c/,
/// <component instance id>/c/, following through into their id subdirectories.
async fn open_id_directories(id_dir: Directory) -> Vec<Directory> {
    let mut ids = id_dir.entries().await.expect("Could not get entries from directory.");
    let proxy = Arc::new(id_dir);
    join_all(ids.drain(..).map(move |id| {
        let proxy = proxy.clone();
        async move {
            assert!(id.chars().all(char::is_numeric));
            let realm = proxy.open_dir(&id).expect(
                format!("open_dir() failed: failed to open realm directory with id `{}`!", &id)
                    .as_str(),
            );
            realm
        }
    }))
    .await
}

fn visit_child_realms(
    child_realms_dir: Directory,
    subcommand: Subcommand,
) -> BoxFuture<'static, Vec<V1Realm>> {
    async move {
        let mut realm_names =
            child_realms_dir.entries().await.expect("Could not get entries from directory.");
        let proxy = Arc::new(child_realms_dir);
        // visit all entries within <realm id>/r/
        join_all(realm_names.drain(..).map(move |realm_name| {
            let proxy = proxy.clone();
            async move {
                // visit <realm id>/r/<child realm name>/<child realm id>/
                let id_dir = proxy.open_dir(&realm_name).expect(
                    format!(
                        "open_dir() failed: failed to open child realm directory with id `{}`!",
                        &realm_name
                    )
                    .as_str(),
                );
                join_all(
                    open_id_directories(id_dir)
                        .await
                        .drain(..)
                        .map(|d| V1Realm::create(d, subcommand.clone())),
                )
                .await
            }
        }))
        .await
        .drain(..)
        .flatten()
        .collect()
    }
    .boxed()
}

/// Traverses a directory of named components, and recurses into each component directory.
/// Each component visited is added to the |child_components| vector.
fn visit_child_components(
    child_components_dir: Directory,
    subcommand: Subcommand,
) -> BoxFuture<'static, Vec<V1Component>> {
    async move {
        let mut component_names =
            child_components_dir.entries().await.expect("Could not get entries from directory.");
        let proxy = Arc::new(child_components_dir);
        join_all(component_names.drain(..).map(move |component_name| {
            let proxy = proxy.clone();
            async move {
                // Visits */c/<component name>/<component instance id>.
                let id_dir = proxy.open_dir(&component_name).expect(
                    format!("open_dir() failed: failed to open `{}` directory!", &component_name)
                        .as_str(),
                );
                join_all(
                    open_id_directories(id_dir)
                        .await
                        .drain(..)
                        .map(|d| V1Component::create(d, subcommand.clone())),
                )
                .await
            }
        }))
        .await
        .drain(..)
        .flatten()
        .collect()
    }
    .boxed()
}

#[derive(Debug, Eq, PartialEq)]
pub struct V1Realm {
    pub name: String,
    pub job_id: u32,
    pub child_realms: Vec<V1Realm>,
    pub child_components: Vec<V1Component>,
}

impl V1Realm {
    pub async fn create(realm_dir: Directory, subcommand: Subcommand) -> V1Realm {
        let child_realms_dir = realm_dir.open_dir("r").expect("open_dir(`r`) failed!");
        let child_components_dir = realm_dir.open_dir("c").expect("open_dir(`c`) failed!");

        let (name, job_id, child_realms, child_components) = futures::join!(
            realm_dir.read_file("name"),
            realm_dir.read_file("job-id"),
            visit_child_realms(child_realms_dir, subcommand.clone()),
            visit_child_components(child_components_dir, subcommand.clone()),
        );

        let name = name.expect("read_file(`name`) failed!");

        let job_id = job_id
            .expect("read_file(`job-id`) failed!")
            .parse::<u32>()
            .expect("parse(`job-id`) failed!");

        V1Realm { name, job_id, child_realms, child_components }
    }

    pub fn print_tree_recursive(&self, level: usize, only: Only, verbose: bool) {
        let space = SPACER.repeat(level - 1);
        if only == Only::CMX || only == Only::Running || only == Only::All {
            if verbose {
                println!(
                    "{:<width_type$}{:<width_running_stopped$}{}{}",
                    "Realm",
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
        for child in &self.child_components {
            child.print_tree_recursive(level + 1, only, verbose);
        }
        for child in &self.child_realms {
            child.print_tree_recursive(level + 1, only, verbose);
        }
    }

    pub fn print_details_recursive(&self, moniker_prefix: &str, filter: &str) -> bool {
        let mut did_print = false;
        let moniker = format!("{}{}", moniker_prefix, self.name);

        // Print information about realms if there is no filter
        if filter.is_empty() || self.name.contains(filter) {
            println!("Moniker: {}", moniker);
            println!("Job ID: {}", self.job_id);
            println!("Type: v1 realm");
            println!("");
            did_print = true;
        }

        // Recurse on child components
        let moniker_prefix = format!("{}/", moniker);
        for child in &self.child_components {
            did_print |= child.print_details_recursive(&moniker_prefix, filter);
        }

        // Recurse on child realms
        for child in &self.child_realms {
            did_print |= child.print_details_recursive(&moniker_prefix, filter);
        }

        did_print
    }
}

#[derive(Debug, Eq, PartialEq)]
pub struct V1Component {
    pub name: String,
    pub child_components: Vec<V1Component>,
    pub details: Option<Details>,
}

#[derive(Debug, Eq, PartialEq)]
pub struct Details {
    pub job_id: u32,
    pub process_id: Option<u32>,
    pub url: String,
    pub merkle_root: Option<String>,
    pub incoming_capabilities: Vec<String>,
    pub outgoing_capabilities: Option<Vec<String>>,
}

impl V1Component {
    async fn create(component_dir: Directory, subcommand: Subcommand) -> V1Component {
        let name = component_dir.read_file("name").await.expect("read_file(`name`) failed!");

        let child_components = if component_dir.exists("c").await {
            let child_components_dir = component_dir.open_dir("c").expect("open_dir(`c`) failed!");
            visit_child_components(child_components_dir, subcommand.clone()).await
        } else {
            vec![]
        };

        let details = if subcommand == Subcommand::Show {
            let (job_id, url) =
                futures::join!(component_dir.read_file("job-id"), component_dir.read_file("url"),);

            let job_id = job_id
                .expect("read_file(`job-id`) failed!")
                .parse::<u32>()
                .expect("parse(`job-id`) failed!");
            let url = url.expect("read_file(`url`) failed!");

            let process_id = if component_dir.exists("process-id").await {
                Some(
                    component_dir
                        .read_file("process-id")
                        .await
                        .expect("read_file(`process-id`) failed!")
                        .parse::<u32>()
                        .expect("parse(`process-id`) failed!"),
                )
            } else {
                None
            };

            let in_dir = component_dir.open_dir("in").expect("open_dir(`in`) failed!");

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

            let incoming_capabilities = get_capabilities(in_dir).await;

            let outgoing_capabilities = if component_dir.exists("out").await {
                get_capabilities_timeout(
                    component_dir.open_dir("out").expect("open_dir(`out`) failed!"),
                )
                .await
                .ok()
            } else {
                // The directory doesn't exist. This is probably because
                // there is no runtime on the component.
                None
            };

            Some(Details {
                job_id,
                process_id,
                url,
                merkle_root,
                incoming_capabilities,
                outgoing_capabilities,
            })
        } else {
            None
        };

        V1Component { name, child_components, details }
    }

    fn print_tree_recursive(&self, level: usize, only: Only, verbose: bool) {
        let space = SPACER.repeat(level - 1);
        if only == Only::CMX || only == Only::Running || only == Only::All {
            if verbose {
                println!(
                    "{:<width_type$}{:<width_running_stopped$}{}{}",
                    "CMX",
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
        for child in &self.child_components {
            child.print_tree_recursive(level + 1, only, verbose);
        }
    }

    fn print_details_recursive(&self, moniker_prefix: &str, filter: &str) -> bool {
        let mut did_print = false;
        let moniker = format!("{}{}", moniker_prefix, self.name);
        if let Some(details) = &self.details {
            let unknown_merkle = UNKNOWN.to_string();
            let merkle = details.merkle_root.as_ref().unwrap_or(&unknown_merkle);

            if filter.is_empty() || details.url.contains(filter) || self.name.contains(filter) {
                println!("Moniker: {}", moniker);
                println!("URL: {}", details.url);
                println!("Job ID: {}", details.job_id);
                if let Some(process_id) = details.process_id {
                    println!("Process ID: {}", process_id);
                }
                println!("Merkle Root: {}", merkle);
                println!("Type: v1 component");

                println!("Incoming Capabilities ({}):", details.incoming_capabilities.len());
                for capability in &details.incoming_capabilities {
                    println!("{}{}", SPACER, capability);
                }

                if let Some(outgoing_capabilities) = &details.outgoing_capabilities {
                    println!("Outgoing Capabilities ({}):", outgoing_capabilities.len());
                    for capability in outgoing_capabilities {
                        println!("{}{}", SPACER, capability);
                    }
                }

                println!("");
                did_print = true;
            }
        } else {
            eprintln!("Error! `V1Component` doesn't have `Details`!");
        }

        // Recurse on children
        let moniker_prefix = format!("{}/", moniker);
        for child in &self.child_components {
            did_print |= child.print_details_recursive(&moniker_prefix, filter);
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
    async fn v1_component_loads_name() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- in
        // |- job-id
        // |- name
        // |- process-id
        // |- url
        fs::create_dir(root.join("in")).unwrap();
        File::create(root.join("job-id")).unwrap().write_all("12345".as_bytes()).unwrap();
        File::create(root.join("name")).unwrap().write_all("cobalt.cmx".as_bytes()).unwrap();
        File::create(root.join("process-id")).unwrap().write_all("67890".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-pkg://fuchsia.com/cobalt#meta/cobalt.cmx".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root v1 hub directory!");
        let v1_component = V1Component::create(root_dir, Subcommand::List).await;

        assert_eq!(v1_component.details, None);
        assert_eq!(v1_component.name, "cobalt.cmx");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn v1_component_loads_child_components() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- c
        //    |- cobalt.cmx
        //       |- 13579
        //          |- in
        //          |- job-id
        //          |- name
        //          |- process-id
        //          |- url
        // |- in
        // |- job-id
        // |- name
        // |- process-id
        // |- url
        fs::create_dir(root.join("c")).unwrap();
        fs::create_dir(root.join("c/cobalt.cmx")).unwrap();
        let child_dir_name = root.join("c/cobalt.cmx/13579");
        fs::create_dir(&child_dir_name).unwrap();
        fs::create_dir(child_dir_name.join("in")).unwrap();
        File::create(child_dir_name.join("job-id")).unwrap().write_all("54321".as_bytes()).unwrap();
        File::create(child_dir_name.join("name")).unwrap();
        File::create(child_dir_name.join("process-id"))
            .unwrap()
            .write_all("09876".as_bytes())
            .unwrap();
        File::create(child_dir_name.join("url")).unwrap();
        fs::create_dir(root.join("in")).unwrap();
        File::create(root.join("job-id")).unwrap().write_all("12345".as_bytes()).unwrap();
        File::create(root.join("name")).unwrap();
        File::create(root.join("process-id")).unwrap().write_all("67890".as_bytes()).unwrap();
        File::create(root.join("url")).unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root v1 hub directory!");
        let v1_component = V1Component::create(root_dir, Subcommand::List).await;

        let child_dir = Directory::from_namespace(child_dir_name)
            .expect("from_namespace() failed: failed to open child_dir v1 hub directory!");
        assert_eq!(
            v1_component.child_components,
            vec![V1Component::create(child_dir, Subcommand::List).await]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn v1_component_with_details_loads_job_id_and_name_and_process_id_and_url() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- in
        // |- job-id
        // |- name
        // |- process-id
        // |- url
        fs::create_dir(root.join("in")).unwrap();
        File::create(root.join("job-id")).unwrap().write_all("12345".as_bytes()).unwrap();
        File::create(root.join("name")).unwrap().write_all("cobalt.cmx".as_bytes()).unwrap();
        File::create(root.join("process-id")).unwrap().write_all("67890".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-pkg://fuchsia.com/cobalt#meta/cobalt.cmx".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root v1 hub directory!");
        let v1_component = V1Component::create(root_dir, Subcommand::Show).await;

        assert_eq!(
            v1_component.details,
            Some(Details {
                job_id: 12345,
                process_id: Some(67890),
                url: "fuchsia-pkg://fuchsia.com/cobalt#meta/cobalt.cmx".to_string(),
                merkle_root: None,
                incoming_capabilities: vec![],
                outgoing_capabilities: None,
            })
        );

        assert_eq!(v1_component.name, "cobalt.cmx");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn v1_component_with_details_loads_missing_process_id() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- in
        // |- job-id
        // |- name
        // |- url
        fs::create_dir(root.join("in")).unwrap();
        File::create(root.join("job-id")).unwrap().write_all("12345".as_bytes()).unwrap();
        File::create(root.join("name")).unwrap().write_all("cobalt.cmx".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-pkg://fuchsia.com/cobalt#meta/cobalt.cmx".as_bytes())
            .unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root v1 hub directory!");
        let v1_component = V1Component::create(root_dir, Subcommand::Show).await;

        assert_eq!(v1_component.details.unwrap().process_id, None);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn v1_component_with_details_loads_merkle_root() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- in
        //    |- pkg
        //       |- meta
        // |- job-id
        // |- name
        // |- process-id
        // |- url
        fs::create_dir(root.join("in")).unwrap();
        fs::create_dir(root.join("in/pkg")).unwrap();
        File::create(root.join("in/pkg/meta"))
            .unwrap()
            .write_all(
                "eb4c673a880a232cc05363ff27691107c89d5b2766d995f782dac1056ecfe8c9".as_bytes(),
            )
            .unwrap();
        File::create(root.join("job-id")).unwrap().write_all("12345".as_bytes()).unwrap();
        File::create(root.join("name")).unwrap();
        File::create(root.join("process-id")).unwrap().write_all("67890".as_bytes()).unwrap();
        File::create(root.join("url")).unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root v1 hub directory!");
        let v1_component = V1Component::create(root_dir, Subcommand::Show).await;

        assert_eq!(
            v1_component.details.unwrap().merkle_root,
            Some("eb4c673a880a232cc05363ff27691107c89d5b2766d995f782dac1056ecfe8c9".to_string())
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn v1_component_with_details_loads_incoming_capabilities_and_outgoing_capabilities() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- in
        //    |- fuchsia.logger.logSink
        // |- job-id
        // |- name
        // |- out
        //    |- fuchsia.cobalt.Controller
        // |- process-id
        // |- url
        fs::create_dir(root.join("in")).unwrap();
        File::create(root.join("in/fuchsia.logger.logSink")).unwrap();
        File::create(root.join("job-id")).unwrap().write_all("12345".as_bytes()).unwrap();
        File::create(root.join("name")).unwrap();
        fs::create_dir(root.join("out")).unwrap();
        File::create(root.join("out/fuchsia.cobalt.Controller")).unwrap();
        File::create(root.join("process-id")).unwrap().write_all("67890".as_bytes()).unwrap();
        File::create(root.join("url")).unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root v1 hub directory!");
        let v1_component = V1Component::create(root_dir, Subcommand::Show).await;

        assert_eq!(
            v1_component.details,
            Some(Details {
                job_id: 12345,
                process_id: Some(67890),
                url: "".to_string(),
                merkle_root: None,
                incoming_capabilities: vec!["fuchsia.logger.logSink".to_string()],
                outgoing_capabilities: Some(vec!["fuchsia.cobalt.Controller".to_string()],)
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn v1_component_with_details_loads_child_components() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- c
        //    |- cobalt.cmx
        //       |- 13579
        //          |- in
        //          |- job-id
        //          |- name
        //          |- process-id
        //          |- url
        // |- in
        // |- job-id
        // |- name
        // |- process-id
        // |- url
        fs::create_dir(root.join("c")).unwrap();
        fs::create_dir(root.join("c/cobalt.cmx")).unwrap();
        let child_dir_name = root.join("c/cobalt.cmx/13579");
        fs::create_dir(&child_dir_name).unwrap();
        fs::create_dir(child_dir_name.join("in")).unwrap();
        File::create(child_dir_name.join("job-id")).unwrap().write_all("54321".as_bytes()).unwrap();
        File::create(child_dir_name.join("name")).unwrap();
        File::create(child_dir_name.join("process-id"))
            .unwrap()
            .write_all("09876".as_bytes())
            .unwrap();
        File::create(child_dir_name.join("url")).unwrap();
        fs::create_dir(root.join("in")).unwrap();
        File::create(root.join("job-id")).unwrap().write_all("12345".as_bytes()).unwrap();
        File::create(root.join("name")).unwrap();
        File::create(root.join("process-id")).unwrap().write_all("67890".as_bytes()).unwrap();
        File::create(root.join("url")).unwrap();

        let root_dir = Directory::from_namespace(root.to_path_buf())
            .expect("from_namespace() failed: failed to open root v1 hub directory!");
        let v1_component = V1Component::create(root_dir, Subcommand::Show).await;

        let child_dir = Directory::from_namespace(child_dir_name)
            .expect("from_namespace() failed: failed to open child_dir v1 hub directory!");
        assert_eq!(
            v1_component.child_components,
            vec![V1Component::create(child_dir, Subcommand::Show).await]
        );
    }
}
