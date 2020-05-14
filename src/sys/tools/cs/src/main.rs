// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! `cs` performs a Component Search on the current system.

use {
    anyhow::{format_err, Error},
    cs::inspect::{generate_inspect_object_tree, InspectObject},
    fdio,
    fidl_fuchsia_inspect::TreeMarker,
    fidl_fuchsia_inspect_deprecated::{InspectMarker, MetricValue, PropertyValue},
    fuchsia_async as fasync,
    fuchsia_inspect::reader::{self, NodeHierarchy, Property},
    std::{
        cmp::Reverse,
        collections::HashMap,
        fmt, fs,
        path::{Path, PathBuf},
    },
    structopt::StructOpt,
};

type ComponentsResult = Result<Vec<Component>, Error>;
type RealmsResult = Result<Vec<Realm>, Error>;
type TraversalResult = Result<(), Error>;
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

    fn write_indented(&self, f: &mut fmt::Formatter<'_>, indent: usize) -> fmt::Result {
        writeln!(f, "{}Realm[{}]: {}", " ".repeat(indent), self.job_id, self.name)?;

        for comp in &self.child_components {
            comp.write_indented(f, indent + 2)?;
        }

        for realm in &self.child_realms {
            realm.write_indented(f, indent + 2)?;
        }

        Ok(())
    }
}

impl fmt::Display for Realm {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.write_indented(f, 0)
    }
}

struct Component {
    job_id: u32,
    name: String,
    path: PathBuf,
    url: String,
    child_components: Vec<Component>,
}

impl Component {
    fn create(path: PathBuf) -> Result<Component, Error> {
        let job_id = fs::read_to_string(&path.join("job-id"))?;
        let url = fs::read_to_string(&path.join("url"))?;
        let name = fs::read_to_string(&path.join("name"))?;
        let child_components = visit_child_components(&path)?;
        Ok(Component { job_id: job_id.parse::<u32>()?, name, path, url, child_components })
    }

    fn write_indented(&self, f: &mut fmt::Formatter<'_>, indent: usize) -> fmt::Result {
        writeln!(f, "{}{}[{}]: {}", " ".repeat(indent), self.name, self.job_id, self.url)?;

        for child in &self.child_components {
            child.write_indented(f, indent + 2)?;
        }

        Ok(())
    }
}

impl fmt::Display for Component {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.write_indented(f, 0)
    }
}

fn visit_child_realms(realm_path: &Path) -> RealmsResult {
    let child_realms_path = realm_path.join("r");
    let entries = fs::read_dir(child_realms_path)?;
    let mut child_realms: Vec<Realm> = Vec::new();

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
    let entries = fs::read_dir(dir)?;
    let mut vec = vec![];
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

fn visit_system_objects(component_path: &Path, exclude_objects: &Vec<String>) -> TraversalResult {
    let channel_path = component_path
        .join("system_objects")
        .join(<InspectMarker as fidl::endpoints::ServiceMarker>::NAME);
    let inspect_object = generate_inspect_object_tree(&channel_path, &exclude_objects)?;
    visit_inspect_object(1, &inspect_object);
    Ok(())
}

fn visit_inspect_object(depth: usize, inspect_object: &InspectObject) {
    let indent = " ".repeat(depth);
    println!("{}{}", indent, inspect_object.inspect_object.name);
    for metric in &inspect_object.inspect_object.metrics {
        println!(
            "{} {}: {}",
            indent,
            metric.key,
            match &metric.value {
                MetricValue::IntValue(v) => format!("{}", v),
                MetricValue::UintValue(v) => format!("{}", v),
                MetricValue::DoubleValue(v) => format!("{}", v),
            },
        );
    }
    for property in &inspect_object.inspect_object.properties {
        println!(
            "{} {}: {}",
            indent,
            property.key,
            match &property.value {
                PropertyValue::Str(s) => s.clone(),
                PropertyValue::Bytes(_) => String::from("<binary>"),
            },
        );
    }
    for child in &inspect_object.child_inspect_objects {
        visit_inspect_object(depth + 1, child);
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
    let entries = fs::read_dir(&child_components_path)?;
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

fn inspect_realm(job_id: u32, exclude_objects: &Vec<String>, realm: &Realm) -> TraversalResult {
    for component in &realm.child_components {
        inspect_component(job_id, exclude_objects, component)?;
    }
    for child_realm in &realm.child_realms {
        inspect_realm(job_id, exclude_objects, child_realm)?;
    }
    Ok(())
}

fn inspect_component(
    job_id: u32,
    exclude_objects: &Vec<String>,
    component: &Component,
) -> TraversalResult {
    if component.job_id == job_id {
        println!("{}", component);
        visit_system_objects(&component.path, exclude_objects)?;
    }
    for component in &component.child_components {
        inspect_component(job_id, exclude_objects, component)?;
    }
    Ok(())
}

#[derive(StructOpt, Debug)]
#[structopt(
    name = "Component Statistics (cs) Reporting Tool",
    about = "Displays information about components on the system."
)]
struct Opt {
    /// Recursively output the Inspect trees of the components in the provided
    /// job Id.
    #[structopt(short = "i", long = "inspect")]
    job_id: Option<u32>,

    // Properties to exclude from display when presenting Inspect trees.
    #[structopt(
        short = "e",
        long = "exclude-objects",
        raw(use_delimiter = "true"),
        default_value = "stack,all_thread_stacks"
    )]
    exclude_objects: Vec<String>,

    #[structopt(long = "log-stats")]
    log_stats: bool,
}

// Number of log messages broken down by severity for a given component.
struct ComponentLogStats {
    component_url: String,
    fatal_logs: u64,
    error_logs: u64,
    warning_logs: u64,
    info_logs: u64,
    debug_logs: u64,
    trace_logs: u64,
    total_logs: u64,
}

impl ComponentLogStats {
    fn get_sort_key(&self) -> (u64, u64, u64, u64, u64, u64) {
        (
            self.fatal_logs,
            self.error_logs,
            self.warning_logs,
            self.info_logs,
            self.debug_logs,
            self.trace_logs,
        )
    }
}

impl From<NodeHierarchy> for ComponentLogStats {
    fn from(node: NodeHierarchy) -> ComponentLogStats {
        let map = node
            .properties
            .into_iter()
            .map(|x| match x {
                Property::Uint(name, value) => (name, value),
                _ => (String::from(""), 0),
            })
            .collect::<HashMap<_, _>>();
        ComponentLogStats {
            component_url: node.name,
            fatal_logs: map["fatal_logs"],
            error_logs: map["error_logs"],
            warning_logs: map["warning_logs"],
            info_logs: map["info_logs"],
            debug_logs: map["debug_logs"],
            trace_logs: map["trace_logs"],
            total_logs: map["total_logs"],
        }
    }
}

async fn print_log_stats() -> Result<(), Error> {
    let mut entries = fs::read_dir("/hub/c/archivist.cmx")?.collect::<Vec<_>>();
    if entries.len() == 0 {
        return Err(format_err!("No instance of archivist present in /hub/c/archivist.cmx"));
    } else if entries.len() > 1 {
        return Err(format_err!("Multiple instances of archivist present in /hub/c/archivist.cmx"));
    }

    let mut path = entries.remove(0)?.path();
    path.push("out/diagnostics/fuchsia.inspect.Tree");
    let path_str = path.to_str().ok_or(format_err!("Failed to convert path to string"))?;

    let (tree, server) = fidl::endpoints::create_proxy::<TreeMarker>()?;
    fdio::service_connect(&path_str, server.into_channel())?;
    let hierarchy = reader::read_from_tree(&tree).await?;

    let log_stats_node = hierarchy
        .children
        .into_iter()
        .find(|x| x.name == "log_stats")
        .ok_or(format_err!("Failed to find /log_stats node in the inspect hierarchy"))?;

    let per_component_node =
        log_stats_node.children.into_iter().find(|x| x.name == "by_component").ok_or(
            format_err!("Failed to find /log_stats/by_component node in the inspect hierarchy"),
        )?;

    let mut stats_list = per_component_node
        .children
        .into_iter()
        .map(|x| ComponentLogStats::from(x))
        .collect::<Vec<_>>();
    stats_list.sort_by_key(|x| Reverse(x.get_sort_key()));

    println!(
        "{:<7}{:<7}{:<7}{:<7}{:<7}{:<7}{:<7}{}",
        "FATAL", "ERROR", "WARN", "INFO", "DEBUG", "TRACE", "Total", "Component"
    );
    for stats in stats_list {
        println!(
            "{:<7}{:<7}{:<7}{:<7}{:<7}{:<7}{:<7}{}",
            stats.fatal_logs,
            stats.error_logs,
            stats.warning_logs,
            stats.info_logs,
            stats.debug_logs,
            stats.trace_logs,
            stats.total_logs,
            stats.component_url
        );
    }

    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> TraversalResult {
    // Visit the directory /hub and recursively traverse it, outputting information about the
    // component hierarchy. See https://fuchsia.dev/fuchsia-src/concepts/components/hub for more
    // information on the Hub directory structure.
    let opt = Opt::from_args();

    if opt.log_stats {
        print_log_stats().await?;
        return Ok(());
    }

    let root_realm = Realm::create("/hub")?;
    match opt.job_id {
        Some(job_id) => inspect_realm(job_id, &opt.exclude_objects, &root_realm)?,
        _ => println!("{}", root_realm),
    };
    Ok(())
}
