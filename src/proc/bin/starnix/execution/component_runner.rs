// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ::runner::{get_program_string, get_program_strvec};
use anyhow::{anyhow, format_err, Context, Error};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_component as fcomponent;
use fidl_fuchsia_component_decl as fdecl;
use fidl_fuchsia_component_runner::{ComponentControllerMarker, ComponentStartInfo};
use fidl_fuchsia_io as fio;
use fuchsia_component::client as fclient;
use fuchsia_zircon as zx;
use log::info;
use rand::Rng;
use std::ffi::CString;
use std::sync::Arc;

use super::*;
use crate::auth::Credentials;
use crate::device::run_features;
use crate::fs::tmpfs::TmpFs;
use crate::fs::*;
use crate::task::*;
use crate::types::*;

pub fn start_component(
    start_info: ComponentStartInfo,
    controller: ServerEnd<ComponentControllerMarker>,
) -> Result<(), Error> {
    info!(
        "start_component: {}\narguments: {:?}\nmanifest: {:?}",
        start_info.resolved_url.clone().unwrap_or("<unknown>".to_string()),
        start_info.numbered_handles,
        start_info.program,
    );

    let mounts = get_program_strvec(&start_info, "mounts").map(|a| a.clone()).unwrap_or(vec![]);
    let binary_path = CString::new(
        get_program_string(&start_info, "binary")
            .ok_or_else(|| anyhow!("Missing \"binary\" in manifest"))?,
    )?;
    let args = get_program_strvec(&start_info, "args")
        .map(|args| {
            args.iter().map(|arg| CString::new(arg.clone())).collect::<Result<Vec<CString>, _>>()
        })
        .unwrap_or(Ok(vec![]))?;
    let environ = get_program_strvec(&start_info, "environ")
        .map(|args| {
            args.iter().map(|arg| CString::new(arg.clone())).collect::<Result<Vec<CString>, _>>()
        })
        .unwrap_or(Ok(vec![]))?;
    let user_passwd = get_program_string(&start_info, "user").unwrap_or("fuchsia:x:42:42");
    let credentials = Credentials::from_passwd(user_passwd)?;
    let apex_hack = get_program_strvec(&start_info, "apex_hack").map(|v| v.clone());
    let cmdline = get_program_string(&start_info, "kernel_cmdline").unwrap_or("");
    let features = get_program_strvec(&start_info, "features").map(|f| f.clone());

    info!("start_component environment: {:?}", environ);

    let kernel_name = if let Some(ref url) = start_info.resolved_url {
        let url = fuchsia_url::pkg_url::PkgUrl::parse(&url)?;
        let name = url.resource().unwrap_or(url.name().as_ref());
        CString::new(if let Some(i) = name.rfind('/') { &name[i + 1..] } else { name })
    } else {
        CString::new("kernel")
    }?;
    let mut kernel = Kernel::new(&kernel_name)?;
    kernel.cmdline = cmdline.as_bytes().to_vec();
    *kernel.outgoing_dir.lock() =
        start_info.outgoing_dir.map(|server_end| server_end.into_channel());
    let kernel = Arc::new(kernel);

    let ns = start_info.ns.ok_or_else(|| anyhow!("Missing namespace"))?;

    let pkg = fio::DirectorySynchronousProxy::new(
        ns.into_iter()
            .find(|entry| entry.path == Some("/pkg".to_string()))
            .ok_or_else(|| anyhow!("Missing /pkg entry in namespace"))?
            .directory
            .ok_or_else(|| anyhow!("Missing directory handlee in pkg namespace entry"))?
            .into_channel(),
    );

    // The mounts are appplied in the order listed. Mounting will fail if the designated mount
    // point doesn't exist in a previous mount. The root must be first so other mounts can be
    // applied on top of it.
    let mut mounts_iter = mounts.iter();
    let (root_point, root_fs) = create_filesystem_from_spec(
        &kernel,
        None,
        &pkg,
        mounts_iter.next().ok_or_else(|| anyhow!("Mounts list is empty"))?,
    )?;
    if root_point != b"/" {
        anyhow::bail!("First mount in mounts list is not the root");
    }
    let root_fs = if let WhatToMount::Fs(fs) = root_fs {
        fs
    } else {
        anyhow::bail!("how did a bind mount manage to get created as the root?")
    };

    let fs = FsContext::new(root_fs);

    let mut current_task =
        Task::create_process_without_parent(&kernel, binary_path.clone(), fs.clone())?;
    *current_task.creds.write() = credentials;
    let startup_handles =
        parse_numbered_handles(start_info.numbered_handles, &current_task.files, &kernel)?;
    let shell_controller = startup_handles.shell_controller;

    for mount_spec in mounts_iter {
        let (mount_point, child_fs) =
            create_filesystem_from_spec(&kernel, Some(&current_task), &pkg, mount_spec)?;
        let mount_point = current_task.lookup_path_from_root(mount_point)?;
        mount_point.mount(child_fs, MountFlags::empty())?;
    }

    // Hack to allow mounting apexes before apexd is working.
    // TODO(tbodt): Remove once apexd works.
    if let Some(apexes) = apex_hack {
        current_task
            .lookup_path_from_root(b"apex")?
            .mount(WhatToMount::Fs(TmpFs::new()), MountFlags::empty())?;
        let apex_dir = current_task.lookup_path_from_root(b"apex")?;
        for apex in apexes {
            let apex = apex.as_bytes();
            let apex_subdir = apex_dir.create_node(
                apex,
                FileMode::IFDIR | FileMode::from_bits(0o700),
                DeviceType::NONE,
            )?;
            let apex_source =
                current_task.lookup_path_from_root(&[b"system/apex/", apex].concat())?;
            apex_subdir.mount(WhatToMount::Dir(apex_source.entry), MountFlags::empty())?;
        }
    }

    // Run all the features (e.g., wayland) that were specified in the .cml.
    if let Some(features) = features {
        run_features(&features, &current_task)
            .map_err(|e| anyhow!("Failed to initialize features: {:?}", e))?;
    }

    let mut argv = vec![binary_path];
    argv.extend(args.into_iter());

    current_task.exec(argv[0].clone(), argv.clone(), environ.clone())?;

    execute_task(current_task, |result| {
        // TODO(fxb/74803): Using the component controller's epitaph may not be the best way to
        // communicate the exit code. The component manager could interpret certain epitaphs as starnix
        // being unstable, and chose to terminate starnix as a result.
        // Errors when closing the controller with an epitaph are disregarded, since there are
        // legitimate reasons for this to fail (like the client having closed the channel).
        if let Some(shell_controller) = shell_controller {
            let _ = shell_controller.close_with_epitaph(zx::Status::OK);
        }
        let _ = match result {
            Ok(0) => controller.close_with_epitaph(zx::Status::OK),
            _ => controller.close_with_epitaph(zx::Status::from_raw(
                fcomponent::Error::InstanceDied.into_primitive() as i32,
            )),
        };
    });

    Ok(())
}

/// Creates a new child component in the `playground` collection.
///
/// # Parameters
/// - `url`: The URL of the component to create.
/// - `args`: The `CreateChildArgs` that are passed to the component manager.
pub async fn create_child_component(
    url: String,
    args: fcomponent::CreateChildArgs,
) -> Result<(), Error> {
    // TODO(fxbug.dev/74511): The amount of setup required here is a bit lengthy. Ideally,
    // fuchsia-component would provide language-specific bindings for the Realm API that could
    // reduce this logic to a few lines.

    const COLLECTION: &str = "playground";
    let realm = fclient::realm().context("failed to connect to Realm service")?;
    let mut collection_ref = fdecl::CollectionRef { name: COLLECTION.into() };
    let id: u64 = rand::thread_rng().gen();
    let child_name = format!("starnix-{}", id);
    let child_decl = fdecl::Child {
        name: Some(child_name.clone()),
        url: Some(url),
        startup: Some(fdecl::StartupMode::Lazy),
        environment: None,
        ..fdecl::Child::EMPTY
    };
    let () = realm
        .create_child(&mut collection_ref, child_decl, args)
        .await?
        .map_err(|e| format_err!("failed to create child: {:?}", e))?;
    // The component is run in a `SingleRun` collection instance, and will be automatically
    // deleted when it exits.
    Ok(())
}
