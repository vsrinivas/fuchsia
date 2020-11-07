// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    futures::future::{BoxFuture, FutureExt},
};

static SPACER: &str = "  ";
static UNKNOWN: &str = "UNKNOWN";

/// Used as a helper function to traverse <realm id>/r/, <realm id>/c/,
/// <component instance id>/c/, following through into their id subdirectories.
async fn open_id_directories(id_dir: Directory) -> Vec<Directory> {
    let mut realms = vec![];
    for id in id_dir.entries().await {
        assert!(id.chars().all(char::is_numeric));
        let realm = id_dir.open_dir(&id).await;
        realms.push(realm);
    }
    realms
}

fn visit_child_realms(child_realms_dir: Directory) -> BoxFuture<'static, Vec<V1Realm>> {
    async move {
        let mut child_realms = vec![];

        // visit all entries within <realm id>/r/
        for realm_name in child_realms_dir.entries().await {
            // visit <realm id>/r/<child realm name>/<child realm id>/
            let id_dir = child_realms_dir.open_dir(&realm_name).await;
            for realm_dir in open_id_directories(id_dir).await.drain(..) {
                child_realms.push(V1Realm::create(realm_dir).await);
            }
        }

        child_realms
    }
    .boxed()
}

/// Traverses a directory of named components, and recurses into each component directory.
/// Each component visited is added to the |child_components| vector.
fn visit_child_components(child_components_dir: Directory) -> BoxFuture<'static, Vec<V1Component>> {
    async move {
        let mut child_components = vec![];

        for component_name in child_components_dir.entries().await {
            // Visits */c/<component name>/<component instance id>.
            let id_dir = child_components_dir.open_dir(&component_name).await;
            for component_dir in open_id_directories(id_dir).await.drain(..) {
                child_components.push(V1Component::create(component_dir).await);
            }
        }

        child_components
    }
    .boxed()
}

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

#[derive(Debug, Eq, PartialEq)]
pub struct V1Realm {
    name: String,
    job_id: u32,
    child_realms: Vec<V1Realm>,
    child_components: Vec<V1Component>,
}

impl V1Realm {
    pub async fn create(realm_dir: Directory) -> V1Realm {
        let name = realm_dir.read_file("name").await;
        let job_id = realm_dir.read_file("job-id").await.parse::<u32>().unwrap();
        let child_realms_dir = realm_dir.open_dir("r").await;
        let child_components_dir = realm_dir.open_dir("c").await;
        V1Realm {
            name,
            job_id,
            child_realms: visit_child_realms(child_realms_dir).await,
            child_components: visit_child_components(child_components_dir).await,
        }
    }

    pub fn print_tree_recursive(&self, level: usize) {
        let space = SPACER.repeat(level - 1);
        println!("{}{} (realm)", space, self.name);
        for child in &self.child_components {
            child.print_tree_recursive(level + 1);
        }
        for child in &self.child_realms {
            child.print_tree_recursive(level + 1);
        }
    }

    pub fn print_details_recursive(&self, moniker_prefix: &str, filter: &str) {
        let moniker = format!("{}{}", moniker_prefix, self.name);

        // Print information about realms if there is no filter
        if filter.is_empty() || self.name.contains(filter) {
            println!("Moniker: {}", moniker);
            println!("Job ID: {}", self.job_id);
            println!("Type: v1 realm");
            println!("");
        }

        // Recurse on child components
        let moniker_prefix = format!("{}/", moniker);
        for child in &self.child_components {
            child.print_details_recursive(&moniker_prefix, filter);
        }

        // Recurse on child realms
        for child in &self.child_realms {
            child.print_details_recursive(&moniker_prefix, filter);
        }
    }
}

#[derive(Debug, Eq, PartialEq)]
pub struct V1Component {
    job_id: u32,
    name: String,
    url: String,
    merkleroot: Option<String>,
    incoming_capabilities: Vec<String>,
    outgoing_capabilities: Option<Vec<String>>,
    child_components: Vec<V1Component>,
}

impl V1Component {
    async fn create(component_dir: Directory) -> V1Component {
        let job_id = component_dir.read_file("job-id").await.parse::<u32>().unwrap();
        let url = component_dir.read_file("url").await;
        let name = component_dir.read_file("name").await;
        let pkg_dir = component_dir.open_dir("in").await.open_dir("pkg").await;

        let merkleroot =
            if pkg_dir.exists("meta").await { Some(pkg_dir.read_file("meta").await) } else { None };

        let child_components = if component_dir.exists("c").await {
            let child_components_dir = component_dir.open_dir("c").await;
            visit_child_components(child_components_dir).await
        } else {
            vec![]
        };

        let incoming_capabilities = {
            let in_dir = component_dir.open_dir("in").await;
            get_capabilities(in_dir).await
        };

        let outgoing_capabilities = if component_dir.exists("out").await {
            if let Some(out_dir) = component_dir.open_dir_timeout("out").await {
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

        V1Component {
            job_id,
            name,
            url,
            merkleroot,
            incoming_capabilities,
            outgoing_capabilities,
            child_components,
        }
    }

    fn print_tree_recursive(&self, level: usize) {
        let space = SPACER.repeat(level - 1);
        println!("{}{}", space, self.name);
        for child in &self.child_components {
            child.print_tree_recursive(level + 1);
        }
    }

    fn print_details_recursive(&self, moniker_prefix: &str, filter: &str) {
        let moniker = format!("{}{}", moniker_prefix, self.name);
        let unknown_merkle = UNKNOWN.to_string();
        let merkle = self.merkleroot.as_ref().unwrap_or(&unknown_merkle);

        if filter.is_empty() || self.url.contains(filter) || self.name.contains(filter) {
            println!("Moniker: {}", moniker);
            println!("URL: {}", self.url);
            println!("Job ID: {}", self.job_id);
            println!("Merkle Root: {}", merkle);
            println!("Type: v1 component");

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

            println!("");
        }

        // Recurse on children
        let moniker_prefix = format!("{}/", moniker);
        for child in &self.child_components {
            child.print_details_recursive(&moniker_prefix, filter);
        }
    }
}
