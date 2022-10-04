// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    anyhow::{format_err, Result},
    fidl_fuchsia_sys2 as fsys,
    futures::future::{join, join_all, BoxFuture, FutureExt},
    moniker::{
        AbsoluteMoniker, AbsoluteMonikerBase, ChildMoniker, ChildMonikerBase, RelativeMoniker,
        RelativeMonikerBase,
    },
    std::collections::HashSet,
    std::str::FromStr,
};

/// Reading from the v1/CMX hub is flaky while components are being added/removed.
/// Attempt to get CMX instances several times before calling it a failure.
const CMX_HUB_RETRY_ATTEMPTS: u64 = 10;

/// Filters that can be applied when listing components
#[derive(Debug, PartialEq)]
pub enum ListFilter {
    CMX,
    CML,
    Running,
    Stopped,

    /// Filters components that are an ancestor of the component with the given name.
    /// Includes the named component.
    Ancestor(String),
    /// Filters components that are a descendant of the component with the given name.
    /// Includes the named component.
    Descendant(String),
    /// Filters components that are a relative (either an ancestor or a descendant) of the
    /// component with the given name. Includes the named component.
    Relative(String),
    None,
}

impl FromStr for ListFilter {
    type Err = &'static str;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "cmx" => Ok(ListFilter::CMX),
            "cml" => Ok(ListFilter::CML),
            "running" => Ok(ListFilter::Running),
            "stopped" => Ok(ListFilter::Stopped),
            filter => match filter.split_once(":") {
                Some((function, arg)) => match function {
                    "ancestor" | "ancestors" => Ok(ListFilter::Ancestor(arg.to_string())),
                    "descendant" | "descendants" => Ok(ListFilter::Descendant(arg.to_string())),
                    "relative" | "relatives" => Ok(ListFilter::Relative(arg.to_string())),
                    _ => Err("unknown function for list filter."),
                },
                None => Err("list filter should be 'cmx', 'cml', 'running', 'stopped', 'ancestors:<component_name>', 'descendants:<component_name>', or 'relatives:<component_name>'."),
            },
        }
    }
}

#[derive(PartialEq, Debug)]
pub enum InstanceState {
    Unresolved,
    Resolved,
    Started,
}

impl From<fsys::InstanceState> for InstanceState {
    fn from(state: fsys::InstanceState) -> Self {
        match state {
            fsys::InstanceState::Unresolved => InstanceState::Unresolved,
            fsys::InstanceState::Resolved => InstanceState::Resolved,
            fsys::InstanceState::Started => InstanceState::Started,
        }
    }
}

/// Basic information about a component for the `list` command.
pub struct Instance {
    /// Moniker of the component.
    pub moniker: AbsoluteMoniker,

    /// URL of the component.
    pub url: Option<String>,

    /// Unique identifier of component.
    pub instance_id: Option<String>,

    /// True if instance is CMX component/realm.
    // TODO(https://fxbug.dev/102390): Remove this when CMX is deprecated.
    pub is_cmx: bool,

    /// Current state of instance.
    pub state: InstanceState,

    /// Points to the Hub Directory of this component.
    /// Value is present only for CMX components.
    // TODO(https://fxbug.dev/102390): Remove this when CMX is deprecated.
    pub hub_dir: Option<Directory>,
}

/// Uses the fuchsia.sys2.RealmExplorer and fuchsia.sys2.RealmQuery protocols to retrieve a list of
/// all CMX and CML components in the system that match the given filter.
pub async fn get_all_instances(
    explorer: &fsys::RealmExplorerProxy,
    query: &fsys::RealmQueryProxy,
    filter: Option<ListFilter>,
) -> Result<Vec<Instance>> {
    let instances = match filter {
        Some(ListFilter::CML) => get_all_cml_instances(explorer).await?,
        Some(ListFilter::CMX) => get_all_cmx_instances(query).await?,
        _ => {
            // Get both CMX and CML instances.
            let instances_future = get_all_cml_instances(explorer);
            let cmx_future = get_all_cmx_instances(query);

            let (instances, cmx) = join(instances_future, cmx_future).await;
            let mut instances = instances?;
            let mut cmx = cmx?;

            instances.append(&mut cmx);
            instances
        }
    };

    let mut instances = match filter {
        Some(ListFilter::Running) => {
            instances.into_iter().filter(|i| i.state == InstanceState::Started).collect()
        }
        Some(ListFilter::Stopped) => {
            instances.into_iter().filter(|i| i.state != InstanceState::Started).collect()
        }
        Some(ListFilter::Ancestor(m)) => filter_ancestors(instances, m),
        Some(ListFilter::Descendant(m)) => filter_descendants(instances, m),
        Some(ListFilter::Relative(m)) => filter_relatives(instances, m),
        _ => instances,
    };

    instances.sort_by_key(|c| c.moniker.to_string());

    Ok(instances)
}

fn filter_ancestors(instances: Vec<Instance>, child_str: String) -> Vec<Instance> {
    let mut ancestors = HashSet::new();

    // Find monikers with this child as the leaf.
    for instance in &instances {
        if let Some(child) = instance.moniker.leaf() {
            if child.as_str() == &child_str {
                // Add this moniker to ancestor list.
                let mut cur_moniker = instance.moniker.clone();
                ancestors.insert(cur_moniker.clone());

                // Loop over parents of this moniker and add them to ancestor list.
                while let Some(parent) = cur_moniker.parent() {
                    ancestors.insert(parent.clone());
                    cur_moniker = parent;
                }
            }
        }
    }

    instances.into_iter().filter(|i| ancestors.contains(&i.moniker)).collect()
}

fn filter_descendants(instances: Vec<Instance>, child_str: String) -> Vec<Instance> {
    let mut descendants = HashSet::new();

    // Find monikers with this child as the leaf.
    for instance in &instances {
        if let Some(child) = instance.moniker.leaf() {
            if child.as_str() == &child_str {
                // Get all descendants of this moniker.
                for possible_child_instance in &instances {
                    if instance.moniker.contains_in_realm(&possible_child_instance.moniker) {
                        descendants.insert(possible_child_instance.moniker.clone());
                    }
                }
            }
        }
    }

    instances.into_iter().filter(|i| descendants.contains(&i.moniker)).collect()
}

fn filter_relatives(instances: Vec<Instance>, child_str: String) -> Vec<Instance> {
    let mut relatives = HashSet::new();

    // Find monikers with this child as the leaf.
    for instance in &instances {
        if let Some(child) = instance.moniker.leaf() {
            if child.as_str() == &child_str {
                // Loop over parents of this moniker and add them to relatives list.
                let mut cur_moniker = instance.moniker.clone();
                while let Some(parent) = cur_moniker.parent() {
                    relatives.insert(parent.clone());
                    cur_moniker = parent;
                }

                // Get all descendants of this moniker and add them to relatives list.
                for possible_child_instance in &instances {
                    if instance.moniker.contains_in_realm(&possible_child_instance.moniker) {
                        relatives.insert(possible_child_instance.moniker.clone());
                    }
                }
            }
        }
    }

    instances.into_iter().filter(|i| relatives.contains(&i.moniker)).collect()
}

pub async fn get_all_cml_instances(explorer: &fsys::RealmExplorerProxy) -> Result<Vec<Instance>> {
    let iterator = explorer
        .get_all_instance_infos()
        .await?
        .map_err(|e| format_err!("could not get instance infos from realm explorer: {:?}", e))?;
    let iterator = iterator.into_proxy()?;
    let mut instance_infos = vec![];

    loop {
        let mut batch = iterator.next().await?;
        if batch.is_empty() {
            break;
        }
        instance_infos.append(&mut batch);
    }

    let mut instances = vec![];

    for info in instance_infos {
        let moniker = RelativeMoniker::parse_str(&info.moniker)?;
        let moniker = AbsoluteMoniker::root().descendant(&moniker);
        let instance = Instance {
            moniker,
            url: Some(info.url),
            instance_id: info.instance_id,
            is_cmx: false,
            state: info.state.into(),
            hub_dir: None,
        };
        instances.push(instance);
    }

    Ok(instances)
}

pub async fn get_all_cmx_instances(query: &fsys::RealmQueryProxy) -> Result<Vec<Instance>> {
    // Reading from the v1/CMX hub is flaky while components are being added/removed.
    // Attempt to get CMX instances several times before calling it a failure.
    let mut attempt = 1;
    loop {
        match get_all_cmx_instances_internal(query).await {
            Ok(instances) => break Ok(instances),
            Err(e) => {
                if attempt == CMX_HUB_RETRY_ATTEMPTS {
                    break Err(format_err!(
                        "Maximum attempts reached trying to parse CMX realm.\nLast Error: {}",
                        e
                    ));
                }
                attempt += 1;
            }
        }
    }
}

async fn get_all_cmx_instances_internal(query: &fsys::RealmQueryProxy) -> Result<Vec<Instance>> {
    let moniker = AbsoluteMoniker::parse_str("/core/appmgr")?;

    if let Ok((_, resolved)) = query.get_instance_info("./core/appmgr").await? {
        if let Some(resolved) = resolved {
            if let Some(execution) = resolved.execution {
                let out_dir = execution.out_dir.ok_or_else(|| {
                    format_err!("Outgoing directory is not available from appmgr")
                })?;

                let out_dir = out_dir.into_proxy()?;
                let out_dir = Directory::from_proxy(out_dir);
                let root_realm_dir = out_dir.open_dir_readable("hub")?;

                let mut instances = parse_cmx_realm(moniker, root_realm_dir).await?;

                // Remove the /core/appmgr CMX root realm instance because that already exists as
                // a CML component.
                instances.pop();

                return Ok(instances);
            }
        }
    }

    // Return an empty list if appmgr does not exist or is not started
    Ok(vec![])
}

fn parse_cmx_realm(
    moniker: AbsoluteMoniker,
    realm_dir: Directory,
) -> BoxFuture<'static, Result<Vec<Instance>>> {
    async move {
        let children_dir = realm_dir.open_dir_readable("c")?;
        let realms_dir = realm_dir.open_dir_readable("r")?;

        let future_children = parse_cmx_components_in_c_dir(children_dir, moniker.clone());
        let future_realms = parse_cmx_realms_in_r_dir(realms_dir, moniker.clone());

        let (children, realms) = join(future_children, future_realms).await;
        let mut children = children?;
        let mut realms = realms?;

        children.append(&mut realms);

        // Add this realm to the results.
        children.push(Instance {
            moniker,
            url: None, // CMX realms don't have a URL.
            instance_id: None,
            is_cmx: true,
            state: InstanceState::Started,
            hub_dir: None,
        });

        Ok(children)
    }
    .boxed()
}

fn parse_cmx_component(
    moniker: AbsoluteMoniker,
    dir: Directory,
) -> BoxFuture<'static, Result<Vec<Instance>>> {
    async move {
        // Runner CMX components may have child components.
        let url = dir.read_file("url").await?;

        let mut instances = if dir.exists("c").await? {
            let children_dir = dir.open_dir_readable("c")?;
            parse_cmx_components_in_c_dir(children_dir, moniker.clone()).await?
        } else {
            vec![]
        };

        instances.push(Instance {
            moniker,
            url: Some(url),
            instance_id: None,
            is_cmx: true,
            state: InstanceState::Started, // CMX components are always running.
            hub_dir: Some(dir.clone()?),
        });

        Ok(instances)
    }
    .boxed()
}

async fn parse_cmx_components_in_c_dir(
    children_dir: Directory,
    moniker: AbsoluteMoniker,
) -> Result<Vec<Instance>> {
    let child_component_names = children_dir.entry_names().await?;
    let mut future_children = vec![];
    for child_component_name in child_component_names {
        let child_moniker = ChildMoniker::parse(&child_component_name)?;
        let child_moniker = moniker.child(child_moniker);
        let job_ids_dir = children_dir.open_dir_readable(&child_component_name)?;
        let child_dirs = open_all_job_ids(job_ids_dir).await?;
        for child_dir in child_dirs {
            let future_child = parse_cmx_component(child_moniker.clone(), child_dir);
            future_children.push(future_child);
        }
    }

    let instances: Vec<Result<Vec<Instance>>> = join_all(future_children).await;
    let instances: Result<Vec<Vec<Instance>>> = instances.into_iter().collect();
    let instances: Vec<Instance> = instances?.into_iter().flatten().collect();

    Ok(instances)
}

async fn parse_cmx_realms_in_r_dir(
    realms_dir: Directory,
    moniker: AbsoluteMoniker,
) -> Result<Vec<Instance>> {
    let mut future_realms = vec![];
    for child_realm_name in realms_dir.entry_names().await? {
        let child_moniker = ChildMoniker::parse(&child_realm_name)?;
        let child_moniker = moniker.child(child_moniker);
        let job_ids_dir = realms_dir.open_dir_readable(&child_realm_name)?;
        let child_realm_dirs = open_all_job_ids(job_ids_dir).await?;
        for child_realm_dir in child_realm_dirs {
            let future_realm = parse_cmx_realm(child_moniker.clone(), child_realm_dir);
            future_realms.push(future_realm);
        }
    }

    let instances: Vec<Result<Vec<Instance>>> = join_all(future_realms).await;
    let instances: Result<Vec<Vec<Instance>>> = instances.into_iter().collect();
    let instances: Vec<Instance> = instances?.into_iter().flatten().collect();

    Ok(instances)
}

async fn open_all_job_ids(job_ids_dir: Directory) -> Result<Vec<Directory>> {
    let dirs = job_ids_dir
        .entry_names()
        .await?
        .into_iter()
        .map(|job_id| job_ids_dir.open_dir_readable(&job_id))
        .collect::<Result<Vec<Directory>>>()?;
    Ok(dirs)
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::*;
    use fidl_fuchsia_io as fio;
    use fuchsia_async::Task;
    use futures::StreamExt;
    use std::collections::HashMap;
    use std::fs;
    use tempfile::TempDir;

    pub fn serve_realm_explorer(instances: Vec<fsys::InstanceInfo>) -> fsys::RealmExplorerProxy {
        let (client, mut stream) = create_proxy_and_stream::<fsys::RealmExplorerMarker>().unwrap();
        Task::spawn(async move {
            loop {
                let fsys::RealmExplorerRequest::GetAllInstanceInfos { responder } =
                    stream.next().await.unwrap().unwrap();
                let iterator = serve_instance_iterator(instances.clone());
                responder.send(&mut Ok(iterator)).unwrap();
            }
        })
        .detach();
        client
    }

    pub fn serve_instance_iterator(
        instances: Vec<fsys::InstanceInfo>,
    ) -> ClientEnd<fsys::InstanceInfoIteratorMarker> {
        let (client, mut stream) =
            create_request_stream::<fsys::InstanceInfoIteratorMarker>().unwrap();
        Task::spawn(async move {
            let fsys::InstanceInfoIteratorRequest::Next { responder } =
                stream.next().await.unwrap().unwrap();
            responder.send(&mut instances.clone().iter_mut()).unwrap();
            let fsys::InstanceInfoIteratorRequest::Next { responder } =
                stream.next().await.unwrap().unwrap();
            responder.send(&mut std::iter::empty()).unwrap();
        })
        .detach();
        client
    }

    pub fn serve_realm_query(
        mut instances: HashMap<String, (fsys::InstanceInfo, Option<Box<fsys::ResolvedState>>)>,
    ) -> fsys::RealmQueryProxy {
        let (client, mut stream) = create_proxy_and_stream::<fsys::RealmQueryMarker>().unwrap();
        Task::spawn(async move {
            loop {
                let (moniker, responder) = match stream.next().await.unwrap().unwrap() {
                    fsys::RealmQueryRequest::GetInstanceInfo { moniker, responder } => {
                        (moniker, responder)
                    }
                    _ => panic!("Unexpected RealmQuery request"),
                };
                let response = instances.remove(&moniker).unwrap();
                responder.send(&mut Ok(response)).unwrap();
            }
        })
        .detach();
        client
    }

    pub fn create_appmgr_out() -> (TempDir, ClientEnd<fio::DirectoryMarker>) {
        let temp_dir = TempDir::new_in("/tmp").unwrap();
        let root = temp_dir.path();

        fs::create_dir_all(root.join("hub/r")).unwrap();

        {
            let sshd = root.join("hub/c/sshd.cmx/9898");
            fs::create_dir_all(&sshd).unwrap();
            fs::create_dir_all(sshd.join("in/pkg")).unwrap();
            fs::create_dir_all(sshd.join("out/dev")).unwrap();

            fs::write(sshd.join("url"), "fuchsia-pkg://fuchsia.com/sshd#meta/sshd.cmx").unwrap();
            fs::write(sshd.join("in/pkg/meta"), "1234").unwrap();
            fs::write(sshd.join("job-id"), "5454").unwrap();
            fs::write(sshd.join("process-id"), "9898").unwrap();
        }

        let root = root.display().to_string();
        let (client, server) = create_endpoints::<fio::DirectoryMarker>().unwrap();
        fuchsia_fs::directory::open_channel_in_namespace(
            &root,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DIRECTORY,
            server,
        )
        .unwrap();
        (temp_dir, client)
    }

    #[fuchsia::test]
    async fn basic_cml() {
        let explorer = serve_realm_explorer(vec![fsys::InstanceInfo {
            moniker: "./".to_string(),
            url: "fuchsia-pkg://fuchsia.com/foo#meta/bar.cm".to_string(),
            instance_id: Some("1234567890".to_string()),
            state: fsys::InstanceState::Started,
        }]);

        let mut instances = get_all_cml_instances(&explorer).await.unwrap();
        assert_eq!(instances.len(), 1);
        let instance = instances.remove(0);

        assert_eq!(instance.moniker, AbsoluteMoniker::root());
        assert_eq!(instance.url, Some("fuchsia-pkg://fuchsia.com/foo#meta/bar.cm".to_string()));
        assert_eq!(instance.instance_id, Some("1234567890".to_string()));
        assert!(!instance.is_cmx);
        assert!(instance.hub_dir.is_none());
        assert_eq!(instance.state, InstanceState::Started);
    }

    #[fuchsia::test]
    async fn basic_cmx() {
        let (_temp_out_dir, out_dir) = create_appmgr_out();

        // The exposed and namespace dir is not used by this library.
        let (exposed_dir, _) = create_endpoints::<fio::DirectoryMarker>().unwrap();
        let (ns_dir, _) = create_endpoints::<fio::DirectoryMarker>().unwrap();

        let query = serve_realm_query(HashMap::from([(
            "./core/appmgr".to_string(),
            (
                fsys::InstanceInfo {
                    moniker: "./core/appmgr".to_string(),
                    url: "fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm".to_string(),
                    instance_id: None,
                    state: fsys::InstanceState::Started,
                },
                Some(Box::new(fsys::ResolvedState {
                    uses: vec![],
                    exposes: vec![],
                    config: None,
                    pkg_dir: None,
                    execution: Some(Box::new(fsys::ExecutionState {
                        out_dir: Some(out_dir),
                        runtime_dir: None,
                        start_reason: "Debugging Workflow".to_string(),
                    })),
                    exposed_dir,
                    ns_dir,
                })),
            ),
        )]));

        let mut instances = get_all_cmx_instances(&query).await.unwrap();
        assert_eq!(instances.len(), 1);
        let instance = instances.remove(0);

        assert_eq!(instance.moniker, AbsoluteMoniker::parse_str("/core/appmgr/sshd.cmx").unwrap());
        assert_eq!(instance.url, Some("fuchsia-pkg://fuchsia.com/sshd#meta/sshd.cmx".to_string()));
        assert!(instance.instance_id.is_none());
        assert!(instance.is_cmx);
        assert!(instance.hub_dir.is_some());
        assert_eq!(instance.state, InstanceState::Started);
    }

    fn create_explorer_and_query() -> (TempDir, fsys::RealmExplorerProxy, fsys::RealmQueryProxy) {
        // Serve RealmExplorer for CML components
        let explorer = serve_realm_explorer(vec![
            fsys::InstanceInfo {
                moniker: "./core".to_string(),
                url: "fuchsia-pkg://fuchsia.com/core#meta/core.cm".to_string(),
                instance_id: None,
                state: fsys::InstanceState::Started,
            },
            fsys::InstanceInfo {
                moniker: "./core/appmgr".to_string(),
                url: "fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm".to_string(),
                instance_id: None,
                state: fsys::InstanceState::Started,
            },
            fsys::InstanceInfo {
                moniker: "./".to_string(),
                url: "fuchsia-pkg://fuchsia.com/root#meta/root.cm".to_string(),
                instance_id: None,
                state: fsys::InstanceState::Resolved,
            },
        ]);

        // Serve RealmQuery to provide /core/appmgr and hence the CMX hub
        let (temp_dir, out_dir) = create_appmgr_out();

        // The exposed and namespace dir is not used by this library.
        let (exposed_dir, _) = create_endpoints::<fio::DirectoryMarker>().unwrap();
        let (ns_dir, _) = create_endpoints::<fio::DirectoryMarker>().unwrap();

        let query = serve_realm_query(HashMap::from([(
            "./core/appmgr".to_string(),
            (
                fsys::InstanceInfo {
                    moniker: "./core/appmgr".to_string(),
                    url: "fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm".to_string(),
                    instance_id: None,
                    state: fsys::InstanceState::Started,
                },
                Some(Box::new(fsys::ResolvedState {
                    uses: vec![],
                    exposes: vec![],
                    config: None,
                    pkg_dir: None,
                    execution: Some(Box::new(fsys::ExecutionState {
                        out_dir: Some(out_dir),
                        runtime_dir: None,
                        start_reason: "Debugging Workflow".to_string(),
                    })),
                    exposed_dir,
                    ns_dir,
                })),
            ),
        )]));
        (temp_dir, explorer, query)
    }

    #[fuchsia::test]
    async fn no_filter() {
        let (_temp_dir, explorer, query) = create_explorer_and_query();

        let mut instances = get_all_instances(&explorer, &query, None).await.unwrap();
        assert_eq!(instances.len(), 4);

        assert_eq!(instances.remove(0).moniker, AbsoluteMoniker::root());
        assert_eq!(instances.remove(0).moniker, AbsoluteMoniker::parse_str("/core").unwrap());
        assert_eq!(
            instances.remove(0).moniker,
            AbsoluteMoniker::parse_str("/core/appmgr").unwrap()
        );
        assert_eq!(
            instances.remove(0).moniker,
            AbsoluteMoniker::parse_str("/core/appmgr/sshd.cmx").unwrap()
        );
    }

    #[fuchsia::test]
    async fn cml_only() {
        let (_temp_dir, explorer, query) = create_explorer_and_query();

        let mut instances =
            get_all_instances(&explorer, &query, Some(ListFilter::CML)).await.unwrap();
        assert_eq!(instances.len(), 3);

        assert_eq!(instances.remove(0).moniker, AbsoluteMoniker::root());
        assert_eq!(instances.remove(0).moniker, AbsoluteMoniker::parse_str("/core").unwrap());
        assert_eq!(
            instances.remove(0).moniker,
            AbsoluteMoniker::parse_str("/core/appmgr").unwrap()
        );
    }

    #[fuchsia::test]
    async fn cmx_only() {
        let (_temp_dir, explorer, query) = create_explorer_and_query();

        let mut instances =
            get_all_instances(&explorer, &query, Some(ListFilter::CMX)).await.unwrap();
        assert_eq!(instances.len(), 1);
        assert_eq!(
            instances.remove(0).moniker,
            AbsoluteMoniker::parse_str("/core/appmgr/sshd.cmx").unwrap()
        );
    }

    #[fuchsia::test]
    async fn running_only() {
        let (_temp_dir, explorer, query) = create_explorer_and_query();

        let mut instances =
            get_all_instances(&explorer, &query, Some(ListFilter::Running)).await.unwrap();
        assert_eq!(instances.len(), 3);
        assert_eq!(instances.remove(0).moniker, AbsoluteMoniker::parse_str("/core").unwrap());
        assert_eq!(
            instances.remove(0).moniker,
            AbsoluteMoniker::parse_str("/core/appmgr").unwrap()
        );
        assert_eq!(
            instances.remove(0).moniker,
            AbsoluteMoniker::parse_str("/core/appmgr/sshd.cmx").unwrap()
        );
    }

    #[fuchsia::test]
    async fn stopped_only() {
        let (_temp_dir, explorer, query) = create_explorer_and_query();

        let mut instances =
            get_all_instances(&explorer, &query, Some(ListFilter::Stopped)).await.unwrap();
        assert_eq!(instances.len(), 1);
        assert_eq!(instances.remove(0).moniker, AbsoluteMoniker::root());
    }

    #[fuchsia::test]
    async fn descendants_only() {
        let (_temp_dir, explorer, query) = create_explorer_and_query();

        let mut instances =
            get_all_instances(&explorer, &query, Some(ListFilter::Descendant("core".to_string())))
                .await
                .unwrap();
        assert_eq!(instances.len(), 3);
        assert_eq!(instances.remove(0).moniker, AbsoluteMoniker::parse_str("/core").unwrap());
        assert_eq!(
            instances.remove(0).moniker,
            AbsoluteMoniker::parse_str("/core/appmgr").unwrap()
        );
        assert_eq!(
            instances.remove(0).moniker,
            AbsoluteMoniker::parse_str("/core/appmgr/sshd.cmx").unwrap()
        );
    }

    #[fuchsia::test]
    async fn ancestors_only() {
        let (_temp_dir, explorer, query) = create_explorer_and_query();

        let mut instances =
            get_all_instances(&explorer, &query, Some(ListFilter::Ancestor("core".to_string())))
                .await
                .unwrap();
        assert_eq!(instances.len(), 2);
        assert_eq!(instances.remove(0).moniker, AbsoluteMoniker::root());
        assert_eq!(instances.remove(0).moniker, AbsoluteMoniker::parse_str("/core").unwrap());
    }

    #[fuchsia::test]
    async fn relative_only() {
        let (_temp_dir, explorer, query) = create_explorer_and_query();

        let mut instances =
            get_all_instances(&explorer, &query, Some(ListFilter::Relative("core".to_string())))
                .await
                .unwrap();
        assert_eq!(instances.len(), 4);

        assert_eq!(instances.remove(0).moniker, AbsoluteMoniker::root());
        assert_eq!(instances.remove(0).moniker, AbsoluteMoniker::parse_str("/core").unwrap());
        assert_eq!(
            instances.remove(0).moniker,
            AbsoluteMoniker::parse_str("/core/appmgr").unwrap()
        );
        assert_eq!(
            instances.remove(0).moniker,
            AbsoluteMoniker::parse_str("/core/appmgr/sshd.cmx").unwrap()
        );
    }
}
