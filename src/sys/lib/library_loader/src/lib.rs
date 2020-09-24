// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_io::{self as fio, DirectoryProxy},
    fidl_fuchsia_ldsvc::{LoaderRequest, LoaderRequestStream},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{TryFutureExt, TryStreamExt},
    io_util,
    log::*,
    std::path::Path,
};

/// start will expose the `fuchsia.ldsvc.Loader` service over the given channel, providing VMO
/// buffers of requested library object names from `lib_proxy`.
///
/// `lib_proxy` must have been opened with at minimum OPEN_RIGHT_READABLE and OPEN_RIGHT_EXECUTABLE
/// rights.
pub fn start(lib_proxy: DirectoryProxy, chan: zx::Channel) {
    fasync::Task::spawn(
        async move {
            let mut search_dirs =
                vec![io_util::clone_directory(&lib_proxy, fio::CLONE_FLAG_SAME_RIGHTS)?];
            // Wait for requests
            let mut stream =
                LoaderRequestStream::from_channel(fasync::Channel::from_channel(chan)?);
            'request_loop: while let Some(req) = stream.try_next().await? {
                match req {
                    LoaderRequest::Done { control_handle } => {
                        control_handle.shutdown();
                    }
                    LoaderRequest::LoadObject { object_name, responder } => {
                        let mut errors = vec![];
                        for dir_proxy in &search_dirs {
                            match load_vmo(dir_proxy, &object_name).await {
                                Ok(b) => {
                                    responder.send(zx::sys::ZX_OK, Some(b))?;
                                    continue 'request_loop;
                                }
                                Err(e) => errors.push(e),
                            }
                        }
                        warn!("failed to load object: {:?}", errors);
                        responder.send(zx::sys::ZX_ERR_NOT_FOUND, None)?;
                    }
                    LoaderRequest::Config { config, responder } => {
                        match parse_config_string(&lib_proxy, &config) {
                            Ok(new_search_path) => {
                                search_dirs = new_search_path;
                                responder.send(zx::sys::ZX_OK)?;
                            }
                            Err(e) => {
                                warn!("failed to parse config: {}", e);
                                responder.send(zx::sys::ZX_ERR_INVALID_ARGS)?;
                            }
                        }
                    }
                    LoaderRequest::Clone { loader, responder } => {
                        let new_lib_proxy =
                            io_util::clone_directory(&lib_proxy, fio::CLONE_FLAG_SAME_RIGHTS)?;
                        start(new_lib_proxy, loader.into_channel());
                        responder.send(zx::sys::ZX_OK)?;
                    }
                }
            }
            Ok(())
        }
        .unwrap_or_else(|e: Error| warn!("couldn't run library loader service: {}", e)),
    )
    .detach();
}

/// load_vmo will attempt to open the provided name in `dir_proxy` and return an executable VMO
/// with the contents.
///
/// `dir_proxy` must have been opened with at minimum OPEN_RIGHT_READABLE and OPEN_RIGHT_EXECUTABLE
/// rights.
pub async fn load_vmo<'a>(
    dir_proxy: &'a DirectoryProxy,
    object_name: &'a str,
) -> Result<zx::Vmo, Error> {
    // TODO(fxbug.dev/52468): This does not ask or wait for a Describe event, which means a failure to
    // open the file will appear as a PEER_CLOSED on the get_buffer call. It also means this could
    // be a Vmofile node and that we're relying on it still supporting the File protocol.
    let file_proxy = io_util::open_file(
        dir_proxy,
        &Path::new(object_name),
        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
    )?;
    let (status, fidlbuf) = file_proxy
        .get_buffer(fio::VMO_FLAG_READ | fio::VMO_FLAG_EXEC)
        .await
        .map_err(|e| format_err!("reading object at {:?} failed: {}", object_name, e))?;
    let status = zx::Status::from_raw(status);
    if status != zx::Status::OK {
        return Err(format_err!("reading object at {:?} failed: {}", object_name, status));
    }
    Ok(fidlbuf.ok_or(format_err!("no buffer returned from GetBuffer"))?.vmo)
}

/// parses a config string from the `fuchsia.ldsvc.Loader` service. See
/// `//docs/concepts/booting/program_loading.md` for a description of the format. Returns the set
/// of directories which should be searched for objects.
fn parse_config_string(
    dir_proxy: &DirectoryProxy,
    config: &str,
) -> Result<Vec<DirectoryProxy>, Error> {
    if config.contains("/") {
        return Err(format_err!("'/' character found in loader service config string"));
    }
    if Some('!') == config.chars().last() {
        let sub_dir_proxy = io_util::open_directory(
            dir_proxy,
            &Path::new(&config[..config.len() - 1]),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        )?;
        Ok(vec![sub_dir_proxy])
    } else {
        let dir_proxy_clone = io_util::clone_directory(dir_proxy, fio::CLONE_FLAG_SAME_RIGHTS)?;
        let sub_dir_proxy = io_util::open_directory(
            dir_proxy,
            &Path::new(config),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        )?;
        Ok(vec![sub_dir_proxy, dir_proxy_clone])
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_ldsvc::LoaderMarker};

    async fn list_directory<'a>(root_proxy: &'a DirectoryProxy) -> Vec<String> {
        let dir = io_util::clone_directory(&root_proxy, fio::CLONE_FLAG_SAME_RIGHTS)
            .expect("Failed to clone DirectoryProxy");
        let entries = files_async::readdir(&dir).await.expect("readdir failed");
        entries.iter().map(|entry| entry.name.clone()).collect::<Vec<String>>()
    }

    #[fasync::run_singlethreaded(test)]
    async fn load_objects_test() -> Result<(), Error> {
        // Open this test's real /pkg/lib directory to use for this test, and then check to see
        // whether an asan subdirectory is present, and use it instead if so.
        // TODO(fxbug.dev/37534): Use a synthetic /pkg/lib in this test so it doesn't depend on the
        // package layout (like whether sanitizers are in use) once Rust vfs supports
        // OPEN_RIGHT_EXECUTABLE
        let rights = fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE;
        let mut pkg_lib = io_util::open_directory_in_namespace("/pkg/lib", rights)?;
        let entries = list_directory(&pkg_lib).await;
        if entries.iter().any(|f| &f as &str == "asan-ubsan") {
            pkg_lib = io_util::open_directory(&pkg_lib, &Path::new("asan-ubsan"), rights)?;
        } else if entries.iter().any(|f| &f as &str == "asan") {
            pkg_lib = io_util::open_directory(&pkg_lib, &Path::new("asan"), rights)?;
        } else if entries.iter().any(|f| &f as &str == "profile") {
            pkg_lib = io_util::open_directory(&pkg_lib, &Path::new("profile"), rights)?;
        }

        let (loader_proxy, loader_service) = fidl::endpoints::create_proxy::<LoaderMarker>()?;
        start(pkg_lib, loader_service.into_channel());

        for (obj_name, should_succeed) in vec![
            // Should be able to access lib/ld.so.1
            ("ld.so.1", true),
            // Should be able to access lib/libfdio.so
            ("libfdio.so", true),
            // Should not be able to access lib/lib/ld.so.1
            ("lib/ld.so.1", false),
            // Should not be able to access lib/../lib/ld.so.1
            ("../lib/ld.so.1", false),
            // Should not be able to access test/component_manager_tests
            ("../test/component_manager_tests", false),
            // Should not be able to access lib/bin/hello_world
            ("bin/hello_world", false),
            // Should not be able to access bin/hello_world
            ("../bin/hello_world", false),
            // Should not be able to access meta/hello_world.cm
            ("../meta/hello_world.cm", false),
        ] {
            let (res, o_vmo) = loader_proxy.load_object(obj_name).await?;
            if should_succeed {
                assert_eq!(zx::sys::ZX_OK, res, "loading {} did not succeed", obj_name);
                assert!(o_vmo.is_some());
            } else {
                assert_ne!(zx::sys::ZX_OK, res, "loading {} did not fail", obj_name);
                assert!(o_vmo.is_none());
            }
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn config_test() -> Result<(), Error> {
        // This /pkg/lib/config_test/ directory is added by the build rules for this test package,
        // since we need a directory that supports OPEN_RIGHT_EXECUTABLE. It contains a file 'foo'
        // which contains 'hippos' and a file 'bar/baz' (that is, baz in a subdirectory bar) which
        // contains 'rule'.
        // TODO(fxbug.dev/37534): Use a synthetic /pkg/lib in this test so it doesn't depend on the
        // package layout once Rust vfs supports OPEN_RIGHT_EXECUTABLE
        let pkg_lib = io_util::open_directory_in_namespace(
            "/pkg/lib/config_test/",
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        )?;
        let (loader_proxy, loader_service) = fidl::endpoints::create_proxy::<LoaderMarker>()?;
        start(pkg_lib, loader_service.into_channel());

        // Attempt to access things with different configurations
        for (obj_name, config, expected_result) in vec![
            // Should be able to load foo
            ("foo", None, Some("hippos")),
            // Should not be able to load bar (it's a directory)
            ("bar", None, None),
            // Should not be able to load baz (it's in a sub directory)
            ("baz", None, None),
            // Should be able to load baz with config "bar!" (only look in sub directory bar)
            ("baz", Some("bar!"), Some("rule")),
            // Should not be able to load foo with config "bar!" (only look in sub directory bar)
            ("foo", Some("bar!"), None),
            // Should be able to load foo with config "bar" (also look in sub directory bar)
            ("foo", Some("bar"), Some("hippos")),
            // Should be able to load baz with config "bar" (also look in sub directory bar)
            ("baz", Some("bar"), Some("rule")),
        ] {
            if let Some(config) = config {
                assert_eq!(zx::sys::ZX_OK, loader_proxy.config(config).await?);
            }

            let (res, o_vmo) = loader_proxy.load_object(obj_name).await?;
            if let Some(expected_result) = expected_result {
                assert_eq!(zx::sys::ZX_OK, res);
                let mut buf = vec![0; expected_result.len()];
                o_vmo.ok_or(format_err!("missing vmo"))?.read(&mut buf, 0)?;
                assert_eq!(expected_result.as_bytes(), buf.as_slice());
            } else {
                assert_ne!(zx::sys::ZX_OK, res);
                assert!(o_vmo.is_none());
            }
        }
        Ok(())
    }
}
