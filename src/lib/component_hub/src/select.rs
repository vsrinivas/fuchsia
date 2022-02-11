// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    anyhow::Result,
    fuchsia_async::TimeoutExt,
    futures::future::{join, join_all, BoxFuture},
    futures::FutureExt,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMoniker, ChildMonikerBase},
};

static CAPABILITY_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(1);

/// Components that were found by |find_components|, separated into two vectors (one for components
/// that expose the capability, the other for components that use the capability).
pub struct MatchingComponents {
    pub exposed: Vec<AbsoluteMoniker>,
    pub used: Vec<AbsoluteMoniker>,
}

struct CapabilityExists {
    exposed: bool,
    used: bool,
}

/// Given a v2 hub directory, collect components that expose or use |capability|.
/// This function is recursive and will find matching CMX and CML components.
pub async fn find_components(capability: String, hub_dir: Directory) -> Result<MatchingComponents> {
    find_components_internal(capability, String::new(), AbsoluteMoniker::root(), hub_dir).await
}

fn find_components_internal(
    capability: String,
    name: String,
    moniker: AbsoluteMoniker,
    hub_dir: Directory,
) -> BoxFuture<'static, Result<MatchingComponents>> {
    async move {
        let mut futures = vec![];
        let children_dir = hub_dir.open_dir_readable("children")?;

        for child_name in children_dir.entries().await? {
            let child_moniker = ChildMoniker::parse(&child_name)?;
            let child_moniker = moniker.child(child_moniker);
            let child_hub_dir = children_dir.open_dir_readable(&child_name)?;
            let child_future = find_components_internal(
                capability.clone(),
                child_name,
                child_moniker,
                child_hub_dir,
            );
            futures.push(child_future);
        }

        if name == "appmgr" {
            let realm_dir = hub_dir.open_dir_readable("exec/out/hub")?;
            let appmgr_future = find_cmx_realms(capability.clone(), moniker.clone(), realm_dir);
            futures.push(appmgr_future);
        }

        let results = join_all(futures).await;
        let mut matching_components = MatchingComponents { exposed: vec![], used: vec![] };

        for result in results {
            let MatchingComponents { mut exposed, mut used } = result?;
            matching_components.exposed.append(&mut exposed);
            matching_components.used.append(&mut used);
        }

        let CapabilityExists { exposed, used } =
            capability_is_exposed_or_used_v2(hub_dir, capability).await?;

        if exposed {
            matching_components.exposed.push(moniker.clone());
        }
        if used {
            matching_components.used.push(moniker.clone());
        }

        Ok(matching_components)
    }
    .boxed()
}

// Given a v1 realm directory, return monikers of components that expose |capability|.
// |moniker| corresponds to the moniker of the current realm.
fn find_cmx_realms(
    capability: String,
    moniker: AbsoluteMoniker,
    hub_dir: Directory,
) -> BoxFuture<'static, Result<MatchingComponents>> {
    async move {
        let c_dir = hub_dir.open_dir_readable("c")?;
        let c_future = find_cmx_components_in_c_dir(capability.clone(), moniker.clone(), c_dir);

        let r_dir = hub_dir.open_dir_readable("r")?;
        let r_future = find_cmx_realms_in_r_dir(capability, moniker, r_dir);

        let (matching_components_c, matching_components_r) = join(c_future, r_future).await;

        let MatchingComponents { mut exposed, mut used } = matching_components_c?;
        let mut matching_components_r = matching_components_r?;

        exposed.append(&mut matching_components_r.exposed);
        used.append(&mut matching_components_r.used);

        Ok(MatchingComponents { exposed, used })
    }
    .boxed()
}

// Given a v1 component directory, return monikers of components that expose |capability|.
// |moniker| corresponds to the moniker of the current component.
fn find_cmx_components(
    capability: String,
    moniker: AbsoluteMoniker,
    hub_dir: Directory,
) -> BoxFuture<'static, Result<MatchingComponents>> {
    async move {
        let mut matching_components_exposed = vec![];
        let mut matching_components_used = vec![];

        // Component runners can have a `c` dir with child components
        if hub_dir.exists("c").await? {
            let c_dir = hub_dir.open_dir_readable("c")?;
            let MatchingComponents { mut exposed, mut used } =
                find_cmx_components_in_c_dir(capability.clone(), moniker.clone(), c_dir).await?;
            matching_components_exposed.append(&mut exposed);
            matching_components_used.append(&mut used);
        }

        let CapabilityExists { exposed, used } =
            capability_is_exposed_or_used_v1(hub_dir, capability).await?;
        if exposed {
            matching_components_exposed.push(moniker.clone());
        }
        if used {
            matching_components_used.push(moniker.clone())
        }

        Ok(MatchingComponents {
            exposed: matching_components_exposed,
            used: matching_components_used,
        })
    }
    .boxed()
}

async fn find_cmx_components_in_c_dir(
    capability: String,
    moniker: AbsoluteMoniker,
    c_dir: Directory,
) -> Result<MatchingComponents> {
    // Get all CMX child components
    let child_component_names = c_dir.entries().await?;
    let mut future_children = vec![];
    for child_component_name in child_component_names {
        let child_moniker = ChildMoniker::parse(&child_component_name)?;
        let child_moniker = moniker.child(child_moniker);
        let job_ids_dir = c_dir.open_dir_readable(&child_component_name)?;
        let hub_dirs = open_all_job_ids(job_ids_dir).await?;
        for hub_dir in hub_dirs {
            let future_child =
                find_cmx_components(capability.clone(), child_moniker.clone(), hub_dir);
            future_children.push(future_child);
        }
    }

    let results = join_all(future_children).await;
    let mut flattened_components = MatchingComponents { exposed: vec![], used: vec![] };

    for result in results {
        let MatchingComponents { mut exposed, mut used } = result?;
        flattened_components.exposed.append(&mut exposed);
        flattened_components.used.append(&mut used);
    }
    Ok(flattened_components)
}

async fn find_cmx_realms_in_r_dir(
    capability: String,
    moniker: AbsoluteMoniker,
    r_dir: Directory,
) -> Result<MatchingComponents> {
    // Get all CMX child realms
    let mut future_realms = vec![];
    for child_realm_name in r_dir.entries().await? {
        let child_moniker = ChildMoniker::parse(&child_realm_name)?;
        let child_moniker = moniker.child(child_moniker);
        let job_ids_dir = r_dir.open_dir_readable(&child_realm_name)?;
        let hub_dirs = open_all_job_ids(job_ids_dir).await?;
        for hub_dir in hub_dirs {
            let future_realm = find_cmx_realms(capability.clone(), child_moniker.clone(), hub_dir);
            future_realms.push(future_realm);
        }
    }
    let results = join_all(future_realms).await;
    let mut flattened_components = MatchingComponents { exposed: vec![], used: vec![] };

    for result in results {
        let MatchingComponents { mut exposed, mut used } = result?;
        flattened_components.exposed.append(&mut exposed);
        flattened_components.used.append(&mut used);
    }

    Ok(flattened_components)
}

async fn open_all_job_ids(job_ids_dir: Directory) -> Result<Vec<Directory>> {
    // Recurse on the job_ids
    let mut dirs = vec![];
    for job_id in job_ids_dir.entries().await? {
        let dir = job_ids_dir.open_dir_readable(&job_id)?;
        dirs.push(dir);
    }
    Ok(dirs)
}

/// Determine if |capability| is exposed or used by this v2 component.
async fn capability_is_exposed_or_used_v2(
    hub_dir: Directory,
    capability: String,
) -> Result<CapabilityExists> {
    if !hub_dir.exists("resolved").await? {
        // We have no information about an unresolved component
        return Ok(CapabilityExists { exposed: false, used: false });
    }

    let exec_dir = hub_dir.open_dir_readable("resolved/expose")?;
    let expose_capabilities = get_capabilities(exec_dir).await?;

    let use_dir = hub_dir.open_dir_readable("resolved/use")?;
    let used_capabilities = get_capabilities(use_dir).await?;

    Ok(CapabilityExists {
        exposed: expose_capabilities.iter().any(|c| c.as_str() == capability),
        used: used_capabilities.iter().any(|c| c.as_str() == capability),
    })
}

/// Determine if |capability| is exposed or used by this v1 component.
async fn capability_is_exposed_or_used_v1(
    hub_dir: Directory,
    capability: String,
) -> Result<CapabilityExists> {
    if !hub_dir.exists("out").await? {
        // No `out` directory implies no exposed capabilities
        return Ok(CapabilityExists { exposed: false, used: false });
    }

    let out_dir = hub_dir.open_dir_readable("out")?;
    let out_capabilities =
        get_capabilities(out_dir).on_timeout(CAPABILITY_TIMEOUT, || Ok(vec![])).await?;
    let in_dir = hub_dir.open_dir_readable("in")?;
    let in_capabilities =
        get_capabilities(in_dir).on_timeout(CAPABILITY_TIMEOUT, || Ok(vec![])).await?;

    Ok(CapabilityExists {
        exposed: out_capabilities.iter().any(|c| c.as_str() == capability),
        used: in_capabilities.iter().any(|c| c.as_str() == capability),
    })
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
    use {
        std::fs::{self, File},
        tempfile::TempDir,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn unresolved_cml() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        fs::create_dir(root.join("children")).unwrap();

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let MatchingComponents { exposed, used } =
            find_components("fuchsia.logger.LogSink".to_string(), hub_dir).await.unwrap();

        assert!(exposed.is_empty());
        assert!(used.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn cml_protocol_found() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        // |- resolved
        //    |- expose
        //       |- svc
        //          |- fuchsia.logger.LogSink
        //    |- use
        //       |- svc
        //          |- fuchsia.logger.LogSink
        fs::create_dir(root.join("children")).unwrap();
        fs::create_dir_all(root.join("resolved/expose/svc")).unwrap();
        File::create(root.join("resolved/expose/svc/fuchsia.logger.LogSink")).unwrap();
        fs::create_dir_all(root.join("resolved/use/svc")).unwrap();
        File::create(root.join("resolved/use/svc/fuchsia.logger.LogSink")).unwrap();

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let MatchingComponents { mut exposed, mut used } =
            find_components("fuchsia.logger.LogSink".to_string(), hub_dir).await.unwrap();

        assert_eq!(exposed.len(), 1);
        assert_eq!(used.len(), 1);
        let exposed_component = exposed.remove(0);
        let used_component = used.remove(0);
        assert!(exposed_component.is_root());
        assert!(used_component.is_root());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn cml_dir_found() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        // |- resolved
        //    |- expose
        //       |- hub
        //    |- use
        //       |- hub
        fs::create_dir(root.join("children")).unwrap();
        fs::create_dir_all(root.join("resolved/expose/hub")).unwrap();
        fs::create_dir_all(root.join("resolved/use/hub")).unwrap();

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let MatchingComponents { mut exposed, mut used } =
            find_components("hub".to_string(), hub_dir).await.unwrap();

        assert_eq!(exposed.len(), 1);
        assert_eq!(used.len(), 1);
        let exposed_component = exposed.remove(0);
        let used_component = used.remove(0);
        assert!(exposed_component.is_root());
        assert!(used_component.is_root());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn nested_cml() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        //    |- core
        //       |- children
        //       |- resolved
        //          |- expose
        //          |- use
        //             |- minfs
        // |- resolved
        //    |- expose
        //       |- minfs
        //    |- use
        fs::create_dir(root.join("children")).unwrap();
        fs::create_dir_all(root.join("resolved/expose/minfs")).unwrap();
        fs::create_dir_all(root.join("resolved/use")).unwrap();

        {
            let core = root.join("children/core");
            fs::create_dir_all(core.join("children")).unwrap();
            fs::create_dir_all(core.join("resolved/expose")).unwrap();
            fs::create_dir_all(core.join("resolved/use/minfs")).unwrap();
        }

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let MatchingComponents { mut exposed, mut used } =
            find_components("minfs".to_string(), hub_dir).await.unwrap();

        assert_eq!(exposed.len(), 1);
        assert_eq!(used.len(), 1);
        let exposed_component = exposed.remove(0);
        let used_component = used.remove(0);
        assert!(exposed_component.is_root());
        assert_eq!(used_component, vec!["core"].into());
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
        //          |- exec
        //             |- out
        //                |- hub
        //                   |- r
        //                   |- c
        //                      |- sshd.cmx
        //                         |- 9898
        //                            |- in
        //                               |- dev
        //                            |- out
        //                               |- dev
        fs::create_dir(root.join("children")).unwrap();

        {
            let appmgr = root.join("children/appmgr");
            fs::create_dir(&appmgr).unwrap();
            fs::create_dir(appmgr.join("children")).unwrap();
            fs::create_dir_all(appmgr.join("exec/out/hub/r")).unwrap();

            {
                let sshd = appmgr.join("exec/out/hub/c/sshd.cmx/9898");
                fs::create_dir_all(&sshd).unwrap();
                fs::create_dir_all(sshd.join("in/dev")).unwrap();
                fs::create_dir_all(sshd.join("out/dev")).unwrap();
            }
        }

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let MatchingComponents { mut exposed, mut used } =
            find_components("dev".to_string(), hub_dir).await.unwrap();

        assert_eq!(exposed.len(), 1);
        assert_eq!(used.len(), 1);
        let exposed_component = exposed.remove(0);
        let used_component = used.remove(0);
        assert_eq!(exposed_component, vec!["appmgr", "sshd.cmx"].into());
        assert_eq!(used_component, vec!["appmgr", "sshd.cmx"].into());
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
        //          |- exec
        //             |- out
        //                |- hub
        //                   |- r
        //                   |- c
        //                      |- sshd.cmx
        //                         |- 9898
        //                            |- c
        //                               |- foo.cmx
        //                                  |- 1919
        //                                     |- in
        //                                        |- dev
        //                                     |- out
        //                                        |- dev
        fs::create_dir(root.join("children")).unwrap();

        {
            let appmgr = root.join("children/appmgr");
            fs::create_dir(&appmgr).unwrap();
            fs::create_dir(appmgr.join("children")).unwrap();
            fs::create_dir_all(appmgr.join("exec/out/hub/r")).unwrap();

            {
                let sshd = appmgr.join("exec/out/hub/c/sshd.cmx/9898");
                fs::create_dir_all(&sshd).unwrap();
                {
                    let dev = sshd.join("c/foo.cmx/1919/in/dev");
                    fs::create_dir_all(&dev).unwrap();
                    let dev = sshd.join("c/foo.cmx/1919/out/dev");
                    fs::create_dir_all(&dev).unwrap();
                }
            }
        }

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let MatchingComponents { mut exposed, mut used } =
            find_components("dev".to_string(), hub_dir).await.unwrap();

        assert_eq!(exposed.len(), 1);
        assert_eq!(used.len(), 1);
        let exposed_component = exposed.remove(0);
        let used_component = used.remove(0);
        assert_eq!(exposed_component, vec!["appmgr", "sshd.cmx", "foo.cmx"].into());
        assert_eq!(used_component, vec!["appmgr", "sshd.cmx", "foo.cmx"].into());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn cmx_no_out() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        //       |- appmgr
        //          |- children
        //          |- exec
        //             |- out
        //                |- hub
        //                   |- r
        //                   |- c
        //                      |- sshd.cmx
        //                         |- 9898
        fs::create_dir(root.join("children")).unwrap();

        {
            let appmgr = root.join("children/appmgr");
            fs::create_dir(&appmgr).unwrap();
            fs::create_dir(appmgr.join("children")).unwrap();
            fs::create_dir_all(appmgr.join("exec/out/hub/r")).unwrap();

            {
                let sshd = appmgr.join("exec/out/hub/c/sshd.cmx/9898");
                fs::create_dir_all(&sshd).unwrap();
            }
        }

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let MatchingComponents { exposed, used } =
            find_components("dev".to_string(), hub_dir).await.unwrap();

        assert!(exposed.is_empty());
        assert!(used.is_empty());
    }
}
