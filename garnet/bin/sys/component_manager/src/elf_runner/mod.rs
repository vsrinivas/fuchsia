// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod library_loader;

use {
    crate::model::{Runner, RunnerError},
    crate::ns_util::{self, PKG_PATH},
    cm_rust::data::DictionaryExt,
    failure::{err_msg, format_err, Error, ResultExt},
    fdio::fdio_sys,
    fidl_fuchsia_data as fdata, fidl_fuchsia_process as fproc, fidl_fuchsia_sys2 as fsys,
    fuchsia_component::client::connect_to_service,
    fuchsia_runtime::{create_handle_id, job_default, HandleType},
    fuchsia_zircon::{self as zx, HandleBased},
    futures::future::FutureObj,
    std::path::PathBuf,
};

/// Runs components with ELF binaries.
pub struct ElfRunner {}

fn get_resolved_uri(start_info: &fsys::ComponentStartInfo) -> Result<String, Error> {
    match &start_info.resolved_uri {
        Some(uri) => Ok(uri.to_string()),
        _ => Err(err_msg("missing uri")),
    }
}

fn get_program_binary(start_info: &fsys::ComponentStartInfo) -> Result<PathBuf, Error> {
    if let Some(program) = &start_info.program {
        if let Some(binary) = program.find("binary") {
            if let fdata::Value::Str(bin) = binary {
                return Ok(PKG_PATH.join(bin));
            }
        }
    }
    Err(err_msg("\"binary\" must be specified"))
}

fn handle_info_from_fd(fd: i32) -> Result<Option<fproc::HandleInfo>, Error> {
    // TODO(CF-592): fdio is not guaranteed to be asynchronous, replace with native rust solution
    unsafe {
        let mut fd_handle = zx::sys::ZX_HANDLE_INVALID;
        let status = fdio_sys::fdio_fd_clone(fd, &mut fd_handle as *mut zx::sys::zx_handle_t);
        if status == zx::sys::ZX_ERR_INVALID_ARGS {
            // This file descriptor is closed. We just skip it rather than
            // generating an error.
            return Ok(None);
        }
        if status != zx::sys::ZX_OK {
            return Err(format_err!("failed to clone fd {}: {}", fd, status));
        }
        Ok(Some(fproc::HandleInfo {
            handle: zx::Handle::from_raw(fd_handle),
            id: create_handle_id(HandleType::FileDescriptor, fd as u16),
        }))
    }
}

async fn load_launch_info(
    uri: String,
    bin_path: PathBuf,
    start_info: fsys::ComponentStartInfo,
    launcher: &fproc::LauncherProxy,
) -> Result<fproc::LaunchInfo, Error> {
    let name = PathBuf::from(uri)
        .file_name()
        .ok_or(err_msg("invalid uri"))?
        .to_str()
        .ok_or(err_msg("invalid uri"))?
        .to_string();

    // Make a non-Option namespace
    let ns =
        start_info.ns.unwrap_or(fsys::ComponentNamespace { paths: vec![], directories: vec![] });

    // Load in a VMO holding the target executable from the namespace
    let (mut ns, ns_clone) = ns_util::clone_component_namespace(ns)?;
    let ns_map = ns_util::ns_to_map(ns_clone)?;

    let executable_vmo = await!(library_loader::load_object(&ns_map, bin_path))
        .context("error loading executable")?;

    let child_job = job_default().create_child_job()?;

    let child_job_dup = child_job.duplicate_handle(zx::Rights::SAME_RIGHTS)?;

    // Start the library loader service
    let (ll_client_chan, ll_service_chan) = zx::Channel::create()?;
    library_loader::start(ns_map, ll_service_chan);

    // TODO: launcher.AddArgs
    // TODO: launcher.AddEnvirons

    let mut handle_infos = vec![];
    for fd in 0..3 {
        handle_infos.extend(handle_info_from_fd(fd)?);
    }

    handle_infos.append(&mut vec![
        fproc::HandleInfo {
            handle: ll_client_chan.into_handle(),
            id: create_handle_id(HandleType::LdsvcLoader, 0),
        },
        fproc::HandleInfo {
            handle: child_job_dup.into_handle(),
            id: create_handle_id(HandleType::JobDefault, 0),
        },
    ]);
    if let Some(outgoing_dir) = start_info.outgoing_dir {
        handle_infos.push(fproc::HandleInfo {
            handle: outgoing_dir.into_handle(),
            id: create_handle_id(HandleType::DirectoryRequest, 0),
        });
    }
    launcher.add_handles(&mut handle_infos.iter_mut())?;

    let mut name_infos = vec![];
    while let Some(path) = ns.paths.pop() {
        if let Some(directory) = ns.directories.pop() {
            name_infos.push(fproc::NameInfo { path, directory })
        }
    }
    launcher.add_names(&mut name_infos.iter_mut())?;
    Ok(fproc::LaunchInfo { executable: executable_vmo, job: child_job, name: name })
}

impl ElfRunner {
    pub fn new() -> ElfRunner {
        ElfRunner {}
    }

    async fn start_async(&self, start_info: fsys::ComponentStartInfo) -> Result<(), RunnerError> {
        let resolved_uri =
            get_resolved_uri(&start_info).map_err(|e| RunnerError::invalid_args("", e))?;
        let bin_path: PathBuf = get_program_binary(&start_info)
            .map_err(|e| RunnerError::invalid_args(resolved_uri.as_ref(), e))?;

        let launcher = connect_to_service::<fproc::LauncherMarker>()
            .context("failed to connect to launcher service")
            .map_err(|e| RunnerError::component_load_error(resolved_uri.as_ref(), e))?;

        // Load the component
        let mut launch_info =
            await!(load_launch_info(resolved_uri.clone(), bin_path, start_info, &launcher))
                .map_err(|e| RunnerError::component_load_error(resolved_uri.as_ref(), e))?;

        // Launch the component
        await!(
            async {
                let (status, _process) = await!(launcher.launch(&mut launch_info))?;
                if zx::Status::from_raw(status) != zx::Status::OK {
                    return Err(format_err!("failed to launch component: {}", status));
                }
                Ok(())
            }
        )
        .map_err(|e| RunnerError::component_launch_error(resolved_uri, e))?;

        Ok(())
    }
}

impl Runner for ElfRunner {
    fn start(&self, start_info: fsys::ComponentStartInfo) -> FutureObj<Result<(), RunnerError>> {
        FutureObj::new(Box::new(self.start_async(start_info)))
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::elf_runner::*, crate::io_util, fidl::endpoints::ClientEnd, fuchsia_async as fasync,
    };

    #[test]
    fn hello_world_test() {
        let mut executor = fasync::Executor::new().unwrap();
        executor.run_singlethreaded(
            async {
                // Get a handle to /bin
                let bin_path = "/pkg/bin".to_string();
                let bin_proxy = io_util::open_directory_in_namespace("/pkg/bin").unwrap();
                let bin_chan = bin_proxy.into_channel().unwrap();
                let bin_handle = ClientEnd::new(bin_chan.into_zx_channel());

                // Get a handle to /lib
                let lib_path = "/pkg/lib".to_string();
                let lib_proxy = io_util::open_directory_in_namespace("/pkg/lib").unwrap();
                let lib_chan = lib_proxy.into_channel().unwrap();
                let lib_handle = ClientEnd::new(lib_chan.into_zx_channel());

                let ns = fsys::ComponentNamespace {
                    paths: vec![lib_path, bin_path],
                    directories: vec![lib_handle, bin_handle],
                };

                let start_info = fsys::ComponentStartInfo {
                    resolved_uri: Some(
                        "fuchsia-pkg://fuchsia.com/hello_world_hippo#meta/hello_world.cm"
                            .to_string(),
                    ),
                    program: Some(fdata::Dictionary {
                        entries: vec![fdata::Entry {
                            key: "binary".to_string(),
                            value: Some(Box::new(fdata::Value::Str(
                                "/pkg/bin/hello_world".to_string(),
                            ))),
                        }],
                    }),
                    ns: Some(ns),
                    outgoing_dir: None,
                };

                let runner = ElfRunner::new();
                await!(runner.start_async(start_info)).unwrap();
            },
        );
    }
}
