// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    crate::list::{get_all_instances, Instance as ListInstance},
    anyhow::{bail, format_err, Context, Result},
    cm_rust::{ExposeDecl, ExposeDeclCommon, FidlIntoNative, UseDecl},
    fidl_fuchsia_component_config as fconfig, fidl_fuchsia_sys2 as fsys,
    fuchsia_async::TimeoutExt,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, RelativeMoniker, RelativeMonikerBase},
};

#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};

/// This value is somewhat arbitrarily chosen based on how long we expect a component to take to
/// respond to a directory request. There is no clear answer for how long it should take a
/// component to respond. A request may take unnaturally long if the host is connected to the
/// target over a weak network connection. The target may be busy doing other work, resulting in a
/// delayed response here. A request may never return a response, if the component is simply holding
/// onto the directory handle without serving or dropping it. We should choose a value that balances
/// a reasonable expectation from the component without making the user wait for too long.
// TODO(http://fxbug.dev/99927): Get network latency info from ffx to choose a better timeout.
static DIR_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(1);

/// Uses the fuchsia.sys2.RealmExplorer and fuchsia.sys2.RealmQuery protocols to find all CMX/CML
/// components whose component name, ID or URL contains |query| as a substring.
pub async fn find_instances(
    query: String,
    explorer_proxy: &fsys::RealmExplorerProxy,
    query_proxy: &fsys::RealmQueryProxy,
) -> Result<Vec<Instance>> {
    let list_instances = get_all_instances(explorer_proxy, query_proxy, None).await?;
    let list_instances: Vec<ListInstance> = list_instances
        .into_iter()
        .filter(|i| {
            let url_match = i.url.as_ref().map_or(false, |url| url.contains(&query));
            let moniker_match = i.moniker.to_string().contains(&query);
            let id_match = i.instance_id.as_ref().map_or(false, |id| id.contains(&query));
            url_match || moniker_match || id_match
        })
        .collect();

    let mut instances = vec![];

    for list_instance in list_instances {
        if !list_instance.is_cmx {
            // Get the detailed information for the CML instance
            let moniker_str = format!(".{}", list_instance.moniker.to_string());
            match query_proxy.get_instance_info(&moniker_str).await? {
                Ok((info, resolved)) => {
                    let instance = Instance::parse(info, resolved).await?;
                    instances.push(instance);
                }
                Err(fsys::RealmQueryError::InstanceNotFound) => {
                    bail!(
                        "Instance {} was destroyed before its information could be obtained",
                        list_instance.moniker
                    );
                }
                Err(e) => {
                    bail!(
                        "Could not get detailed information for {} from Hub: {:?}",
                        list_instance.moniker,
                        e
                    );
                }
            }
        } else if let Some(hub_dir) = list_instance.hub_dir {
            let instance = Instance::parse_cmx(list_instance.moniker, hub_dir).await?;
            instances.push(instance);
        }
    }

    Ok(instances)
}

// Get all entries in a capabilities directory. If there is a "svc" directory, traverse it and
// collect all protocol names as well.
async fn get_capabilities(capability_dir: Directory) -> Result<Vec<String>> {
    let mut entries = capability_dir.entry_names().await?;

    for (index, name) in entries.iter().enumerate() {
        if name == "svc" {
            entries.remove(index);
            let svc_dir = capability_dir.open_dir_readable("svc")?;
            let mut svc_entries = svc_dir.entry_names().await?;
            entries.append(&mut svc_entries);
            break;
        }
    }

    entries.sort_unstable();
    Ok(entries)
}

/// Additional information about components that are using the ELF runner
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Debug, Eq, PartialEq)]
pub struct ElfRuntime {
    pub job_id: u64,
    pub process_id: Option<u64>,
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

        let job_id = job_id?.parse::<u64>().context("Job ID is not u64")?;

        let process_id = match process_id {
            Ok(id) => Some(id.parse::<u64>().context("Process ID is not u64")?),
            Err(_) => None,
        };

        let process_start_time =
            process_start_time.ok().map(|time_string| time_string.parse::<i64>().ok()).flatten();

        let process_start_time_utc_estimate = process_start_time_utc_estimate.ok();

        Ok(Self { job_id, process_id, process_start_time, process_start_time_utc_estimate })
    }

    async fn parse_cmx(hub_dir: &Directory) -> Result<Self> {
        let (job_id, process_id) =
            futures::join!(hub_dir.read_file("job-id"), hub_dir.read_file("process-id"),);

        let job_id = job_id?.parse::<u64>().context("Job ID is not u64")?;

        let process_id = if hub_dir.exists("process-id").await? {
            Some(process_id?.parse::<u64>().context("Process ID is not u64")?)
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

/// Additional information about components that are running.
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(Debug, Eq, PartialEq)]
pub struct Execution {
    pub elf_runtime: Option<ElfRuntime>,
    pub outgoing_capabilities: Option<Vec<String>>,
    pub start_reason: String,
}

impl Execution {
    async fn parse(execution: Box<fsys::ExecutionState>) -> Result<Self> {
        let elf_runtime = if let Some(runtime_dir) = execution.runtime_dir {
            let runtime_dir = runtime_dir.into_proxy()?;
            let runtime_dir = Directory::from_proxy(runtime_dir);

            // Some runners may not serve the runtime directory, so attempting to get the entries
            // may fail. This is normal and should be treated as no ELF runtime.
            if let Ok(true) = runtime_dir
                .exists("elf")
                .on_timeout(DIR_TIMEOUT, || {
                    Err(format_err!("timeout occurred opening `runtime` dir"))
                })
                .await
            {
                let elf_dir = runtime_dir.open_dir_readable("elf")?;
                Some(ElfRuntime::parse(elf_dir).await?)
            } else {
                None
            }
        } else {
            None
        };

        let outgoing_capabilities = if let Some(out_dir) = execution.out_dir {
            let out_dir = out_dir.into_proxy()?;
            let out_dir = Directory::from_proxy(out_dir);
            get_capabilities(out_dir)
                .on_timeout(DIR_TIMEOUT, || Err(format_err!("timeout occurred opening `out` dir")))
                .await
                .ok()
        } else {
            None
        };

        Ok(Self { elf_runtime, outgoing_capabilities, start_reason: execution.start_reason })
    }

    async fn parse_cmx(hub_dir: &Directory) -> Result<Self> {
        let elf_runtime = Some(ElfRuntime::parse_cmx(hub_dir).await?);

        let outgoing_capabilities = if hub_dir.exists("out").await? {
            let out_dir = hub_dir.open_dir_readable("out")?;
            get_capabilities(out_dir)
                .on_timeout(DIR_TIMEOUT, || Err(format_err!("timeout occurred opening `out` dir")))
                .await
                .ok()
        } else {
            // The directory doesn't exist. This is probably because
            // there is no runtime on the component.
            None
        };

        let start_reason = "Unknown start reason".to_string();

        Ok(Self { elf_runtime, outgoing_capabilities, start_reason })
    }
}

/// A single structured configuration key-value pair.
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
#[derive(PartialEq, Debug)]
pub struct ConfigField {
    pub key: String,
    pub value: String,
}

/// Additional information about components that are resolved.
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
pub struct Resolved {
    pub incoming_capabilities: Vec<String>,
    pub exposed_capabilities: Vec<String>,
    pub merkle_root: Option<String>,
    pub config: Option<Vec<ConfigField>>,
    pub started: Option<Execution>,
}

impl Resolved {
    async fn parse(resolved: Box<fsys::ResolvedState>) -> Result<Self> {
        let incoming_capabilities = {
            let mut capabilities = vec![];
            for capability in resolved.uses {
                let use_decl: UseDecl = capability.fidl_into_native();
                if let Some(path) = use_decl.path() {
                    let path = path.to_string();
                    capabilities.push(path);
                }
            }
            capabilities
        };

        let exposed_capabilities = {
            let mut capabilities = vec![];
            for capability in resolved.exposes {
                let expose_decl: ExposeDecl = capability.fidl_into_native();
                let name = expose_decl.target_name();
                let name = name.to_string();
                capabilities.push(name);
            }
            capabilities
        };

        let merkle_root = if let Some(pkg_dir) = resolved.pkg_dir {
            let pkg_dir = pkg_dir.into_proxy()?;
            let pkg_dir = Directory::from_proxy(pkg_dir);
            if pkg_dir.exists("meta").await? {
                pkg_dir.read_file("meta").await.ok()
            } else {
                None
            }
        } else {
            None
        };

        let started = if let Some(execution) = resolved.execution {
            Some(Execution::parse(execution).await?)
        } else {
            None
        };

        let config = if let Some(config) = resolved.config {
            let fields = config
                .fields
                .iter()
                .map(|f| {
                    let value = match &f.value {
                        fconfig::Value::Vector(v) => format!("{:#?}", v),
                        fconfig::Value::Single(v) => format!("{:?}", v),
                        _ => "Unknown".to_string(),
                    };
                    ConfigField { key: f.key.clone(), value }
                })
                .collect();
            Some(fields)
        } else {
            None
        };

        Ok(Self { incoming_capabilities, exposed_capabilities, merkle_root, started, config })
    }

    async fn parse_cmx(hub_dir: &Directory) -> Result<Self> {
        let incoming_capabilities = {
            let in_dir = hub_dir.open_dir_readable("in")?;
            get_capabilities(in_dir).await?
        };

        let in_dir = hub_dir.open_dir_readable("in")?;
        let merkle_root = if in_dir.exists("pkg").await? {
            let pkg_dir = in_dir.open_dir_readable("pkg")?;
            if pkg_dir.exists("meta").await? {
                pkg_dir.read_file("meta").await.ok()
            } else {
                None
            }
        } else {
            None
        };

        let started = Some(Execution::parse_cmx(hub_dir).await?);

        Ok(Self {
            incoming_capabilities,
            exposed_capabilities: vec![],
            merkle_root,
            config: None,
            started,
        })
    }
}

/// Basic information about a component for the `show` command.
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize))]
pub struct Instance {
    pub moniker: AbsoluteMoniker,
    pub url: String,
    pub is_cmx: bool,
    pub instance_id: Option<String>,
    pub resolved: Option<Resolved>,
}

impl Instance {
    async fn parse(
        info: fsys::InstanceInfo,
        resolved: Option<Box<fsys::ResolvedState>>,
    ) -> Result<Instance> {
        let resolved = if let Some(resolved) = resolved {
            Some(Resolved::parse(resolved).await?)
        } else {
            None
        };

        let moniker = RelativeMoniker::parse_str(&info.moniker)?;
        let moniker = AbsoluteMoniker::root().descendant(&moniker);

        Ok(Instance {
            moniker,
            url: info.url,
            is_cmx: false,
            instance_id: info.instance_id,
            resolved,
        })
    }

    pub async fn parse_cmx(moniker: AbsoluteMoniker, hub_dir: Directory) -> Result<Instance> {
        let resolved = Some(Resolved::parse_cmx(&hub_dir).await?);

        let url = hub_dir.read_file("url").await?;

        Ok(Instance { moniker, url, is_cmx: true, instance_id: None, resolved })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::*;
    use fidl_fuchsia_component_decl as fdecl;
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

    pub fn create_pkg_dir() -> (TempDir, ClientEnd<fio::DirectoryMarker>) {
        let temp_dir = TempDir::new_in("/tmp").unwrap();
        let root = temp_dir.path();

        fs::write(root.join("meta"), "1234").unwrap();

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

    pub fn create_out_dir() -> (TempDir, ClientEnd<fio::DirectoryMarker>) {
        let temp_dir = TempDir::new_in("/tmp").unwrap();
        let root = temp_dir.path();

        fs::create_dir(root.join("diagnostics")).unwrap();

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

    pub fn create_runtime_dir() -> (TempDir, ClientEnd<fio::DirectoryMarker>) {
        let temp_dir = TempDir::new_in("/tmp").unwrap();
        let root = temp_dir.path();

        fs::create_dir_all(root.join("elf")).unwrap();
        fs::write(root.join("elf/job_id"), "1234").unwrap();
        fs::write(root.join("elf/process_id"), "2345").unwrap();
        fs::write(root.join("elf/process_start_time"), "3456").unwrap();
        fs::write(root.join("elf/process_start_time_utc_estimate"), "abcd").unwrap();

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

    fn create_explorer_and_query() -> (Vec<TempDir>, fsys::RealmExplorerProxy, fsys::RealmQueryProxy)
    {
        // Serve RealmExplorer for CML components.
        let explorer = serve_realm_explorer(vec![
            fsys::InstanceInfo {
                moniker: "./my_foo".to_string(),
                url: "fuchsia-pkg://fuchsia.com/foo#meta/foo.cm".to_string(),
                instance_id: Some("1234567890".to_string()),
                state: fsys::InstanceState::Resolved,
            },
            fsys::InstanceInfo {
                moniker: "./core/appmgr".to_string(),
                url: "fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm".to_string(),
                instance_id: None,
                state: fsys::InstanceState::Started,
            },
        ]);

        // Serve RealmQuery for CML components.
        let (temp_dir_out, out_dir) = create_out_dir();
        let (temp_dir_pkg, pkg_dir) = create_pkg_dir();
        let (temp_dir_runtime, runtime_dir) = create_runtime_dir();
        let (temp_dir_appmgr_out, appmgr_out_dir) = create_appmgr_out();

        // The exposed dir is not used by this library.
        let (exposed_dir, _) = create_endpoints::<fio::DirectoryMarker>().unwrap();
        let (appmgr_exposed_dir, _) = create_endpoints::<fio::DirectoryMarker>().unwrap();

        // The namespace dir is not used by this library.
        let (ns_dir, _) = create_endpoints::<fio::DirectoryMarker>().unwrap();
        let (appmgr_ns_dir, _) = create_endpoints::<fio::DirectoryMarker>().unwrap();

        let query = serve_realm_query(HashMap::from([
            (
                "./my_foo".to_string(),
                (
                    fsys::InstanceInfo {
                        moniker: "./my_foo".to_string(),
                        url: "fuchsia-pkg://fuchsia.com/foo#meta/foo.cm".to_string(),
                        instance_id: Some("1234567890".to_string()),
                        state: fsys::InstanceState::Resolved,
                    },
                    Some(Box::new(fsys::ResolvedState {
                        uses: vec![fdecl::Use::Protocol(fdecl::UseProtocol {
                            dependency_type: Some(fdecl::DependencyType::Strong),
                            source: Some(fdecl::Ref::Parent(fdecl::ParentRef)),
                            source_name: Some("fuchsia.foo.bar".to_string()),
                            target_path: Some("/svc/fuchsia.foo.bar".to_string()),
                            ..fdecl::UseProtocol::EMPTY
                        })],
                        exposes: vec![fdecl::Expose::Protocol(fdecl::ExposeProtocol {
                            source: Some(fdecl::Ref::Self_(fdecl::SelfRef)),
                            source_name: Some("fuchsia.bar.baz".to_string()),
                            target_name: Some("fuchsia.bar.baz".to_string()),
                            target: Some(fdecl::Ref::Parent(fdecl::ParentRef)),
                            ..fdecl::ExposeProtocol::EMPTY
                        })],
                        config: Some(Box::new(fconfig::ResolvedConfig {
                            checksum: fdecl::ConfigChecksum::Sha256([0; 32]),
                            fields: vec![fconfig::ResolvedConfigField {
                                key: "foo".to_string(),
                                value: fconfig::Value::Single(fconfig::SingleValue::Bool(false)),
                            }],
                        })),
                        pkg_dir: Some(pkg_dir),
                        execution: Some(Box::new(fsys::ExecutionState {
                            out_dir: Some(out_dir),
                            runtime_dir: Some(runtime_dir),
                            start_reason: "Debugging Workflow".to_string(),
                        })),
                        exposed_dir,
                        ns_dir,
                    })),
                ),
            ),
            (
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
                            out_dir: Some(appmgr_out_dir),
                            runtime_dir: None,
                            start_reason: "Debugging Workflow".to_string(),
                        })),
                        exposed_dir: appmgr_exposed_dir,
                        ns_dir: appmgr_ns_dir,
                    })),
                ),
            ),
        ]));
        (vec![temp_dir_out, temp_dir_pkg, temp_dir_runtime, temp_dir_appmgr_out], explorer, query)
    }

    #[fuchsia::test]
    async fn basic_cml() {
        let (_temp_dirs, explorer, query) = create_explorer_and_query();

        let mut instances = find_instances("foo.cm".to_string(), &explorer, &query).await.unwrap();
        assert_eq!(instances.len(), 1);
        let instance = instances.remove(0);

        assert_eq!(instance.moniker, AbsoluteMoniker::parse_str("/my_foo").unwrap());
        assert_eq!(instance.url, "fuchsia-pkg://fuchsia.com/foo#meta/foo.cm");
        assert_eq!(instance.instance_id.unwrap(), "1234567890");
        assert!(!instance.is_cmx);
        assert!(instance.resolved.is_some());

        let resolved = instance.resolved.unwrap();
        assert_eq!(resolved.incoming_capabilities.len(), 1);
        assert_eq!(resolved.incoming_capabilities[0], "/svc/fuchsia.foo.bar");

        assert_eq!(resolved.exposed_capabilities.len(), 1);
        assert_eq!(resolved.exposed_capabilities[0], "fuchsia.bar.baz");
        assert_eq!(resolved.merkle_root.unwrap(), "1234");

        let config = resolved.config.unwrap();
        assert_eq!(
            config,
            vec![ConfigField { key: "foo".to_string(), value: "Bool(false)".to_string() }]
        );

        let started = resolved.started.unwrap();
        let outgoing_capabilities = started.outgoing_capabilities.unwrap();
        assert_eq!(outgoing_capabilities, vec!["diagnostics".to_string()]);
        assert_eq!(started.start_reason, "Debugging Workflow".to_string());

        let elf_runtime = started.elf_runtime.unwrap();
        assert_eq!(elf_runtime.job_id, 1234);
        assert_eq!(elf_runtime.process_id, Some(2345));
        assert_eq!(elf_runtime.process_start_time, Some(3456));
        assert_eq!(elf_runtime.process_start_time_utc_estimate, Some("abcd".to_string()));
    }

    #[fuchsia::test]
    async fn basic_cmx() {
        let (_temp_dirs, explorer, query) = create_explorer_and_query();

        let mut instances = find_instances("sshd".to_string(), &explorer, &query).await.unwrap();
        assert_eq!(instances.len(), 1);
        let instance = instances.remove(0);

        assert_eq!(instance.moniker, AbsoluteMoniker::parse_str("/core/appmgr/sshd.cmx").unwrap());
        assert_eq!(instance.url, "fuchsia-pkg://fuchsia.com/sshd#meta/sshd.cmx");
        assert!(instance.instance_id.is_none());
        assert!(instance.is_cmx);
        assert!(instance.resolved.is_some());

        let resolved = instance.resolved.unwrap();
        assert_eq!(resolved.incoming_capabilities.len(), 1);
        assert_eq!(resolved.incoming_capabilities[0], "pkg");

        assert!(resolved.exposed_capabilities.is_empty());
        assert_eq!(resolved.merkle_root.unwrap(), "1234");

        assert!(resolved.config.is_none());

        let started = resolved.started.unwrap();
        let outgoing_capabilities = started.outgoing_capabilities.unwrap();
        assert_eq!(outgoing_capabilities, vec!["dev".to_string()]);
        assert_eq!(started.start_reason, "Unknown start reason".to_string());

        let elf_runtime = started.elf_runtime.unwrap();
        assert_eq!(elf_runtime.job_id, 5454);
        assert_eq!(elf_runtime.process_id, Some(9898));
        assert_eq!(elf_runtime.process_start_time, None);
        assert_eq!(elf_runtime.process_start_time_utc_estimate, None);
    }

    #[fuchsia::test]
    async fn find_by_moniker() {
        let (_temp_dirs, explorer, query) = create_explorer_and_query();

        let mut instances = find_instances("my_foo".to_string(), &explorer, &query).await.unwrap();
        assert_eq!(instances.len(), 1);
        let instance = instances.remove(0);

        assert_eq!(instance.moniker, AbsoluteMoniker::parse_str("/my_foo").unwrap());
        assert_eq!(instance.url, "fuchsia-pkg://fuchsia.com/foo#meta/foo.cm");
        assert_eq!(instance.instance_id.unwrap(), "1234567890");
    }

    #[fuchsia::test]
    async fn find_by_instance_id() {
        let (_temp_dirs, explorer, query) = create_explorer_and_query();

        let mut instances = find_instances("1234567".to_string(), &explorer, &query).await.unwrap();
        assert_eq!(instances.len(), 1);
        let instance = instances.remove(0);

        assert_eq!(instance.moniker, AbsoluteMoniker::parse_str("/my_foo").unwrap());
        assert_eq!(instance.url, "fuchsia-pkg://fuchsia.com/foo#meta/foo.cm");
        assert_eq!(instance.instance_id.unwrap(), "1234567890");
    }
}
