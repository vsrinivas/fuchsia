// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::device::run_component_features;
use ::runner::{get_program_string, get_program_strvec};
use anyhow::{anyhow, format_err, Context, Error};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_component as fcomponent;
use fidl_fuchsia_component_decl as fdecl;
use fidl_fuchsia_component_runner::{ComponentControllerMarker, ComponentStartInfo};
use fidl_fuchsia_io as fio;
use fuchsia_component::client as fclient;
use fuchsia_zircon as zx;
use rand::Rng;
use std::ffi::CString;
use std::sync::Arc;
use tracing::info;

use crate::auth::Credentials;
use crate::execution::{
    create_filesystem_from_spec, execute_task, galaxy::Galaxy, parse_numbered_handles,
};
use crate::task::*;
use crate::types::*;

/// Starts a component in an isolated environment, called a "galaxy".
///
/// The galaxy will be configured according to a configuration file in the Starnix runner's package.
/// The configuration file specifies, for example, which binary to run as "init", whether or not the
/// system should wait for the existence of a given file path to run the component, etc.
///
/// The Starnix runner's package also contains the system image to mount.
///
/// The component's `binary` can either:
///   - an absolute path, in which case the path is treated as a path into the root filesystem that
///     is mounted by the galaxy's configuration
///   - relative path, in which case the binary is read from the component's package (which is
///     mounted at /data/pkg.)
pub async fn start_component(
    mut start_info: ComponentStartInfo,
    controller: ServerEnd<ComponentControllerMarker>,
    galaxy: Arc<Galaxy>,
) -> Result<(), Error> {
    let url = start_info.resolved_url.clone().unwrap_or("<unknown>".to_string());
    info!(
        "start_component: {}\narguments: {:?}\nmanifest: {:?}",
        url.clone(),
        start_info.numbered_handles,
        start_info.program,
    );

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
    info!("start_component environment: {:?}", environ);

    const COMPONENT_PKG_DIRECTORY: &str = "/data/pkg/";
    let binary_path = get_program_string(&start_info, "binary")
        .ok_or_else(|| anyhow!("Missing \"binary\" in manifest"))?;
    let binary_in_package = &binary_path[..1] != "/";
    let binary_path = CString::new(if binary_in_package {
        // If the binary path is relative, treat it as a path into the component's package
        // directory.
        COMPONENT_PKG_DIRECTORY.to_owned() + binary_path
    } else {
        // If the binary path is absolute, treat it as a path to an existing binary.
        binary_path.to_owned()
    })?;
    let mut current_task = Task::create_process_without_parent(
        &galaxy.kernel,
        binary_path.clone(),
        galaxy.root_fs.clone(),
    )?;
    let user_passwd = get_program_string(&start_info, "user").unwrap_or("fuchsia:x:42:42");
    let credentials = Credentials::from_passwd(user_passwd)?;
    current_task.write().creds = credentials;

    let ns = start_info.ns.take().ok_or_else(|| anyhow!("Missing namespace"))?;
    let pkg = fio::DirectorySynchronousProxy::new(
        ns.into_iter()
            .find(|entry| entry.path == Some("/pkg".to_string()))
            .ok_or_else(|| anyhow!("Missing /pkg entry in namespace"))?
            .directory
            .ok_or_else(|| anyhow!("Missing directory handlee in pkg namespace entry"))?
            .into_channel(),
    );

    if binary_in_package {
        // If the component's binary path point inside the package, mount the package directory.
        mount_component_pkg_data(&current_task, &galaxy, &COMPONENT_PKG_DIRECTORY, &pkg)?;
    }

    let startup_handles =
        parse_numbered_handles(start_info.numbered_handles, &current_task.files, &galaxy.kernel)?;
    let shell_controller = startup_handles.shell_controller;

    let mut argv = vec![binary_path];
    argv.extend(args.into_iter());

    current_task.exec(argv[0].clone(), argv.clone(), environ.clone())?;

    // run per-component features
    // TODO(fxb/100316) - we should examine start_info to determine which features are needed for this component.
    run_component_features(&galaxy.kernel.features, &current_task, &mut start_info.outgoing_dir)
        .unwrap_or_else(|e| {
            tracing::error!("failed to set component features for {} - {:?}", url, e);
        });

    execute_task(current_task, |result| {
        // TODO(fxb/74803): Using the component controller's epitaph may not be the best way to
        // communicate the exit status. The component manager could interpret certain epitaphs as starnix
        // being unstable, and chose to terminate starnix as a result.
        // Errors when closing the controller with an epitaph are disregarded, since there are
        // legitimate reasons for this to fail (like the client having closed the channel).
        if let Some(shell_controller) = shell_controller {
            let _ = shell_controller.close_with_epitaph(zx::Status::OK);
        }
        let _ = match result {
            Ok(ExitStatus::Exit(0)) => controller.close_with_epitaph(zx::Status::OK),
            _ => controller.close_with_epitaph(zx::Status::from_raw(
                fcomponent::Error::InstanceDied.into_primitive() as i32,
            )),
        };
    });

    Ok(())
}

/// Attempts to mount the component's package directory at `package_directory_path` in the task's
/// filesystem. This allows components to bundle their own binary in their package, instead of
/// relying on it existing in the system image of the galaxy.
fn mount_component_pkg_data(
    current_task: &CurrentTask,
    galaxy: &Galaxy,
    package_directory_path: &str,
    package_proxy: &fio::DirectorySynchronousProxy,
) -> Result<(), Error> {
    let remotefs_mount_point = package_directory_path.to_owned() + ":remotefs";
    let (mount_point, child_fs) = create_filesystem_from_spec(
        &galaxy.kernel,
        Some(&current_task),
        &package_proxy,
        &remotefs_mount_point,
    )?;
    let mount_point = current_task.lookup_path_from_root(mount_point)?;
    mount_point.mount(child_fs, MountFlags::empty())?;

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
