// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_io::{DirectoryProxy, CLONE_FLAG_SAME_RIGHTS, VMO_FLAG_READ},
    fidl_fuchsia_ldsvc::{LoaderRequest, LoaderRequestStream},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{TryFutureExt, TryStreamExt},
    io_util,
    log::*,
    std::path::Path,
};

/// start will expose the `fuchsia.ldsvc.Loader` service over the given channel, providing VMO
/// buffers of requested library object names from `lib_proxy`.
pub fn start(lib_proxy: DirectoryProxy, chan: zx::Channel) {
    fasync::spawn(
        async move {
            let mut search_dirs =
                vec![io_util::clone_directory(&lib_proxy, CLONE_FLAG_SAME_RIGHTS)?];
            // Wait for requests
            let mut stream =
                LoaderRequestStream::from_channel(fasync::Channel::from_channel(chan)?);
            'request_loop: while let Some(req) = stream.try_next().await? {
                match req {
                    LoaderRequest::Done { control_handle } => {
                        control_handle.shutdown();
                    }
                    LoaderRequest::LoadObject { object_name, responder } => {
                        // TODO(ZX-3392): The name provided by the client here has a null byte at
                        // the end, which doesn't work from here on out (io.fidl doesn't like it).
                        let object_name = object_name.trim_matches(char::from(0)).to_string();
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
                            io_util::clone_directory(&lib_proxy, CLONE_FLAG_SAME_RIGHTS)?;
                        start(new_lib_proxy, loader.into_channel());
                        responder.send(zx::sys::ZX_OK)?;
                    }
                }
            }
            Ok(())
        }
        .unwrap_or_else(|e: Error| warn!("couldn't run library loader service: {}", e)),
    );
}

/// load_vmo will attempt to open the provided name in `dir_proxy` and return an executable VMO
/// with the contents.
pub async fn load_vmo<'a>(
    dir_proxy: &'a DirectoryProxy,
    object_name: &'a str,
) -> Result<zx::Vmo, Error> {
    let file_proxy =
        io_util::open_file(dir_proxy, &Path::new(object_name), io_util::OPEN_RIGHT_READABLE)?;
    let (status, fidlbuf) = file_proxy
        .get_buffer(VMO_FLAG_READ)
        .await
        .map_err(|e| format_err!("reading object at {:?} failed: {}", object_name, e))?;
    let status = zx::Status::from_raw(status);
    if status != zx::Status::OK {
        return Err(format_err!("reading object at {:?} failed: {}", object_name, status));
    }
    fidlbuf
        .ok_or(format_err!("no buffer returned from GetBuffer"))?
        .vmo
        .replace_as_executable()
        .map_err(|status| format_err!("failed to replace VMO as executable: {}", status))
}

/// parses a config string from the `fuchsia.ldsvc.Loader` service. See
/// `//docs/zircon/program_loading.md` for a description of the format. Returns the set of
/// directories which should be searched for objects.
fn parse_config_string(
    dir_proxy: &DirectoryProxy,
    config: &str,
) -> Result<Vec<DirectoryProxy>, Error> {
    if config.contains("/") {
        return Err(format_err!("'/' chacter found in loader service config string"));
    }
    if Some('!') == config.chars().last() {
        let sub_dir_proxy = io_util::open_directory(
            dir_proxy,
            &Path::new(&config[..config.len() - 1]),
            io_util::OPEN_RIGHT_READABLE,
        )?;
        Ok(vec![sub_dir_proxy])
    } else {
        let dir_proxy_clone = io_util::clone_directory(dir_proxy, CLONE_FLAG_SAME_RIGHTS)?;
        let sub_dir_proxy =
            io_util::open_directory(dir_proxy, &Path::new(config), io_util::OPEN_RIGHT_READABLE)?;
        Ok(vec![sub_dir_proxy, dir_proxy_clone])
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{Proxy, ServerEnd},
        fidl_fuchsia_io::{DirectoryMarker, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE},
        fidl_fuchsia_ldsvc::{LoaderMarker, LoaderProxy},
        fuchsia_vfs_pseudo_fs::{
            directory::entry::DirectoryEntry, file::simple::read_only, pseudo_directory,
        },
        std::iter,
        std::path::Path,
    };

    #[fasync::run_singlethreaded(test)]
    // TODO: Use a synthetic /pkg/lib in this test so it doesn't depend on the package layout
    async fn load_objects_test() -> Result<(), Error> {
        let mut pkg_lib = io_util::open_directory_in_namespace("/pkg/lib", OPEN_RIGHT_READABLE)?;
        let entries = list_directory(&pkg_lib).await;
        if entries.iter().any(|f| &f as &str == "asan") {
            pkg_lib = io_util::open_directory(&pkg_lib, &Path::new("asan"), OPEN_RIGHT_READABLE)?;
        }
        let (client_chan, service_chan) = zx::Channel::create()?;
        start(pkg_lib, service_chan);

        let loader = LoaderProxy::from_channel(fasync::Channel::from_channel(client_chan)?);

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
            // Should not be able to access meta/component_manager_tests_hello_world.cm
            ("../meta/component_manager_tests_hello_world.cm", false),
        ] {
            let (res, o_vmo) = loader.load_object(obj_name).await?;
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

    async fn list_directory<'a>(root_proxy: &'a DirectoryProxy) -> Vec<String> {
        let dir = io_util::clone_directory(&root_proxy, CLONE_FLAG_SAME_RIGHTS)
            .expect("Failed to clone DirectoryProxy");
        let entries = files_async::readdir(&dir).await.expect("readdir failed");
        entries.iter().map(|entry| entry.name.clone()).collect::<Vec<String>>()
    }

    #[fasync::run_singlethreaded(test)]
    async fn config_test() -> Result<(), Error> {
        let mut example_dir = pseudo_directory! {
            "foo" => read_only(|| Ok(b"hippos".to_vec())),
            "bar" => pseudo_directory! {
                "baz" => read_only(|| Ok(b"rule".to_vec())),
            },
        };

        let (example_dir_proxy, example_dir_service) =
            fidl::endpoints::create_proxy::<DirectoryMarker>()?;
        example_dir.open(
            OPEN_RIGHT_READABLE,
            MODE_TYPE_DIRECTORY,
            &mut iter::empty(),
            ServerEnd::new(example_dir_service.into_channel()),
        );
        fasync::spawn(async move {
            let _ = example_dir.await;
        });

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
            let example_dir_proxy_clone =
                io_util::clone_directory(&example_dir_proxy, CLONE_FLAG_SAME_RIGHTS)?;

            let (loader_proxy, loader_service) = fidl::endpoints::create_proxy::<LoaderMarker>()?;
            start(example_dir_proxy_clone, loader_service.into_channel());

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
