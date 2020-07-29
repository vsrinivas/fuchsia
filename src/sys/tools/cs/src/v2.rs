// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::v1::V1Realm,
    files_async::readdir_with_timeout,
    fuchsia_zircon as zx,
    futures::future::{BoxFuture, FutureExt},
    io_util::{directory, file},
};

static SPACER: &str = "  ";
static IO_TIMEOUT: zx::Duration = zx::Duration::from_seconds(2);

async fn get_file_names(path: String) -> Result<Vec<String>, files_async::Error> {
    let dir = directory::open_in_namespace(&path, io_util::OPEN_RIGHT_READABLE).unwrap();
    let result = readdir_with_timeout(&dir, IO_TIMEOUT).await;
    result.map(|entries| entries.iter().map(|entry| entry.name.clone()).collect())
}

async fn get_file(path: String) -> Result<String, file::ReadNamedError> {
    file::read_in_namespace_to_string_with_timeout(&path, IO_TIMEOUT).await
}

async fn get_services(path: String) -> Vec<String> {
    let full_path = format!("{}/svc", path);
    let result = get_file_names(full_path).await;
    match result {
        Ok(entries) => entries,
        Err(_) => vec![],
    }
}

fn generate_services(services_type: &str, services: &Vec<String>, lines: &mut Vec<String>) {
    lines.push(format!("- {} ({})", services_type, services.len()));
    for service in services {
        lines.push(format!("{}- {}", SPACER, service));
    }
}

pub struct V2Component {
    name: String,
    url: String,
    id: String,
    component_type: String,
    children: Vec<Self>,
    in_services: Vec<String>,
    out_services: Vec<String>,
    exposed_services: Vec<String>,
    appmgr_root_v1_realm: Option<V1Realm>,
}

impl V2Component {
    pub async fn new_root_component(hub_path: String) -> Self {
        Self::explore("<root>".to_string(), hub_path).await
    }

    fn explore(name: String, hub_path: String) -> BoxFuture<'static, Self> {
        async move {
            let url_path = format!("{}/url", hub_path);
            let id_path = format!("{}/id", hub_path);
            let component_type_path = format!("{}/component_type", hub_path);

            let url = get_file(url_path).await.expect("Could not read url from hub");
            let id = get_file(id_path).await.expect("Could not read id from hub");
            let component_type: String = get_file(component_type_path)
                .await
                .expect("Could not read component_type from hub");

            let exec_path = format!("{}/exec", hub_path);
            let in_services = get_services(format!("{}/in", exec_path)).await;
            let out_services = get_services(format!("{}/out", exec_path)).await;
            let exposed_services = get_services(format!("{}/expose", exec_path)).await;

            // Recurse on the children
            let child_path = format!("{}/children", hub_path);
            let child_names =
                get_file_names(child_path).await.expect("Could not get children from hub");
            let mut children: Vec<Self> = vec![];
            for child_name in child_names {
                let path = format!("{}/children/{}", hub_path, child_name);
                let child = Self::explore(child_name, path).await;
                children.push(child);
            }

            // If this component is appmgr, use it to explore the v1 component world
            let appmgr_root_v1_realm = if name == "appmgr" {
                let path_to_v1_hub = format!("{}/exec/out/hub", hub_path);
                Some(
                    V1Realm::create(path_to_v1_hub).expect("Could not traverse v1 component world"),
                )
            } else {
                None
            };

            Self {
                name,
                url,
                id,
                component_type,
                children,
                in_services,
                out_services,
                exposed_services,
                appmgr_root_v1_realm,
            }
        }
        .boxed()
    }

    pub fn generate_tree(&self) -> Vec<String> {
        let mut lines: Vec<String> = vec![];
        self.generate_tree_recursive(1, &mut lines);
        lines
    }

    fn generate_tree_recursive(&self, level: usize, lines: &mut Vec<String>) {
        let space = SPACER.repeat(level - 1);
        let line = format!("{}{}", space, self.name);
        lines.push(line);
        for child in &self.children {
            child.generate_tree_recursive(level + 1, lines);
        }

        // If this component is appmgr, generate tree for all v1 components
        if let Some(v1_realm) = &self.appmgr_root_v1_realm {
            v1_realm.generate_tree_recursive(level + 1, lines);
        }
    }

    pub fn generate_details(&self, url_filter: &str) -> Vec<String> {
        let mut lines: Vec<String> = vec![];
        self.generate_details_recursive("", &mut lines, url_filter);
        lines
    }

    fn generate_details_recursive(&self, prefix: &str, lines: &mut Vec<String>, url_filter: &str) {
        let moniker = format!("{}{}:{}", prefix, self.name, self.id);

        // Print if the filter matches
        if url_filter.is_empty() || self.url.contains(url_filter) {
            lines.push(moniker.clone());
            lines.push(format!("- URL: {}", self.url));
            lines.push(format!("- Type: v2 {} component", self.component_type));
            generate_services("Exposed Services", &self.exposed_services, lines);
            generate_services("Incoming Services", &self.in_services, lines);
            generate_services("Outgoing Services", &self.out_services, lines);
            lines.push("".to_string());
        }

        // Recurse on children
        let prefix = format!("{}/", moniker);
        for child in &self.children {
            child.generate_details_recursive(&prefix, lines, url_filter);
        }

        // If this component is appmgr, generate details for all v1 components
        if let Some(v1_realm) = &self.appmgr_root_v1_realm {
            v1_realm.generate_details_recursive(&prefix, lines, url_filter);
        }
    }
}
