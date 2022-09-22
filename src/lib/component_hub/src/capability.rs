// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    crate::list::get_all_instances,
    anyhow::{bail, Result},
    cm_rust::{ExposeDecl, FidlIntoNative, UseDecl},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_async::TimeoutExt,
    moniker::AbsoluteMoniker,
};

static CAPABILITY_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(1);

/// Component instances that use/expose a given capability, separated into two vectors (one for
/// components that expose the capability, the other for components that use the capability).
pub struct MatchingInstances {
    pub exposed: Vec<AbsoluteMoniker>,
    pub used: Vec<AbsoluteMoniker>,
}

/// Find components that expose or use a given capability. The capability must be a protocol name or
/// a directory capability name.
pub async fn find_instances_that_expose_or_use_capability(
    capability: String,
    explorer_proxy: &fsys::RealmExplorerProxy,
    query_proxy: &fsys::RealmQueryProxy,
) -> Result<MatchingInstances> {
    let list_instances = get_all_instances(explorer_proxy, query_proxy, None).await?;
    let mut matching_instances = MatchingInstances { exposed: vec![], used: vec![] };

    for list_instance in list_instances {
        if !list_instance.is_cmx {
            // Get the detailed information for the CML instance.
            // RealmQuery expects a relative moniker, so we add the `.` to the
            // absolute moniker, resulting in a moniker that looks like `./foo/bar`.
            let moniker_str = format!(".{}", list_instance.moniker.to_string());
            match query_proxy.get_instance_info(&moniker_str).await? {
                Ok((_, resolved)) => {
                    if let Some(resolved) = resolved {
                        let (exposed, used) =
                            capability_is_exposed_or_used_v2(resolved, &capability);
                        if exposed {
                            matching_instances.exposed.push(list_instance.moniker.clone());
                        }
                        if used {
                            matching_instances.used.push(list_instance.moniker.clone());
                        }
                    }
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
            let (exposed, used) = capability_is_exposed_or_used_v1(hub_dir, &capability).await?;
            if exposed {
                matching_instances.exposed.push(list_instance.moniker.clone());
            }
            if used {
                matching_instances.used.push(list_instance.moniker.clone())
            }
        }
    }

    Ok(matching_instances)
}

/// Determine if |capability| is exposed or used by this v2 component.
fn capability_is_exposed_or_used_v2(
    resolved: Box<fsys::ResolvedState>,
    capability: &str,
) -> (bool, bool) {
    let exposes = resolved.exposes;
    let uses = resolved.uses;

    let exposed = exposes.into_iter().any(|decl| {
        let name = match decl.fidl_into_native() {
            ExposeDecl::Protocol(p) => p.target_name,
            ExposeDecl::Directory(d) => d.target_name,
            ExposeDecl::Service(s) => s.target_name,
            _ => {
                return false;
            }
        };
        name.to_string() == capability
    });

    let used = uses.into_iter().any(|decl| {
        let name = match decl.fidl_into_native() {
            UseDecl::Protocol(p) => p.source_name,
            UseDecl::Directory(d) => d.source_name,
            UseDecl::Storage(s) => s.source_name,
            UseDecl::Service(s) => s.source_name,
            _ => {
                return false;
            }
        };
        name.to_string() == capability
    });

    (exposed, used)
}

/// Determine if |capability| is exposed or used by this v1 component.
async fn capability_is_exposed_or_used_v1(
    hub_dir: Directory,
    capability: &str,
) -> Result<(bool, bool)> {
    if !hub_dir.exists("out").await? {
        // No `out` directory implies no exposed capabilities
        return Ok((false, false));
    }

    let out_dir = hub_dir.open_dir_readable("out")?;
    let out_capabilities =
        get_capabilities(out_dir).on_timeout(CAPABILITY_TIMEOUT, || Ok(vec![])).await?;
    let in_dir = hub_dir.open_dir_readable("in")?;
    let in_capabilities =
        get_capabilities(in_dir).on_timeout(CAPABILITY_TIMEOUT, || Ok(vec![])).await?;

    let exposed = out_capabilities.iter().any(|c| c.as_str() == capability);
    let used = in_capabilities.iter().any(|c| c.as_str() == capability);
    Ok((exposed, used))
}

// Get all entries in a capabilities directory. If there is a "svc" directory, traverse it and
// collect all protocol names as well.
async fn get_capabilities(capability_dir: Directory) -> Result<Vec<String>> {
    let mut entries = capability_dir.entries().await?;

    for (index, name) in entries.iter().enumerate() {
        if name == "svc" {
            entries.remove(index);
            let svc_dir = capability_dir.open_dir_readable("svc")?;
            let mut svc_entries = svc_dir.entries().await?;
            entries.append(&mut svc_entries);
            break;
        }
    }

    Ok(entries)
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::*;
    use fidl_fuchsia_component_decl as fdecl;
    use fidl_fuchsia_io as fio;
    use fuchsia_async::Task;
    use futures::StreamExt;
    use moniker::AbsoluteMonikerBase;
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
        mut instances: HashMap<String, Vec<(fsys::InstanceInfo, Option<Box<fsys::ResolvedState>>)>>,
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
                let responses = instances.get_mut(&moniker).unwrap();
                let response = responses.remove(0);
                responder.send(&mut Ok(response)).unwrap();
            }
        })
        .detach();
        client
    }

    pub fn create_appmgr_out(
    ) -> (TempDir, ClientEnd<fio::DirectoryMarker>, ClientEnd<fio::DirectoryMarker>) {
        let temp_dir = TempDir::new_in("/tmp").unwrap();
        let root = temp_dir.path();

        fs::create_dir_all(root.join("hub/r")).unwrap();

        {
            let sshd = root.join("hub/c/sshd.cmx/9898");
            fs::create_dir_all(&sshd).unwrap();
            fs::create_dir_all(sshd.join("in/pkg")).unwrap();
            fs::create_dir_all(sshd.join("in/data")).unwrap();
            fs::create_dir_all(sshd.join("out/dev")).unwrap();

            fs::write(sshd.join("url"), "fuchsia-pkg://fuchsia.com/sshd#meta/sshd.cmx").unwrap();
            fs::write(sshd.join("in/pkg/meta"), "1234").unwrap();
            fs::write(sshd.join("job-id"), "5454").unwrap();
            fs::write(sshd.join("process-id"), "9898").unwrap();
        }

        let root = root.display().to_string();
        let (client1, server1) = create_endpoints::<fio::DirectoryMarker>().unwrap();
        fuchsia_fs::directory::open_channel_in_namespace(
            &root,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DIRECTORY,
            server1,
        )
        .unwrap();
        let (client2, server2) = create_endpoints::<fio::DirectoryMarker>().unwrap();
        fuchsia_fs::directory::open_channel_in_namespace(
            &root,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DIRECTORY,
            server2,
        )
        .unwrap();
        (temp_dir, client1, client2)
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
        let (temp_dir_appmgr_out, appmgr_out_dir_1, appmgr_out_dir_2) = create_appmgr_out();

        // The exposed dir is not used by this library.
        let (exposed_dir, _) = create_endpoints::<fio::DirectoryMarker>().unwrap();
        let (appmgr_exposed_dir_1, _) = create_endpoints::<fio::DirectoryMarker>().unwrap();
        let (appmgr_exposed_dir_2, _) = create_endpoints::<fio::DirectoryMarker>().unwrap();

        // The namespace dir is not used by this library.
        let (ns_dir, _) = create_endpoints::<fio::DirectoryMarker>().unwrap();
        let (appmgr_ns_dir_1, _) = create_endpoints::<fio::DirectoryMarker>().unwrap();
        let (appmgr_ns_dir_2, _) = create_endpoints::<fio::DirectoryMarker>().unwrap();

        let query = serve_realm_query(HashMap::from([
            (
                "./my_foo".to_string(),
                vec![(
                    fsys::InstanceInfo {
                        moniker: "./my_foo".to_string(),
                        url: "fuchsia-pkg://fuchsia.com/foo#meta/foo.cm".to_string(),
                        instance_id: None,
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
                        config: None,
                        pkg_dir: None,
                        execution: None,
                        exposed_dir,
                        ns_dir,
                    })),
                )],
            ),
            (
                "./core/appmgr".to_string(),
                vec![
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
                                out_dir: Some(appmgr_out_dir_1),
                                runtime_dir: None,
                                start_reason: "Debugging Workflow".to_string(),
                            })),
                            exposed_dir: appmgr_exposed_dir_1,
                            ns_dir: appmgr_ns_dir_1,
                        })),
                    ),
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
                                out_dir: Some(appmgr_out_dir_2),
                                runtime_dir: None,
                                start_reason: "Debugging Workflow".to_string(),
                            })),
                            exposed_dir: appmgr_exposed_dir_2,
                            ns_dir: appmgr_ns_dir_2,
                        })),
                    ),
                ],
            ),
        ]));
        (vec![temp_dir_appmgr_out], explorer, query)
    }

    #[fuchsia::test]
    async fn uses_cml() {
        let (_temp_dirs, explorer, query) = create_explorer_and_query();

        let instances = find_instances_that_expose_or_use_capability(
            "fuchsia.foo.bar".to_string(),
            &explorer,
            &query,
        )
        .await
        .unwrap();
        let exposed = instances.exposed;
        let mut used = instances.used;
        assert_eq!(used.len(), 1);
        assert!(exposed.is_empty());

        let moniker = used.remove(0);
        assert_eq!(moniker, AbsoluteMoniker::parse_str("/my_foo").unwrap());
    }

    #[fuchsia::test]
    async fn uses_cmx() {
        let (_temp_dirs, explorer, query) = create_explorer_and_query();

        let instances =
            find_instances_that_expose_or_use_capability("data".to_string(), &explorer, &query)
                .await
                .unwrap();
        let exposed = instances.exposed;
        let mut used = instances.used;
        assert_eq!(used.len(), 1);
        assert!(exposed.is_empty());

        let moniker = used.remove(0);
        assert_eq!(moniker, AbsoluteMoniker::parse_str("/core/appmgr/sshd.cmx").unwrap());
    }

    #[fuchsia::test]
    async fn exposes_cml() {
        let (_temp_dirs, explorer, query) = create_explorer_and_query();

        let instances = find_instances_that_expose_or_use_capability(
            "fuchsia.bar.baz".to_string(),
            &explorer,
            &query,
        )
        .await
        .unwrap();
        let mut exposed = instances.exposed;
        let used = instances.used;
        assert_eq!(exposed.len(), 1);
        assert!(used.is_empty());

        let moniker = exposed.remove(0);
        assert_eq!(moniker, AbsoluteMoniker::parse_str("/my_foo").unwrap());
    }

    #[fuchsia::test]
    async fn exposes_cmx() {
        let (_temp_dirs, explorer, query) = create_explorer_and_query();

        let instances =
            find_instances_that_expose_or_use_capability("dev".to_string(), &explorer, &query)
                .await
                .unwrap();
        let mut exposed = instances.exposed;
        let used = instances.used;
        assert_eq!(exposed.len(), 1);
        assert!(used.is_empty());

        let moniker = exposed.remove(0);
        assert_eq!(moniker, AbsoluteMoniker::parse_str("/core/appmgr/sshd.cmx").unwrap());
    }
}
