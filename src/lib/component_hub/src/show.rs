// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    anyhow::{format_err, Context, Result},
    fuchsia_async::TimeoutExt,
    futures::future::{join, join_all, BoxFuture},
    futures::FutureExt,
};

static SPACER: &str = "  ";
static CAPABILITY_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(1);

async fn does_url_match_query(query: &str, hub_dir: &Directory) -> bool {
    let url = hub_dir.read_file("url").await.expect("Could not read component URL");
    url.contains(query)
}

// Given a v2 hub directory, collect components whose component name or URL contains |query| as a
// substring. |name| and |moniker| correspond to the name and moniker of the current component
// respectively. This function is recursive and will find matching CMX and CML components.
pub fn find_components(
    query: String,
    name: String,
    moniker: String,
    hub_dir: Directory,
) -> BoxFuture<'static, Result<Vec<Component>>> {
    async move {
        let mut futures = vec![];
        let children_dir = hub_dir.open_dir("children")?;

        for child_name in children_dir.entries().await? {
            let child_moniker = format!("{}/{}", moniker, child_name);
            let child_hub_dir = children_dir.open_dir(&child_name)?;
            let child_future =
                find_components(query.clone(), child_name, child_moniker, child_hub_dir);
            futures.push(child_future);
        }

        if name == "appmgr" {
            let realm_dir = hub_dir.open_dir("exec/out/hub")?;
            let appmgr_future = find_cmx_realms(query.clone(), moniker.clone(), realm_dir);
            futures.push(appmgr_future);
        }

        let results = join_all(futures).await;
        let mut matching_components = vec![];
        for result in results {
            let mut result = result?;
            matching_components.append(&mut result);
        }

        let should_include = name.contains(&query) || does_url_match_query(&query, &hub_dir).await;
        if should_include {
            let component = Component::parse(moniker, &hub_dir).await?;
            matching_components.push(component);
        }

        Ok(matching_components)
    }
    .boxed()
}

// Given a v1 realm directory, collect components whose URL matches the given |query|.
// |moniker| corresponds to the moniker of the current realm.
fn find_cmx_realms(
    query: String,
    moniker: String,
    hub_dir: Directory,
) -> BoxFuture<'static, Result<Vec<Component>>> {
    async move {
        let c_dir = hub_dir.open_dir("c")?;
        let c_future = find_cmx_components_in_c_dir(query.clone(), moniker.clone(), c_dir);

        let r_dir = hub_dir.open_dir("r")?;
        let r_future = find_cmx_realms_in_r_dir(query, moniker, r_dir);

        let (matching_components_c, matching_components_r) = join(c_future, r_future).await;

        let mut matching_components_c = matching_components_c?;
        let mut matching_components_r = matching_components_r?;

        matching_components_c.append(&mut matching_components_r);

        Ok(matching_components_c)
    }
    .boxed()
}

// Given a v1 component directory, collect components whose URL matches the given |query|.
// |moniker| corresponds to the moniker of the current component.
fn find_cmx_components(
    name: String,
    query: String,
    moniker: String,
    hub_dir: Directory,
) -> BoxFuture<'static, Result<Vec<Component>>> {
    async move {
        let mut matching_components = vec![];

        // Component runners can have a `c` dir with child components
        if hub_dir.exists("c").await? {
            let c_dir = hub_dir.open_dir("c")?;
            let mut child_components =
                find_cmx_components_in_c_dir(query.clone(), moniker.clone(), c_dir).await?;
            matching_components.append(&mut child_components);
        }

        let should_include = name.contains(&query) || does_url_match_query(&query, &hub_dir).await;
        if should_include {
            let component = Component::parse_cmx(moniker, hub_dir).await?;
            matching_components.push(component);
        }

        Ok(matching_components)
    }
    .boxed()
}

async fn find_cmx_components_in_c_dir(
    query: String,
    moniker: String,
    c_dir: Directory,
) -> Result<Vec<Component>> {
    // Get all CMX child components
    let child_component_names = c_dir.entries().await?;
    let mut future_children = vec![];
    for child_component_name in child_component_names {
        let child_moniker = format!("{}/{}", moniker, child_component_name);
        let job_ids_dir = c_dir.open_dir(&child_component_name)?;
        let hub_dirs = open_all_job_ids(job_ids_dir).await?;
        for hub_dir in hub_dirs {
            let future_child = find_cmx_components(
                child_component_name.clone(),
                query.clone(),
                child_moniker.clone(),
                hub_dir,
            );
            future_children.push(future_child);
        }
    }

    let results = join_all(future_children).await;
    let mut flattened_components = vec![];
    for result in results {
        let mut components = result?;
        flattened_components.append(&mut components);
    }
    Ok(flattened_components)
}

async fn find_cmx_realms_in_r_dir(
    query: String,
    moniker: String,
    r_dir: Directory,
) -> Result<Vec<Component>> {
    // Get all CMX child realms
    let mut future_realms = vec![];
    for child_realm_name in r_dir.entries().await? {
        let child_moniker = format!("{}/{}", moniker, child_realm_name);
        let job_ids_dir = r_dir.open_dir(&child_realm_name)?;
        let hub_dirs = open_all_job_ids(job_ids_dir).await?;
        for hub_dir in hub_dirs {
            let future_realm = find_cmx_realms(query.clone(), child_moniker.clone(), hub_dir);
            future_realms.push(future_realm);
        }
    }
    let results = join_all(future_realms).await;
    let mut flattened_components = vec![];
    for result in results {
        let mut components = result?;
        flattened_components.append(&mut components);
    }
    Ok(flattened_components)
}

async fn open_all_job_ids(job_ids_dir: Directory) -> Result<Vec<Directory>> {
    // Recurse on the job_ids
    let mut dirs = vec![];
    for job_id in job_ids_dir.entries().await? {
        let dir = job_ids_dir.open_dir(&job_id)?;
        dirs.push(dir);
    }
    Ok(dirs)
}

// Get all entries in a capabilities directory. If there is a "svc" directory, traverse it and
// collect all protocol names as well.
async fn get_capabilities(capability_dir: Directory) -> Result<Vec<String>> {
    let mut entries = capability_dir.entries().await?;

    for (index, name) in entries.iter().enumerate() {
        if name == "svc" {
            entries.remove(index);
            let svc_dir = capability_dir.open_dir("svc")?;
            let mut svc_entries = svc_dir.entries().await?;
            entries.append(&mut svc_entries);
            break;
        }
    }

    entries.sort_unstable();
    Ok(entries)
}

/// Additional information about components that are using the ELF runner
#[derive(Debug, Eq, PartialEq)]
pub struct ElfRuntime {
    pub job_id: u32,
    pub process_id: Option<u32>,
    pub process_start_time: Option<i64>,
    pub process_start_time_utc_estimate: Option<String>,
}

impl ElfRuntime {
    async fn parse(elf_dir: Directory) -> Result<Self> {
        let (job_id, process_id, process_start_time, process_start_time_utc_estimate) = futures::join!(
            elf_dir.read_file("job_id"),
            elf_dir.read_file("process_id"),
            elf_dir.read_file("process_start_time"),
            elf_dir.read_file("process_start_time_utc_estimate"),
        );

        let job_id = job_id?.parse::<u32>().context("Job ID is not u32")?;

        let process_id = Some(process_id?.parse::<u32>().context("Process ID is not u32")?);

        let process_start_time =
            process_start_time.ok().map(|time_string| time_string.parse::<i64>().ok()).flatten();

        let process_start_time_utc_estimate = process_start_time_utc_estimate.ok();

        Ok(Self { job_id, process_id, process_start_time, process_start_time_utc_estimate })
    }

    async fn parse_cmx(hub_dir: &Directory) -> Result<Self> {
        let (job_id, process_id) =
            futures::join!(hub_dir.read_file("job-id"), hub_dir.read_file("process-id"),);

        let job_id = job_id?.parse::<u32>().context("Job ID is not u32")?;

        let process_id = if hub_dir.exists("process-id").await? {
            Some(process_id?.parse::<u32>().context("Process ID is not u32")?)
        } else {
            None
        };

        Ok(Self {
            job_id,
            process_id,
            process_start_time: None,
            process_start_time_utc_estimate: None,
        })
    }
}

impl std::fmt::Display for ElfRuntime {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        writeln!(f, "Job ID: {}", self.job_id)?;

        if let Some(process_id) = &self.process_id {
            writeln!(f, "Process ID: {}", process_id)?;
        }

        if let Some(ticks) = &self.process_start_time {
            writeln!(f, "Process Start Time (ticks): {}", ticks)?;
        } else {
            writeln!(f, "Process Start Time (ticks): (not available)")?;
        }

        if let Some(utc_estimate) = &self.process_start_time_utc_estimate {
            writeln!(f, "Process Start Time (UTC estimate): {}", utc_estimate)?;
        } else {
            writeln!(f, "Process Start Time (UTC estimate): (not available)")?;
        }

        Ok(())
    }
}

/// Additional information about components that are running
#[derive(Debug, Eq, PartialEq)]
pub struct Execution {
    pub elf_runtime: Option<ElfRuntime>,
    pub merkle_root: Option<String>,
    pub outgoing_capabilities: Option<Vec<String>>,
}

impl Execution {
    async fn parse(exec_dir: Directory) -> Result<Self> {
        let in_dir = exec_dir.open_dir("in")?;

        let merkle_root = if in_dir.exists("pkg").await? {
            let pkg_dir = in_dir.open_dir("pkg")?;
            if pkg_dir.exists("meta").await? {
                pkg_dir.read_file("meta").await.ok()
            } else {
                None
            }
        } else {
            None
        };

        let elf_runtime = if exec_dir.exists("runtime").await? {
            let runtime_dir = exec_dir.open_dir("runtime")?;

            // Some runners may not serve the runtime directory, so attempting to get the entries
            // may fail. This is normal and should be treated as no ELF runtime.
            if let Ok(true) = runtime_dir.exists("elf").await {
                let elf_dir = runtime_dir.open_dir("elf")?;
                Some(ElfRuntime::parse(elf_dir).await?)
            } else {
                None
            }
        } else {
            None
        };

        let outgoing_capabilities = if exec_dir.exists("out").await? {
            let out_dir = exec_dir.open_dir("out")?;
            get_capabilities(out_dir)
                .on_timeout(CAPABILITY_TIMEOUT, || {
                    Err(format_err!("Timeout occurred opening `out` dir"))
                })
                .await
                .ok()
        } else {
            // The directory doesn't exist. This is probably because
            // there is no runtime on the component.
            None
        };

        Ok(Self { elf_runtime, merkle_root, outgoing_capabilities })
    }

    async fn parse_cmx(hub_dir: &Directory) -> Result<Self> {
        let in_dir = hub_dir.open_dir("in")?;

        let merkle_root = if in_dir.exists("pkg").await? {
            let pkg_dir = in_dir.open_dir("pkg")?;
            if pkg_dir.exists("meta").await? {
                pkg_dir.read_file("meta").await.ok()
            } else {
                None
            }
        } else {
            None
        };

        let elf_runtime = Some(ElfRuntime::parse_cmx(hub_dir).await?);

        let outgoing_capabilities = if hub_dir.exists("out").await? {
            let out_dir = hub_dir.open_dir("out")?;
            get_capabilities(out_dir)
                .on_timeout(CAPABILITY_TIMEOUT, || {
                    Err(format_err!("Timeout occurred opening `out` dir"))
                })
                .await
                .ok()
        } else {
            // The directory doesn't exist. This is probably because
            // there is no runtime on the component.
            None
        };

        Ok(Self { elf_runtime, merkle_root, outgoing_capabilities })
    }
}

impl std::fmt::Display for Execution {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if let Some(runtime) = &self.elf_runtime {
            write!(f, "{}", runtime)?;
        }

        if let Some(merkle_root) = &self.merkle_root {
            writeln!(f, "Merkle root: {}", merkle_root)?;
        }

        if let Some(outgoing_capabilities) = &self.outgoing_capabilities {
            writeln!(f, "Outgoing Capabilities ({}):", outgoing_capabilities.len())?;
            for capability in outgoing_capabilities {
                writeln!(f, "{}{}", SPACER, capability)?;
            }
        }

        Ok(())
    }
}

/// Additional information about components that are resolved
pub struct Resolved {
    pub incoming_capabilities: Vec<String>,
    pub exposed_capabilities: Vec<String>,
}

impl Resolved {
    async fn parse(resolved_dir: Directory) -> Result<Self> {
        let incoming_capabilities = {
            let use_dir = resolved_dir.open_dir("use")?;
            get_capabilities(use_dir).await?
        };

        let exposed_capabilities = {
            let expose_dir = resolved_dir.open_dir("expose")?;
            get_capabilities(expose_dir).await?
        };

        Ok(Self { incoming_capabilities, exposed_capabilities })
    }

    async fn parse_cmx(hub_dir: &Directory) -> Result<Self> {
        let incoming_capabilities = {
            let in_dir = hub_dir.open_dir("in")?;
            get_capabilities(in_dir).await?
        };

        Ok(Self { incoming_capabilities, exposed_capabilities: vec![] })
    }
}

impl std::fmt::Display for Resolved {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        writeln!(f, "Incoming Capabilities ({}):", self.incoming_capabilities.len())?;
        for capability in &self.incoming_capabilities {
            writeln!(f, "{}{}", SPACER, capability)?;
        }

        writeln!(f, "Exposed Capabilities ({}):", self.exposed_capabilities.len())?;
        for capability in &self.exposed_capabilities {
            writeln!(f, "{}{}", SPACER, capability)?;
        }
        Ok(())
    }
}

/// Basic information about a component for the `show` command.
pub struct Component {
    pub moniker: String,
    pub url: String,
    pub component_type: String,
    pub execution: Option<Execution>,
    pub resolved: Option<Resolved>,
}

impl Component {
    async fn parse(moniker: String, hub_dir: &Directory) -> Result<Component> {
        let resolved = if hub_dir.exists("resolved").await? {
            let resolved_dir = hub_dir.open_dir("resolved")?;
            Some(Resolved::parse(resolved_dir).await?)
        } else {
            None
        };

        let execution = if hub_dir.exists("exec").await? {
            let exec_dir = hub_dir.open_dir("exec")?;
            Some(Execution::parse(exec_dir).await?)
        } else {
            None
        };

        let (url, component_type) =
            futures::join!(hub_dir.read_file("url"), hub_dir.read_file("component_type"),);

        let url = url?;
        let component_type = format!("CML {} component", component_type?);

        Ok(Component { moniker, url, component_type, execution, resolved })
    }

    async fn parse_cmx(moniker: String, hub_dir: Directory) -> Result<Component> {
        let resolved = Some(Resolved::parse_cmx(&hub_dir).await?);
        let execution = Some(Execution::parse_cmx(&hub_dir).await?);

        let url = hub_dir.read_file("url").await?;
        let component_type = "CMX component".to_string();

        Ok(Component { moniker, url, component_type, execution, resolved })
    }
}

impl std::fmt::Display for Component {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        writeln!(f, "Moniker: {}", self.moniker)?;
        writeln!(f, "URL: {}", self.url)?;
        writeln!(f, "Type: {}", self.component_type)?;

        if let Some(resolved) = &self.resolved {
            writeln!(f, "Component State: Resolved")?;
            write!(f, "{}", resolved)?;
        } else {
            writeln!(f, "Component State: Unresolved")?;
        }

        if let Some(execution) = &self.execution {
            writeln!(f, "Execution State: Running")?;
            write!(f, "{}", execution)?;
        } else {
            writeln!(f, "Execution State: Stopped")?;
        }

        Ok(())
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
    async fn cml_find_by_name() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        //       |- stash
        //          |- children
        //          |- component_type
        //          |- url
        // |- component_type
        // |- url
        fs::create_dir(root.join("children")).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        {
            let stash = root.join("children/stash");
            fs::create_dir(&stash).unwrap();
            fs::create_dir(stash.join("children")).unwrap();
            File::create(stash.join("component_type"))
                .unwrap()
                .write_all("static".as_bytes())
                .unwrap();
            File::create(stash.join("url"))
                .unwrap()
                .write_all("fuchsia-pkg://fuchsia.com/abcd#meta/abcd.cm".as_bytes())
                .unwrap();
        }

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let components =
            find_components("stash".to_string(), ".".to_string(), ".".to_string(), hub_dir)
                .await
                .unwrap();

        assert_eq!(components.len(), 1);
        let component = &components[0];
        assert_eq!(component.moniker, "./stash");
        assert_eq!(component.url, "fuchsia-pkg://fuchsia.com/abcd#meta/abcd.cm");
        assert_eq!(component.component_type, "CML static component");
        assert!(component.resolved.is_none());
        assert!(component.execution.is_none());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn cml_find_by_url() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        //       |- abcd
        //          |- children
        //          |- component_type
        //          |- url
        // |- component_type
        // |- url
        fs::create_dir(root.join("children")).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        {
            let stash = root.join("children/abcd");
            fs::create_dir(&stash).unwrap();
            fs::create_dir(stash.join("children")).unwrap();
            File::create(stash.join("component_type"))
                .unwrap()
                .write_all("static".as_bytes())
                .unwrap();
            File::create(stash.join("url"))
                .unwrap()
                .write_all("fuchsia-pkg://fuchsia.com/stash#meta/stash.cm".as_bytes())
                .unwrap();
        }

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let components =
            find_components("stash".to_string(), ".".to_string(), ".".to_string(), hub_dir)
                .await
                .unwrap();

        assert_eq!(components.len(), 1);
        let component = &components[0];
        assert_eq!(component.moniker, "./abcd");
        assert_eq!(component.url, "fuchsia-pkg://fuchsia.com/stash#meta/stash.cm");
        assert_eq!(component.component_type, "CML static component");
        assert!(component.resolved.is_none());
        assert!(component.execution.is_none());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn nested_cml() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        //       |- abcd
        //          |- children
        //             |- efgh
        //                |- children
        //                |- component_type
        //                |- url
        //          |- component_type
        //          |- url
        // |- component_type
        // |- url
        fs::create_dir(root.join("children")).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        {
            let abcd = root.join("children/abcd");
            fs::create_dir(&abcd).unwrap();
            fs::create_dir(abcd.join("children")).unwrap();
            File::create(abcd.join("component_type"))
                .unwrap()
                .write_all("static".as_bytes())
                .unwrap();
            File::create(abcd.join("url"))
                .unwrap()
                .write_all("fuchsia-pkg://fuchsia.com/abcd#meta/abcd.cm".as_bytes())
                .unwrap();

            {
                let efgh = abcd.join("children/efgh");
                fs::create_dir(&efgh).unwrap();
                fs::create_dir(efgh.join("children")).unwrap();
                File::create(efgh.join("component_type"))
                    .unwrap()
                    .write_all("static".as_bytes())
                    .unwrap();
                File::create(efgh.join("url"))
                    .unwrap()
                    .write_all("fuchsia-pkg://fuchsia.com/efgh#meta/efgh.cm".as_bytes())
                    .unwrap();
            }
        }

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let components =
            find_components("efgh".to_string(), ".".to_string(), ".".to_string(), hub_dir)
                .await
                .unwrap();

        assert_eq!(components.len(), 1);
        let component = &components[0];
        assert_eq!(component.moniker, "./abcd/efgh");
        assert_eq!(component.url, "fuchsia-pkg://fuchsia.com/efgh#meta/efgh.cm");
        assert_eq!(component.component_type, "CML static component");
        assert!(component.resolved.is_none());
        assert!(component.execution.is_none());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn multiple_cml() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        //       |- stash_1
        //          |- children
        //          |- component_type
        //          |- url
        //       |- stash_2
        //          |- children
        //          |- component_type
        //          |- url
        // |- component_type
        // |- url
        fs::create_dir(root.join("children")).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        {
            let stash_1 = root.join("children/stash_1");
            fs::create_dir(&stash_1).unwrap();
            fs::create_dir(stash_1.join("children")).unwrap();
            File::create(stash_1.join("component_type"))
                .unwrap()
                .write_all("static".as_bytes())
                .unwrap();
            File::create(stash_1.join("url"))
                .unwrap()
                .write_all("fuchsia-pkg://fuchsia.com/abcd#meta/abcd.cm".as_bytes())
                .unwrap();
        }

        {
            let stash_2 = root.join("children/stash_2");
            fs::create_dir(&stash_2).unwrap();
            fs::create_dir(stash_2.join("children")).unwrap();
            File::create(stash_2.join("component_type"))
                .unwrap()
                .write_all("static".as_bytes())
                .unwrap();
            File::create(stash_2.join("url"))
                .unwrap()
                .write_all("fuchsia-pkg://fuchsia.com/abcd#meta/abcd.cm".as_bytes())
                .unwrap();
        }

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let components =
            find_components("stash".to_string(), ".".to_string(), ".".to_string(), hub_dir)
                .await
                .unwrap();

        assert_eq!(components.len(), 2);
        let component_1 = &components[0];
        assert_eq!(component_1.moniker, "./stash_1");
        assert_eq!(component_1.url, "fuchsia-pkg://fuchsia.com/abcd#meta/abcd.cm");
        assert_eq!(component_1.component_type, "CML static component");
        assert!(component_1.resolved.is_none());
        assert!(component_1.execution.is_none());

        let component_2 = &components[1];
        assert_eq!(component_2.moniker, "./stash_2");
        assert_eq!(component_2.url, "fuchsia-pkg://fuchsia.com/abcd#meta/abcd.cm");
        assert_eq!(component_2.component_type, "CML static component");
        assert!(component_2.resolved.is_none());
        assert!(component_2.execution.is_none());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn resolved_cml() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        // |- component_type
        // |- url
        // |- resolved
        //    |- use
        //       |- dev
        //    |- expose
        //       |- minfs
        fs::create_dir(root.join("children")).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-pkg://fuchsia.com/stash#meta/stash.cm".as_bytes())
            .unwrap();
        fs::create_dir_all(root.join("resolved/use/dev")).unwrap();
        fs::create_dir_all(root.join("resolved/expose/minfs")).unwrap();

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let components =
            find_components("stash".to_string(), ".".to_string(), ".".to_string(), hub_dir)
                .await
                .unwrap();

        assert_eq!(components.len(), 1);
        let component = &components[0];
        assert_eq!(component.url, "fuchsia-pkg://fuchsia.com/stash#meta/stash.cm");

        assert!(component.resolved.is_some());
        let resolved = component.resolved.as_ref().unwrap();

        let incoming_capabilities = &resolved.incoming_capabilities;
        assert_eq!(incoming_capabilities.len(), 1);

        let incoming_capability = &incoming_capabilities[0];
        assert_eq!(incoming_capability, "dev");

        let exposed_capabilities = &resolved.exposed_capabilities;
        assert_eq!(exposed_capabilities.len(), 1);

        let exposed_capability = &exposed_capabilities[0];
        assert_eq!(exposed_capability, "minfs");

        assert!(component.execution.is_none());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn full_execution_cml() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        // |- component_type
        // |- url
        // |- exec
        //    |- in
        //       |- pkg
        //          |- meta
        //    |- out
        //       |- minfs
        //    |- runtime
        //       |- elf
        //          |- job_id
        //          |- process_id
        //          |- process_start_time
        //          |- process_start_time_utc_estimate
        fs::create_dir(root.join("children")).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-pkg://fuchsia.com/stash#meta/stash.cm".as_bytes())
            .unwrap();
        fs::create_dir_all(root.join("exec/in/pkg")).unwrap();
        fs::create_dir_all(root.join("exec/out/minfs")).unwrap();
        fs::create_dir_all(root.join("exec/runtime/elf")).unwrap();

        File::create(root.join("exec/in/pkg/meta")).unwrap().write_all("1234".as_bytes()).unwrap();

        File::create(root.join("exec/runtime/elf/job_id"))
            .unwrap()
            .write_all("5454".as_bytes())
            .unwrap();

        File::create(root.join("exec/runtime/elf/process_id"))
            .unwrap()
            .write_all("9898".as_bytes())
            .unwrap();

        File::create(root.join("exec/runtime/elf/process_start_time"))
            .unwrap()
            .write_all("101010101010".as_bytes())
            .unwrap();

        File::create(root.join("exec/runtime/elf/process_start_time_utc_estimate"))
            .unwrap()
            .write_all("Mon 12 Jul 2021 03:53:33 PM UTC".as_bytes())
            .unwrap();

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let components =
            find_components("stash".to_string(), ".".to_string(), ".".to_string(), hub_dir)
                .await
                .unwrap();

        assert_eq!(components.len(), 1);
        let component = &components[0];
        assert_eq!(component.url, "fuchsia-pkg://fuchsia.com/stash#meta/stash.cm");

        assert!(component.execution.is_some());
        let execution = component.execution.as_ref().unwrap();

        assert!(execution.elf_runtime.is_some());
        let elf_runtime = execution.elf_runtime.as_ref().unwrap();
        assert_eq!(elf_runtime.job_id, 5454);
        let process_id = elf_runtime.process_id.unwrap();
        assert_eq!(process_id, 9898);

        assert!(elf_runtime.process_start_time.is_some());
        let process_start_time = elf_runtime.process_start_time.unwrap();
        assert_eq!(process_start_time, 101010101010);

        assert!(elf_runtime.process_start_time_utc_estimate.is_some());
        let process_start_time_utc_estimate =
            elf_runtime.process_start_time_utc_estimate.as_ref().unwrap();
        assert_eq!(process_start_time_utc_estimate, "Mon 12 Jul 2021 03:53:33 PM UTC");

        assert!(execution.merkle_root.is_some());
        let merkle_root = execution.merkle_root.as_ref().unwrap();
        assert_eq!(merkle_root, "1234");

        assert!(execution.outgoing_capabilities.is_some());
        let outgoing_capabilities = execution.outgoing_capabilities.as_ref().unwrap();
        assert_eq!(outgoing_capabilities.len(), 1);

        let outgoing_capability = &outgoing_capabilities[0];
        assert_eq!(outgoing_capability, "minfs");

        assert!(component.resolved.is_none());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn barebones_execution_cml() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        // |- component_type
        // |- url
        // |- exec
        //    |- in
        //    |- out
        fs::create_dir(root.join("children")).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-pkg://fuchsia.com/stash#meta/stash.cm".as_bytes())
            .unwrap();
        fs::create_dir_all(root.join("exec/in")).unwrap();
        fs::create_dir_all(root.join("exec/out")).unwrap();

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let components =
            find_components("stash".to_string(), ".".to_string(), ".".to_string(), hub_dir)
                .await
                .unwrap();

        assert_eq!(components.len(), 1);
        let component = &components[0];
        assert_eq!(component.url, "fuchsia-pkg://fuchsia.com/stash#meta/stash.cm");

        assert!(component.execution.is_some());
        let execution = component.execution.as_ref().unwrap();

        assert!(execution.elf_runtime.is_none());
        assert!(execution.merkle_root.is_none());

        assert!(execution.outgoing_capabilities.is_some());
        let outgoing_capabilities = execution.outgoing_capabilities.as_ref().unwrap();
        assert!(outgoing_capabilities.is_empty());

        assert!(component.resolved.is_none());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn cmx() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        //       |- appmgr
        //          |- children
        //          |- component_type
        //          |- url
        //          |- exec
        //             |- in
        //             |- out
        //                |- hub
        //                   |- r
        //                   |- c
        //                      |- sshd.cmx
        //                         |- 9898
        //                            |- job-id
        //                            |- process-id
        //                            |- url
        //                            |- in
        //                               |- pkg
        //                                  |- meta
        //                            |- out
        //                               |- dev
        // |- component_type
        // |- url
        fs::create_dir(root.join("children")).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        {
            let appmgr = root.join("children/appmgr");
            fs::create_dir(&appmgr).unwrap();
            fs::create_dir(appmgr.join("children")).unwrap();
            File::create(appmgr.join("component_type"))
                .unwrap()
                .write_all("static".as_bytes())
                .unwrap();
            File::create(appmgr.join("url"))
                .unwrap()
                .write_all("fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm".as_bytes())
                .unwrap();

            fs::create_dir_all(appmgr.join("exec/in")).unwrap();
            fs::create_dir_all(appmgr.join("exec/out/hub/r")).unwrap();

            {
                let sshd = appmgr.join("exec/out/hub/c/sshd.cmx/9898");
                fs::create_dir_all(&sshd).unwrap();
                fs::create_dir_all(sshd.join("in/pkg")).unwrap();
                fs::create_dir_all(sshd.join("out/dev")).unwrap();

                File::create(sshd.join("url"))
                    .unwrap()
                    .write_all("fuchsia-pkg://fuchsia.com/sshd#meta/sshd.cmx".as_bytes())
                    .unwrap();
                File::create(sshd.join("in/pkg/meta"))
                    .unwrap()
                    .write_all("1234".as_bytes())
                    .unwrap();
                File::create(sshd.join("job-id")).unwrap().write_all("5454".as_bytes()).unwrap();
                File::create(sshd.join("process-id"))
                    .unwrap()
                    .write_all("9898".as_bytes())
                    .unwrap();
            }
        }

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let components =
            find_components("sshd".to_string(), ".".to_string(), ".".to_string(), hub_dir)
                .await
                .unwrap();

        assert_eq!(components.len(), 1);
        let component = &components[0];
        assert_eq!(component.moniker, "./appmgr/sshd.cmx");
        assert_eq!(component.url, "fuchsia-pkg://fuchsia.com/sshd#meta/sshd.cmx");
        assert_eq!(component.component_type, "CMX component");

        assert!(component.resolved.is_some());
        let resolved = component.resolved.as_ref().unwrap();

        let incoming_capabilities = &resolved.incoming_capabilities;
        assert_eq!(incoming_capabilities.len(), 1);

        let incoming_capability = &incoming_capabilities[0];
        assert_eq!(incoming_capability, "pkg");

        let exposed_capabilities = &resolved.exposed_capabilities;
        assert!(exposed_capabilities.is_empty());

        assert!(component.execution.is_some());
        let execution = component.execution.as_ref().unwrap();

        assert!(execution.elf_runtime.is_some());
        let elf_runtime = execution.elf_runtime.as_ref().unwrap();
        assert_eq!(elf_runtime.job_id, 5454);
        let process_id = elf_runtime.process_id.unwrap();
        assert_eq!(process_id, 9898);
        assert!(elf_runtime.process_start_time.is_none());
        assert!(elf_runtime.process_start_time_utc_estimate.is_none());

        assert!(execution.merkle_root.is_some());
        let merkle_root = execution.merkle_root.as_ref().unwrap();
        assert_eq!(merkle_root, "1234");

        assert!(execution.outgoing_capabilities.is_some());
        let outgoing_capabilities = execution.outgoing_capabilities.as_ref().unwrap();
        assert_eq!(outgoing_capabilities.len(), 1);

        let outgoing_capability = &outgoing_capabilities[0];
        assert_eq!(outgoing_capability, "dev");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn multiple_cmx_different_process_ids() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        //       |- appmgr
        //          |- children
        //          |- component_type
        //          |- url
        //          |- exec
        //             |- in
        //             |- out
        //                |- hub
        //                   |- r
        //                   |- c
        //                      |- sshd.cmx
        //                         |- 8787
        //                            |- job-id
        //                            |- process-id
        //                            |- url
        //                            |- in
        //                            |- out
        //                         |- 9898
        //                            |- job-id
        //                            |- process-id
        //                            |- url
        //                            |- in
        //                            |- out
        // |- component_type
        // |- url
        fs::create_dir(root.join("children")).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        {
            let appmgr = root.join("children/appmgr");
            fs::create_dir(&appmgr).unwrap();
            fs::create_dir(appmgr.join("children")).unwrap();
            File::create(appmgr.join("component_type"))
                .unwrap()
                .write_all("static".as_bytes())
                .unwrap();
            File::create(appmgr.join("url"))
                .unwrap()
                .write_all("fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm".as_bytes())
                .unwrap();

            fs::create_dir_all(appmgr.join("exec/in")).unwrap();
            fs::create_dir_all(appmgr.join("exec/out/hub/r")).unwrap();

            {
                let sshd_1 = appmgr.join("exec/out/hub/c/sshd.cmx/9898");
                fs::create_dir_all(&sshd_1).unwrap();
                fs::create_dir(sshd_1.join("in")).unwrap();
                fs::create_dir(sshd_1.join("out")).unwrap();

                File::create(sshd_1.join("url"))
                    .unwrap()
                    .write_all("fuchsia-pkg://fuchsia.com/sshd#meta/sshd.cmx".as_bytes())
                    .unwrap();
                File::create(sshd_1.join("job-id")).unwrap().write_all("5454".as_bytes()).unwrap();
                File::create(sshd_1.join("process-id"))
                    .unwrap()
                    .write_all("8787".as_bytes())
                    .unwrap();
            }

            {
                let sshd_2 = appmgr.join("exec/out/hub/c/sshd.cmx/8787");
                fs::create_dir_all(&sshd_2).unwrap();
                fs::create_dir(sshd_2.join("in")).unwrap();
                fs::create_dir(sshd_2.join("out")).unwrap();

                File::create(sshd_2.join("url"))
                    .unwrap()
                    .write_all("fuchsia-pkg://fuchsia.com/sshd#meta/sshd.cmx".as_bytes())
                    .unwrap();
                File::create(sshd_2.join("job-id")).unwrap().write_all("5454".as_bytes()).unwrap();
                File::create(sshd_2.join("process-id"))
                    .unwrap()
                    .write_all("9898".as_bytes())
                    .unwrap();
            }
        }

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let components =
            find_components("sshd".to_string(), ".".to_string(), ".".to_string(), hub_dir)
                .await
                .unwrap();

        assert_eq!(components.len(), 2);

        {
            let component = &components[0];
            assert_eq!(component.moniker, "./appmgr/sshd.cmx");
            assert_eq!(component.url, "fuchsia-pkg://fuchsia.com/sshd#meta/sshd.cmx");
            assert_eq!(component.component_type, "CMX component");

            assert!(component.execution.is_some());
            let execution = component.execution.as_ref().unwrap();

            assert!(execution.elf_runtime.is_some());
            let elf_runtime = execution.elf_runtime.as_ref().unwrap();
            assert_eq!(elf_runtime.job_id, 5454);
            let process_id = elf_runtime.process_id.unwrap();
            assert_eq!(process_id, 9898);
        }

        {
            let component = &components[1];
            assert_eq!(component.moniker, "./appmgr/sshd.cmx");
            assert_eq!(component.url, "fuchsia-pkg://fuchsia.com/sshd#meta/sshd.cmx");
            assert_eq!(component.component_type, "CMX component");

            assert!(component.execution.is_some());
            let execution = component.execution.as_ref().unwrap();

            assert!(execution.elf_runtime.is_some());
            let elf_runtime = execution.elf_runtime.as_ref().unwrap();
            assert_eq!(elf_runtime.job_id, 5454);
            let process_id = elf_runtime.process_id.unwrap();
            assert_eq!(process_id, 8787);
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn multiple_cmx_different_realms() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        //       |- appmgr
        //          |- children
        //          |- component_type
        //          |- url
        //          |- exec
        //             |- in
        //             |- out
        //                |- hub
        //                   |- r
        //                      |- sys
        //                         |- 1765
        //                            |- r
        //                            |- c
        //                               |- sshd.cmx
        //                                  |- 1765
        //                                     |- job-id
        //                                     |- url
        //                                     |- in
        //                                     |- out
        //                   |- c
        //                      |- sshd.cmx
        //                         |- 5454
        //                            |- job-id
        //                            |- process-id
        //                            |- url
        //                            |- in
        //                            |- out
        // |- component_type
        // |- url
        fs::create_dir(root.join("children")).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        {
            let appmgr = root.join("children/appmgr");
            fs::create_dir(&appmgr).unwrap();
            fs::create_dir(appmgr.join("children")).unwrap();
            File::create(appmgr.join("component_type"))
                .unwrap()
                .write_all("static".as_bytes())
                .unwrap();
            File::create(appmgr.join("url"))
                .unwrap()
                .write_all("fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm".as_bytes())
                .unwrap();

            fs::create_dir_all(appmgr.join("exec/in")).unwrap();
            fs::create_dir_all(appmgr.join("exec/out/hub/r")).unwrap();
            fs::create_dir_all(appmgr.join("exec/out/hub/r/sys/1765/r")).unwrap();

            {
                let sshd_1 = appmgr.join("exec/out/hub/c/sshd.cmx/5454");
                fs::create_dir_all(&sshd_1).unwrap();
                fs::create_dir(sshd_1.join("in")).unwrap();
                fs::create_dir(sshd_1.join("out")).unwrap();

                File::create(sshd_1.join("url"))
                    .unwrap()
                    .write_all("fuchsia-pkg://fuchsia.com/sshd#meta/sshd.cmx".as_bytes())
                    .unwrap();
                File::create(sshd_1.join("job-id")).unwrap().write_all("5454".as_bytes()).unwrap();
                File::create(sshd_1.join("process-id"))
                    .unwrap()
                    .write_all("8787".as_bytes())
                    .unwrap();
            }

            {
                let sshd_2 = appmgr.join("exec/out/hub/r/sys/1765/c/sshd.cmx/1765");
                fs::create_dir_all(&sshd_2).unwrap();
                fs::create_dir(sshd_2.join("in")).unwrap();
                fs::create_dir(sshd_2.join("out")).unwrap();

                File::create(sshd_2.join("url"))
                    .unwrap()
                    .write_all("fuchsia-pkg://fuchsia.com/sshd#meta/sshd.cmx".as_bytes())
                    .unwrap();
                File::create(sshd_2.join("job-id")).unwrap().write_all("1765".as_bytes()).unwrap();
            }
        }

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let components =
            find_components("sshd".to_string(), ".".to_string(), ".".to_string(), hub_dir)
                .await
                .unwrap();

        assert_eq!(components.len(), 2);

        {
            let component = &components[0];
            assert_eq!(component.moniker, "./appmgr/sshd.cmx");
            assert_eq!(component.url, "fuchsia-pkg://fuchsia.com/sshd#meta/sshd.cmx");
            assert_eq!(component.component_type, "CMX component");

            assert!(component.execution.is_some());
            let execution = component.execution.as_ref().unwrap();

            assert!(execution.elf_runtime.is_some());
            let elf_runtime = execution.elf_runtime.as_ref().unwrap();
            assert_eq!(elf_runtime.job_id, 5454);
            let process_id = elf_runtime.process_id.unwrap();
            assert_eq!(process_id, 8787);
        }

        {
            let component = &components[1];
            assert_eq!(component.moniker, "./appmgr/sys/sshd.cmx");
            assert_eq!(component.url, "fuchsia-pkg://fuchsia.com/sshd#meta/sshd.cmx");
            assert_eq!(component.component_type, "CMX component");

            assert!(component.execution.is_some());
            let execution = component.execution.as_ref().unwrap();

            assert!(execution.elf_runtime.is_some());
            let elf_runtime = execution.elf_runtime.as_ref().unwrap();
            assert_eq!(elf_runtime.job_id, 1765);
            assert!(elf_runtime.process_id.is_none());
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn runner_cmx() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        //       |- appmgr
        //          |- children
        //          |- component_type
        //          |- url
        //          |- exec
        //             |- in
        //             |- out
        //                |- hub
        //                   |- c
        //                      |- sshd.cmx
        //                         |- 5454
        //                            |- job-id
        //                            |- process-id
        //                            |- url
        //                            |- in
        //                            |- out
        //                            |- c
        //                               |- foo.cmx
        //                                  |- 1234
        //                                     |- job-id
        //                                     |- process-id
        //                                     |- url
        //                                     |- in
        //                                     |- out
        // |- component_type
        // |- url
        fs::create_dir(root.join("children")).unwrap();
        File::create(root.join("component_type")).unwrap().write_all("static".as_bytes()).unwrap();
        File::create(root.join("url"))
            .unwrap()
            .write_all("fuchsia-boot:///#meta/root.cm".as_bytes())
            .unwrap();

        {
            let appmgr = root.join("children/appmgr");
            fs::create_dir(&appmgr).unwrap();
            fs::create_dir(appmgr.join("children")).unwrap();
            File::create(appmgr.join("component_type"))
                .unwrap()
                .write_all("static".as_bytes())
                .unwrap();
            File::create(appmgr.join("url"))
                .unwrap()
                .write_all("fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm".as_bytes())
                .unwrap();

            fs::create_dir_all(appmgr.join("exec/in")).unwrap();
            fs::create_dir_all(appmgr.join("exec/out/hub/r")).unwrap();

            {
                let sshd = appmgr.join("exec/out/hub/c/sshd.cmx/5454");
                fs::create_dir_all(&sshd).unwrap();
                fs::create_dir(sshd.join("in")).unwrap();
                fs::create_dir(sshd.join("out")).unwrap();

                File::create(sshd.join("url"))
                    .unwrap()
                    .write_all("fuchsia-pkg://fuchsia.com/sshd#meta/sshd.cmx".as_bytes())
                    .unwrap();
                File::create(sshd.join("job-id")).unwrap().write_all("5454".as_bytes()).unwrap();
                File::create(sshd.join("process-id"))
                    .unwrap()
                    .write_all("8787".as_bytes())
                    .unwrap();
                {
                    let foo = sshd.join("c/foo.cmx/1234");
                    fs::create_dir_all(&foo).unwrap();
                    fs::create_dir(foo.join("in")).unwrap();
                    fs::create_dir(foo.join("out")).unwrap();

                    File::create(foo.join("url"))
                        .unwrap()
                        .write_all("fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx".as_bytes())
                        .unwrap();
                    File::create(foo.join("job-id")).unwrap().write_all("1234".as_bytes()).unwrap();
                    File::create(foo.join("process-id"))
                        .unwrap()
                        .write_all("4536".as_bytes())
                        .unwrap();
                }
            }
        }

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let components =
            find_components("foo.cmx".to_string(), ".".to_string(), ".".to_string(), hub_dir)
                .await
                .unwrap();

        assert_eq!(components.len(), 1);

        {
            let component = &components[0];
            assert_eq!(component.moniker, "./appmgr/sshd.cmx/foo.cmx");
            assert_eq!(component.url, "fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx");
            assert_eq!(component.component_type, "CMX component");

            assert!(component.execution.is_some());
            let execution = component.execution.as_ref().unwrap();

            assert!(execution.elf_runtime.is_some());
            let elf_runtime = execution.elf_runtime.as_ref().unwrap();
            assert_eq!(elf_runtime.job_id, 1234);
            let process_id = elf_runtime.process_id.unwrap();
            assert_eq!(process_id, 4536);
        }
    }
}
