// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! `cs` performs a Component Search on the current system.

use {
    anyhow::{format_err, Error},
    cs::inspect::{generate_inspect_object_tree, InspectObject},
    fidl_fuchsia_inspect_deprecated::{InspectMarker, MetricValue, PropertyValue},
    fuchsia_async as fasync,
    fuchsia_inspect::{
        reader::{NodeHierarchy, Property},
        testing::InspectDataFetcher,
    },
    fuchsia_zircon as zx,
    std::{
        cmp::Reverse,
        collections::HashMap,
        fmt, fs,
        path::{Path, PathBuf},
        str::FromStr,
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
    merkleroot: Option<String>,
    child_components: Vec<Component>,
}

const UNKNOWN_VERSION: &str = "unknown version";

impl Component {
    fn create(path: PathBuf) -> Result<Component, Error> {
        let job_id = fs::read_to_string(&path.join("job-id"))?;
        let url = fs::read_to_string(&path.join("url"))?;
        let name = fs::read_to_string(&path.join("name"))?;
        let merkleroot = fs::read_to_string(&path.join("in/pkg/meta")).ok();
        let child_components = visit_child_components(&path)?;
        Ok(Component {
            job_id: job_id.parse::<u32>()?,
            name,
            path,
            url,
            merkleroot,
            child_components,
        })
    }

    fn write_indented(&self, f: &mut fmt::Formatter<'_>, indent: usize) -> fmt::Result {
        writeln!(
            f,
            "{}{}[{}]: {} ({})",
            " ".repeat(indent),
            self.name,
            self.job_id,
            self.url,
            self.merkleroot.as_ref().unwrap_or(&UNKNOWN_VERSION.to_string())
        )?;

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

#[derive(Debug, PartialEq, Eq)]
enum LogSeverity {
    TRACE = 0,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL,
}

impl FromStr for LogSeverity {
    type Err = Error;
    fn from_str(s: &str) -> Result<Self, Error> {
        match s.to_lowercase().as_str() {
            "fatal" => Ok(LogSeverity::FATAL),
            "error" => Ok(LogSeverity::ERROR),
            "warn" | "warning" => Ok(LogSeverity::WARN),
            "info" => Ok(LogSeverity::INFO),
            "debug" => Ok(LogSeverity::DEBUG),
            "trace" => Ok(LogSeverity::TRACE),
            _ => Err(format_err!("{} is not a valid log severity", s)),
        }
    }
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

    /// Properties to exclude from display when presenting Inspect trees.
    #[structopt(
        short = "e",
        long = "exclude-objects",
        raw(use_delimiter = "true"),
        default_value = "stack,all_thread_stacks"
    )]
    exclude_objects: Vec<String>,

    /// Show number of log messages for each component broken down by severity.
    #[structopt(long = "log-stats")]
    log_stats: bool,

    /// The minimum severity to show in the log stats.
    #[structopt(long = "min-severity", default_value = "info")]
    min_severity: LogSeverity,
}

// Number of log messages broken down by severity for a given component.
struct ComponentLogStats {
    component_url: String,
    log_counts: Vec<u64>,
    total_logs: u64,
}

impl ComponentLogStats {
    fn get_sort_key(&self) -> (u64, u64, u64, u64, u64) {
        (
            // Fatal logs are reported separately. They shouldn't affect the order of the output.
            self.get_count(LogSeverity::ERROR),
            self.get_count(LogSeverity::WARN),
            self.get_count(LogSeverity::INFO),
            self.get_count(LogSeverity::DEBUG),
            self.get_count(LogSeverity::TRACE),
        )
    }

    fn get_count(&self, severity: LogSeverity) -> u64 {
        self.log_counts[severity as usize]
    }
}

impl From<&NodeHierarchy> for ComponentLogStats {
    fn from(node: &NodeHierarchy) -> ComponentLogStats {
        let map = node
            .properties
            .iter()
            .map(|x| match x {
                Property::Int(name, value) => (name.as_str(), *value as u64),
                _ => ("", 0),
            })
            .collect::<HashMap<_, _>>();
        ComponentLogStats {
            component_url: node.name.clone(),
            log_counts: vec![
                map["trace_logs"],
                map["debug_logs"],
                map["info_logs"],
                map["warning_logs"],
                map["error_logs"],
                map["fatal_logs"],
            ],
            total_logs: map["total_logs"],
        }
    }
}

trait NodeHierarchyExt {
    fn get_child(&self, name: &str) -> Result<&NodeHierarchy, Error>;
    fn get_property_str(&self, name: &str) -> Result<&str, Error>;
}

impl NodeHierarchyExt for NodeHierarchy {
    fn get_child(&self, name: &str) -> Result<&NodeHierarchy, Error> {
        self.children
            .iter()
            .find(|x| x.name == name)
            .ok_or(format_err!("Failed to find {} node in the inspect hierarchy", name))
    }
    fn get_property_str(&self, name: &str) -> Result<&str, Error> {
        for property in &self.properties {
            if let Property::String(key, value) = property {
                if key == name {
                    return Ok(value);
                }
            }
        }
        return Err(format_err!("Failed to find string property {}", name));
    }
}

// Extracts the component start times from the inspect hierarchy.
fn get_component_start_times(inspect_root: &NodeHierarchy) -> Result<HashMap<&str, f64>, Error> {
    let mut res = HashMap::new();
    let events_node = inspect_root.get_child("event_stats")?.get_child("recent_events")?;

    for event in &events_node.children {
        // Extract the component name from the moniker. This allows us to match against the
        // component url from log stats.
        let moniker = event.get_property_str("moniker")?;
        let last_slash_index = moniker.rfind("/");
        if let Some(i) = last_slash_index {
            let last_colon_index = moniker.rfind(":");
            if let Some(j) = last_colon_index {
                res.insert(&moniker[i + 1..j], event.get_property_str("@time")?.parse::<f64>()?);
            }
        }
    }

    Ok(res)
}

async fn print_log_stats(opt: Opt) -> Result<(), Error> {
    let mut response = InspectDataFetcher::new()
        .add_selector("archivist.cmx:root/log_stats/by_component/*:*")
        .add_selector("archivist.cmx:root/event_stats/recent_events/*:*")
        .get()
        .await?;

    if response.len() != 1 {
        return Err(format_err!("Expected one inspect tree, received {}", response.len()));
    }

    let hierarchy = response.pop().unwrap();

    let stats_node = hierarchy.get_child("log_stats")?.get_child("by_component")?;
    let mut stats_list =
        stats_node.children.iter().map(|x| ComponentLogStats::from(x)).collect::<Vec<_>>();
    stats_list.sort_by_key(|x| Reverse(x.get_sort_key()));

    let start_times = get_component_start_times(&hierarchy)?;

    // Number of fatal logs is expected to be zero. If that's not the case, report it here.
    for stats in &stats_list {
        if stats.get_count(LogSeverity::FATAL) != 0 {
            println!(
                "Found {} fatal log messages for component {}",
                stats.get_count(LogSeverity::FATAL),
                stats.component_url
            );
        }
    }

    //  Min severity cannot be FATAL.
    let min_severity =
        if opt.min_severity == LogSeverity::FATAL { LogSeverity::ERROR } else { opt.min_severity };

    let min_severity_int = min_severity as usize;
    let max_severity_int = LogSeverity::ERROR as usize;

    let severity_strs = vec!["TRACE", "DEBUG", "INFO", "WARN", "ERROR"];
    let mut table_str = String::new();
    for i in (min_severity_int..=max_severity_int).rev() {
        table_str.push_str(&format!("{:<7}", severity_strs[i]));
    }
    table_str.push_str(&format!("{:<7}{:<10}{}\n", "Total", "ERROR/h", "Component"));

    let now = zx::Time::get(zx::ClockId::Monotonic).into_nanos() / 1_000_000_000;

    for stats in stats_list {
        let last_slash_index = stats.component_url.rfind("/");
        let short_name = match last_slash_index {
            Some(index) => &stats.component_url[index + 1..],
            None => stats.component_url.as_str(),
        };
        for i in (min_severity_int..=max_severity_int).rev() {
            table_str.push_str(&format!("{:<7}", stats.log_counts[i]));
        }
        table_str.push_str(&format!("{:<7}", stats.total_logs));
        let start_time = match start_times.get(short_name) {
            Some(s) => *s as i64,
            None => 0,
        };
        let uptime_in_hours = (now - start_time) as f64 / 60.0;
        let error_rate = stats.get_count(LogSeverity::ERROR) as f64 / uptime_in_hours;
        table_str.push_str(&format!("{:<10.4}", error_rate));
        table_str.push_str(&format!("{}\n", short_name));
    }

    print!("{}", table_str);

    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> TraversalResult {
    // Visit the directory /hub and recursively traverse it, outputting information about the
    // component hierarchy. See https://fuchsia.dev/fuchsia-src/concepts/components/hub for more
    // information on the Hub directory structure.
    let opt = Opt::from_args();

    if opt.log_stats {
        print_log_stats(opt).await?;
        return Ok(());
    }

    let root_realm = Realm::create("/hub")?;
    match opt.job_id {
        Some(job_id) => inspect_realm(job_id, &opt.exclude_objects, &root_realm)?,
        _ => println!("{}", root_realm),
    };
    Ok(())
}
