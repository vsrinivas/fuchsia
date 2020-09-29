// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use crate::component::types::{
    ComponentLaunchRequest, ComponentLaunchResponse, ComponentSearchRequest, ComponentSearchResult,
};
use anyhow::Error;
use fidl_fuchsia_sys::ComponentControllerEvent;
use fuchsia_component::client;
use fuchsia_syslog::macros::fx_log_info;
use fuchsia_syslog::macros::*;
use futures::StreamExt;
use serde_json::{from_value, Value};
use std::collections::HashSet;
use std::fs;
use std::path::Path;
use std::path::PathBuf;

/// Perform operations related to Component.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct ComponentFacade {}

impl ComponentFacade {
    pub fn new() -> ComponentFacade {
        ComponentFacade {}
    }

    /// Parse component url and return app created by launch function
    /// # Arguments
    /// * `args`: will be parsed to ComponentLaunchRequest
    /// * `url`: full url of the component
    /// * `arguments`: optional arguments for the component
    async fn create_launch_app(&self, args: Value) -> Result<client::App, Error> {
        let tag = "ComponentFacade::create_launch_app";
        let req: ComponentLaunchRequest = from_value(args)?;
        // check if it's full url
        let component_url = match req.url {
            Some(x) => {
                if !x.starts_with("fuchsia-pkg") {
                    return Err(format_err!("Need full component url to launch"));
                }
                fx_log_info!(
                    "Executing Launch {} in Component Facade with arguments {:?}.",
                    x,
                    req.arguments
                );
                x
            }
            None => return Err(format_err!("Need full component url to launch")),
        };
        let launcher = match client::launcher() {
            Ok(r) => r,
            Err(err) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to get launcher service: {}", err)
            ),
        };
        let app = client::launch(&launcher, component_url.to_string(), req.arguments)?;
        Ok(app)
    }

    /// Launch component with url and optional arguments and detach directly
    /// # Arguments
    /// * `args`: will be parsed to ComponentLaunchRequest in create_launch_app
    /// * `url`: url of the component
    /// * `arguments`: optional arguments for the component
    pub async fn launch(&self, args: Value) -> Result<ComponentLaunchResponse, Error> {
        let tag = "ComponentFacade::launch";
        let launch_app = Some(self.create_launch_app(args).await?);
        let app = match launch_app {
            Some(p) => p,
            None => fx_err_and_bail!(&with_line!(tag), "Failed to launch component."),
        };
        let mut code = 0;
        let mut component_stream = app.controller().take_event_stream();
        match component_stream
            .next()
            .await
            .expect("component event stream ended before termination event")?
        {
            // detach if succeeds
            ComponentControllerEvent::OnDirectoryReady {} => {
                app.controller().detach()?;
            }
            // if there's exception (like url package not found, return fail)
            ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
                code = return_code;
                if return_code != 0 {
                    fx_log_info!(
                        "Component terminated unexpectedly. Code: {}. Reason: {:?}",
                        return_code,
                        termination_reason
                    );
                }
            }
        }
        match code {
            0 => Ok(ComponentLaunchResponse::Success),
            _ => Ok(ComponentLaunchResponse::Fail(code)),
        }
    }

    /// Search component with component's name under appmgr
    /// # Arguments
    /// * `args`: will be parsed to ComponentSearchRequest
    /// * `name`: name of the component (should be like "component.cmx")
    pub fn search(&self, args: Value) -> Result<ComponentSearchResult, Error> {
        let req: ComponentSearchRequest = from_value(args)?;
        let name = match req.name {
            Some(x) => {
                fx_log_info!("Searching Component {} in ComponentSearch Facade", x,);
                x
            }
            None => return Err(format_err!("Need name of the component to search.")),
        };

        let root_realm = Realm::create("/hub")?;
        let mut list: Vec<String> = Vec::new();
        let mut set: HashSet<String> = HashSet::new();
        root_realm.create_list(0, &mut list, &mut set)?;
        if set.contains(&name) {
            return Ok(ComponentSearchResult::Success);
        }

        Ok(ComponentSearchResult::NotFound)
    }

    /// List running Component under appmgr
    pub fn list(&self) -> Result<Vec<String>, Error> {
        fx_log_info!("List running Component under appmgr in ComponentSearch Facade",);
        let root_realm = Realm::create("/hub")?;
        let mut list: Vec<String> = Vec::new();
        let mut set: HashSet<String> = HashSet::new();
        root_realm.create_list(0, &mut list, &mut set)?;
        Ok(list)
    }
}

/* Code adapted from //src/sys/tools/cs to search component run under appmgr */
type ComponentsResult = Result<Vec<Component>, Error>;
type RealmsResult = Result<Vec<Realm>, Error>;
type DirEntryResult = Result<Vec<fs::DirEntry>, Error>;

struct Realm {
    job_id: u32,
    name: String,
    child_realms: Vec<Realm>,
    child_components: Vec<Component>,
}

impl Realm {
    fn create(realm_path: impl AsRef<Path>) -> Result<Realm, Error> {
        let job_id = fs::read_to_string(&realm_path.as_ref().join("job-id"))?;
        let name = fs::read_to_string(&realm_path.as_ref().join("name"))?;
        Ok(Realm {
            job_id: job_id.parse::<u32>()?,
            name,
            child_realms: visit_child_realms(&realm_path.as_ref())?,
            child_components: visit_child_components(&realm_path.as_ref())?,
        })
    }

    fn create_list(
        &self,
        layer: usize,
        list: &mut Vec<String>,
        set: &mut HashSet<String>,
    ) -> Result<(), Error> {
        let s = format!("{}: Realm[{}]: {}", layer, self.job_id, self.name);
        list.push(s);
        set.insert(self.name.clone());
        for comp in &self.child_components {
            comp.create_list(layer + 1, list, set)?;
        }
        for realm in &self.child_realms {
            realm.create_list(layer + 1, list, set)?;
        }
        Ok(())
    }
}

struct Component {
    job_id: u32,
    name: String,
    url: String,
    child_components: Vec<Component>,
}

impl Component {
    fn create(path: PathBuf) -> Result<Component, Error> {
        let job_id = fs::read_to_string(&path.join("job-id"))?;
        let url = fs::read_to_string(&path.join("url"))?;
        let name = fs::read_to_string(&path.join("name"))?;
        let child_components = visit_child_components(&path)?;
        Ok(Component { job_id: job_id.parse::<u32>()?, name, url, child_components })
    }

    fn create_list(
        &self,
        layer: usize,
        list: &mut Vec<String>,
        set: &mut HashSet<String>,
    ) -> Result<(), Error> {
        let s = format!("{}: {}[{}]: {}", layer, self.name, self.job_id, self.url);
        list.push(s);
        set.insert(self.name.clone());
        for child in &self.child_components {
            child.create_list(layer + 1, list, set)?;
        }
        Ok(())
    }
}

fn visit_child_realms(realm_path: &Path) -> RealmsResult {
    let child_realms_path = realm_path.join("r");
    let mut child_realms: Vec<Realm> = Vec::new();
    let entries = match fs::read_dir(&child_realms_path) {
        Ok(ent) => ent,
        Err(err) => {
            fx_log_err!("Error: {:?} realms path {:?} is not found.", err, child_realms_path);
            return Ok(child_realms);
        }
    };
    // visit all entries within <realm id>/r/
    for entry in entries {
        let entry = entry?;
        // visit <realm id>/r/<child realm name>/<child realm id>/
        let child_realm_id_dir_entries = find_id_directories(&entry.path())?;
        for child_realm_id_dir_entry in child_realm_id_dir_entries {
            let path = child_realm_id_dir_entry.path();
            child_realms.push(Realm::create(&path)?);
        }
    }
    Ok(child_realms)
}

/// Used as a helper function to traverse <realm id>/r/, <realm id>/c/,
/// <component instance id>/c/, following through into their id subdirectories.
fn find_id_directories(dir: &Path) -> DirEntryResult {
    let mut vec = vec![];
    let entries = match fs::read_dir(dir) {
        Ok(ent) => ent,
        Err(err) => {
            fx_log_err!("Error: {:?} directory {:?} is not found.", err, dir);
            return Ok(vec);
        }
    };
    for entry in entries {
        let entry = entry?;
        let path = entry.path();
        let id = {
            let name = path.file_name().ok_or_else(|| format_err!("no filename"))?;
            name.to_string_lossy()
        };

        // check for numeric directory name.
        if id.chars().all(char::is_numeric) {
            vec.push(entry)
        }
    }
    match !vec.is_empty() {
        true => Ok(vec),
        false => Err(format_err!("Directory not found")),
    }
}

/// Traverses a directory of named components, and recurses into each component directory.
/// Each component visited is added to the |child_components| vector.
fn visit_child_components(parent_path: &Path) -> ComponentsResult {
    let child_components_path = parent_path.join("c");
    if !child_components_path.is_dir() {
        return Ok(vec![]);
    }
    let mut child_components: Vec<Component> = Vec::new();
    let entries = match fs::read_dir(&child_components_path) {
        Ok(ent) => ent,
        Err(err) => {
            fx_log_err!(
                "Error: {:?} Component path {:?} is not found.",
                err,
                child_components_path
            );
            return Ok(child_components);
        }
    };
    for entry in entries {
        let entry = entry?;
        // Visits */c/<component name>/<component instance id>.
        let component_instance_id_dir_entries = find_id_directories(&entry.path())?;
        for component_instance_id_dir_entry in component_instance_id_dir_entries {
            let path = component_instance_id_dir_entry.path();
            child_components.push(Component::create(path)?);
        }
    }
    Ok(child_components)
}
