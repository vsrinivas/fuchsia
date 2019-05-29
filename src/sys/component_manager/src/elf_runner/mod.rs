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
    fuchsia_runtime::{job_default, HandleInfo, HandleType},
    fuchsia_zircon::{self as zx, HandleBased},
    futures::future::FutureObj,
    std::path::PathBuf,
};

/// Runs components with ELF binaries.
pub struct ElfRunner {}

fn get_resolved_url(start_info: &fsys::ComponentStartInfo) -> Result<String, Error> {
    match &start_info.resolved_url {
        Some(url) => Ok(url.to_string()),
        _ => Err(err_msg("missing url")),
    }
}

fn get_program_binary(start_info: &fsys::ComponentStartInfo) -> Result<String, Error> {
    if let Some(program) = &start_info.program {
        if let Some(binary) = program.find("binary") {
            if let fdata::Value::Str(bin) = binary {
                return Ok(bin.to_string());
            }
        }
    }
    Err(err_msg("\"binary\" must be specified"))
}

fn get_program_args(start_info: &fsys::ComponentStartInfo) -> Result<Vec<String>, Error> {
    if let Some(program) = &start_info.program {
        if let Some(args) = program.find("args") {
            if let fdata::Value::Vec(vec) = args {
                return vec
                    .values
                    .iter()
                    .map(|v| {
                        if let Some(fdata::Value::Str(a)) = v.as_ref().map(|x| &**x) {
                            Ok(a.clone())
                        } else {
                            Err(err_msg("invalid type in arguments"))
                        }
                    })
                    .collect();
            }
        }
    }
    Ok(vec![])
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
            id: HandleInfo::new(HandleType::FileDescriptor, fd as u16).as_raw(),
        }))
    }
}

async fn load_launch_info(
    url: String,
    start_info: fsys::ComponentStartInfo,
    launcher: &fproc::LauncherProxy,
) -> Result<fproc::LaunchInfo, Error> {
    let bin_path =
        get_program_binary(&start_info).map_err(|e| RunnerError::invalid_args(url.as_ref(), e))?;
    let bin_arg =
        &[String::from(PKG_PATH.join(&bin_path).to_str().ok_or(err_msg("invalid binary path"))?)];
    let args = get_program_args(&start_info)?;

    let name = PathBuf::from(url)
        .file_name()
        .ok_or(err_msg("invalid url"))?
        .to_str()
        .ok_or(err_msg("invalid url"))?
        .to_string();

    // Make a non-Option namespace
    let ns =
        start_info.ns.unwrap_or(fsys::ComponentNamespace { paths: vec![], directories: vec![] });

    // Load in a VMO holding the target executable from the namespace
    let (mut ns, ns_clone) = ns_util::clone_component_namespace(ns)?;
    let ns_map = ns_util::ns_to_map(ns_clone)?;

    // Start the library loader service
    let (ll_client_chan, ll_service_chan) = zx::Channel::create()?;
    let pkg_proxy = ns_map.get(&*PKG_PATH).ok_or(err_msg("/pkg missing from namespace"))?;
    let lib_proxy = io_util::open_directory(pkg_proxy, &PathBuf::from("lib"))?;

    // The loader service should only be able to load files from `/pkg/lib` and `/pkg/bin`. Giving
    // it a larger scope is potentially a security vulnerability, as it could make it trivial for
    // parts of applications to get handles to things the application author didn't intend.
    library_loader::start(lib_proxy, ll_service_chan);

    let executable_vmo = await!(library_loader::load_vmo(pkg_proxy, bin_path))
        .context("error loading executable")?;

    let child_job = job_default().create_child_job()?;

    let child_job_dup = child_job.duplicate_handle(zx::Rights::SAME_RIGHTS)?;

    let mut string_iters: Vec<_> = bin_arg.iter().chain(args.iter()).map(|s| s.bytes()).collect();
    launcher.add_args(
        &mut string_iters.iter_mut().map(|iter| iter as &mut dyn ExactSizeIterator<Item = u8>),
    )?;
    // TODO: launcher.AddEnvirons

    let mut handle_infos = vec![];
    for fd in 0..3 {
        handle_infos.extend(handle_info_from_fd(fd)?);
    }

    handle_infos.append(&mut vec![
        fproc::HandleInfo {
            handle: ll_client_chan.into_handle(),
            id: HandleInfo::new(HandleType::LdsvcLoader, 0).as_raw(),
        },
        fproc::HandleInfo {
            handle: child_job_dup.into_handle(),
            id: HandleInfo::new(HandleType::DefaultJob, 0).as_raw(),
        },
    ]);
    if let Some(outgoing_dir) = start_info.outgoing_dir {
        handle_infos.push(fproc::HandleInfo {
            handle: outgoing_dir.into_handle(),
            id: HandleInfo::new(HandleType::DirectoryRequest, 0).as_raw(),
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
        let resolved_url =
            get_resolved_url(&start_info).map_err(|e| RunnerError::invalid_args("", e))?;

        let launcher = connect_to_service::<fproc::LauncherMarker>()
            .context("failed to connect to launcher service")
            .map_err(|e| RunnerError::component_load_error(resolved_url.as_ref(), e))?;

        // Load the component
        let mut launch_info = await!(load_launch_info(resolved_url.clone(), start_info, &launcher))
            .map_err(|e| RunnerError::component_load_error(resolved_url.as_ref(), e))?;

        // Launch the component
        await!(async {
            let (status, _process) = await!(launcher.launch(&mut launch_info))?;
            if zx::Status::from_raw(status) != zx::Status::OK {
                return Err(format_err!("failed to launch component: {}", status));
            }
            Ok(())
        })
        .map_err(|e| RunnerError::component_launch_error(resolved_url, e))?;

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
    use {crate::elf_runner::*, fidl::endpoints::ClientEnd, fuchsia_async as fasync, io_util};

    #[test]
    fn hello_world_test() {
        let mut executor = fasync::Executor::new().unwrap();
        executor.run_singlethreaded(async {
            // Get a handle to /pkg
            let pkg_path = "/pkg".to_string();
            let pkg_chan = io_util::open_directory_in_namespace("/pkg")
                .unwrap()
                .into_channel()
                .unwrap()
                .into_zx_channel();
            let pkg_handle = ClientEnd::new(pkg_chan);

            let ns =
                fsys::ComponentNamespace { paths: vec![pkg_path], directories: vec![pkg_handle] };

            let start_info = fsys::ComponentStartInfo {
                resolved_url: Some(
                    "fuchsia-pkg://fuchsia.com/hello_world_hippo#meta/hello_world.cm".to_string(),
                ),
                program: Some(fdata::Dictionary {
                    entries: vec![fdata::Entry {
                        key: "binary".to_string(),
                        value: Some(Box::new(fdata::Value::Str("bin/hello_world".to_string()))),
                    }],
                }),
                ns: Some(ns),
                outgoing_dir: None,
            };

            let runner = ElfRunner::new();
            await!(runner.start_async(start_info)).unwrap();
        });
    }

    fn new_args_set(args: Vec<Option<Box<fdata::Value>>>) -> fsys::ComponentStartInfo {
        fsys::ComponentStartInfo {
            program: Some(fdata::Dictionary {
                entries: vec![fdata::Entry {
                    key: "args".to_string(),
                    value: Some(Box::new(fdata::Value::Vec(fdata::Vector { values: args }))),
                }],
            }),
            ns: None,
            outgoing_dir: None,
            resolved_url: None,
        }
    }

    #[test]
    fn get_program_args_test() {
        let e: Vec<String> = vec![];

        assert_eq!(
            e,
            get_program_args(&fsys::ComponentStartInfo {
                program: Some(fdata::Dictionary { entries: vec![] }),
                ns: None,
                outgoing_dir: None,
                resolved_url: None,
            })
            .unwrap()
        );

        assert_eq!(e, get_program_args(&new_args_set(vec![])).unwrap());

        assert_eq!(
            vec!["a".to_string()],
            get_program_args(&new_args_set(vec![Some(Box::new(fdata::Value::Str(
                "a".to_string()
            )))]))
            .unwrap()
        );

        assert_eq!(
            vec!["a".to_string(), "b".to_string()],
            get_program_args(&new_args_set(vec![
                Some(Box::new(fdata::Value::Str("a".to_string()))),
                Some(Box::new(fdata::Value::Str("b".to_string()))),
            ]))
            .unwrap()
        );

        assert_eq!(
            format!("{:?}", err_msg("invalid type in arguments")),
            format!(
                "{:?}",
                get_program_args(&new_args_set(vec![
                    Some(Box::new(fdata::Value::Str("a".to_string()))),
                    None,
                ]))
                .unwrap_err()
            )
        );

        assert_eq!(
            format!("{:?}", err_msg("invalid type in arguments")),
            format!(
                "{:?}",
                get_program_args(&new_args_set(vec![Some(Box::new(fdata::Value::Inum(1))),]))
                    .unwrap_err()
            )
        );
    }
}
