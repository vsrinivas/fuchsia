// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Context,
    fidl::endpoints::ControlHandle,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_ldsvc::{LoaderMarker, LoaderRequest},
    fidl_fuchsia_test_runner as ftestrunner,
    ftestrunner::LibraryLoaderCacheMarker,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::lock::Mutex as FutMutex,
    futures::prelude::*,
    std::collections::HashMap,
    std::sync::{Arc, Weak},
    tracing::warn,
};

/// maps vmo key with vmo result.
type VmoKeyMap = HashMap<String, (i32, Option<zx::Vmo>)>;

#[derive(Debug)]
pub struct LibraryLoaderCache {
    /// Proxy to /pkg/lib
    lib_proxy: Arc<fio::DirectoryProxy>,

    /// Mapping of config key with loaded VMOs map.
    load_response_map: FutMutex<HashMap<String, Arc<FutMutex<VmoKeyMap>>>>,
}

impl LibraryLoaderCache {
    pub fn new(lib_proxy: Arc<fio::DirectoryProxy>) -> Arc<Self> {
        return Arc::new(Self { lib_proxy, load_response_map: FutMutex::new(HashMap::new()) });
    }
}

pub async fn serve_cache(
    cache: Arc<LibraryLoaderCache>,
    server_end: ServerEnd<LibraryLoaderCacheMarker>,
) -> Result<(), anyhow::Error> {
    let mut stream = server_end.into_stream()?;
    while let Some(event) = stream.try_next().await? {
        match event {
            ftestrunner::LibraryLoaderCacheRequest::Serve { loader, .. } => {
                let cache = Arc::downgrade(&cache);
                serve_lib_loader(loader, cache).detach();
            }
        }
    }
    Ok(())
}

fn vmo_create_child(vmo: &zx::Vmo) -> Result<zx::Vmo, anyhow::Error> {
    let size = vmo.get_size().context("Cannot get vmo size.")?;
    vmo.create_child(
        zx::VmoChildOptions::SNAPSHOT_AT_LEAST_ON_WRITE | zx::VmoChildOptions::NO_WRITE,
        0,
        size,
    )
    .context("cannot create child vmo")
}

fn duplicate_vmo(vmo: &Option<zx::Vmo>) -> Result<Option<zx::Vmo>, anyhow::Error> {
    Ok(match &vmo {
        // create child instead of duplicating so that our debugger tools don't break.
        // Also vmo created using create child is non-writable, but debugger is able to write to it
        // as it has special permissions.
        Some(vmo) => vmo_create_child(&vmo)?.into(),
        None => None,
    })
}

/// Serve a custom lib loader which caches request to load VMOs.
fn serve_lib_loader(
    loader: ServerEnd<LoaderMarker>,
    lib_loader_cache: Weak<LibraryLoaderCache>,
) -> fasync::Task<()> {
    fasync::Task::spawn(
        async move {
            let mut stream = loader.into_stream()?;
            let (mut search_dirs, mut current_response_map) = match lib_loader_cache.upgrade() {
                Some(obj) => (
                    vec![obj.lib_proxy.clone()],
                    obj.load_response_map.lock().await.entry("".to_string()).or_default().clone(),
                ),
                None => return Ok(()),
            };

            while let Some(req) = stream.try_next().await? {
                let lib_loader_cache = match lib_loader_cache.upgrade() {
                    Some(obj) => obj,
                    None => break,
                };
                match req {
                    LoaderRequest::Done { control_handle } => {
                        control_handle.shutdown();
                    }
                    LoaderRequest::LoadObject { object_name, responder } => {
                        if let Some((rv, vmo)) = current_response_map.lock().await.get(&object_name)
                        {
                            responder.send(rv.clone(), duplicate_vmo(vmo)?)?;
                            continue;
                        }

                        let (vmo, rv) =
                            match library_loader::load_object(&search_dirs, &object_name).await {
                                Ok(b) => (b.into(), zx::sys::ZX_OK),
                                Err(e) => {
                                    warn!("failed to load object: {:?}", e);
                                    (None, zx::sys::ZX_ERR_NOT_FOUND)
                                }
                            };

                        let vmo_clone = duplicate_vmo(&vmo)?;
                        current_response_map.lock().await.insert(object_name, (rv, vmo));
                        responder.send(rv, vmo_clone)?;
                    }
                    LoaderRequest::Config { config, responder } => {
                        match library_loader::parse_config_string(
                            &vec![lib_loader_cache.lib_proxy.clone()],
                            &config,
                        ) {
                            Ok(new_search_path) => {
                                search_dirs = new_search_path;
                                current_response_map = lib_loader_cache
                                    .load_response_map
                                    .lock()
                                    .await
                                    .entry(config)
                                    .or_default()
                                    .clone();
                                responder.send(zx::sys::ZX_OK)?;
                            }
                            Err(e) => {
                                warn!("failed to parse config: {}", e);
                                responder.send(zx::sys::ZX_ERR_INVALID_ARGS)?;
                            }
                        }
                    }
                    LoaderRequest::Clone { loader, responder } => {
                        serve_lib_loader(loader, Arc::downgrade(&lib_loader_cache)).detach();
                        responder.send(zx::sys::ZX_OK)?;
                    }
                }
            }
            Ok(())
        }
        .unwrap_or_else(|e: anyhow::Error| warn!("couldn't run library loader service: {:?}", e)),
    )
}

#[cfg(test)]
mod tests {
    use {super::*, anyhow::Error, assert_matches::assert_matches, std::path::Path};

    async fn list_directory<'a>(root_proxy: &'a fio::DirectoryProxy) -> Vec<String> {
        let dir = fuchsia_fs::clone_directory(&root_proxy, fio::OpenFlags::CLONE_SAME_RIGHTS)
            .expect("Failed to clone DirectoryProxy");
        let entries = fuchsia_fs::directory::readdir(&dir).await.expect("readdir failed");
        entries.iter().map(|entry| entry.name.clone()).collect::<Vec<String>>()
    }

    #[fasync::run_singlethreaded(test)]
    async fn load_objects_test() -> Result<(), Error> {
        // Open this test's real /pkg/lib directory to use for this test, and then check to see
        // whether an asan subdirectory is present, and use it instead if so.
        // TODO(fxbug.dev/37534): Use a synthetic /pkg/lib in this test so it doesn't depend on the
        // package layout (like whether sanitizers are in use) once Rust vfs supports
        // OPEN_RIGHT_EXECUTABLE
        let rights = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE;
        let mut pkg_lib = fuchsia_fs::directory::open_in_namespace("/pkg/lib", rights)?;
        let entries = list_directory(&pkg_lib).await;
        if entries.iter().any(|f| &f as &str == "asan-ubsan") {
            pkg_lib = fuchsia_fs::open_directory(&pkg_lib, &Path::new("asan-ubsan"), rights)?;
        } else if entries.iter().any(|f| &f as &str == "asan") {
            pkg_lib = fuchsia_fs::open_directory(&pkg_lib, &Path::new("asan"), rights)?;
        } else if entries.iter().any(|f| &f as &str == "coverage") {
            pkg_lib = fuchsia_fs::open_directory(&pkg_lib, &Path::new("coverage"), rights)?;
        } else if entries.iter().any(|f| &f as &str == "coverage-rust") {
            pkg_lib = fuchsia_fs::open_directory(&pkg_lib, &Path::new("coverage-rust"), rights)?;
        } else if entries.iter().any(|f| &f as &str == "coverage-cts") {
            pkg_lib = fuchsia_fs::open_directory(&pkg_lib, &Path::new("coverage-cts"), rights)?;
        } else if entries.iter().any(|f| &f as &str == "profile") {
            pkg_lib = fuchsia_fs::open_directory(&pkg_lib, &Path::new("profile"), rights)?;
        }

        let (loader_proxy, loader_service) = fidl::endpoints::create_proxy::<LoaderMarker>()?;
        let cache = Arc::new(LibraryLoaderCache {
            lib_proxy: pkg_lib.into(),
            load_response_map: FutMutex::new(HashMap::new()),
        });
        serve_lib_loader(loader_service, Arc::downgrade(&cache)).detach();
        let tests = vec![
            // Should be able to access lib/ld.so.1
            ("ld.so.1", true),
            // Should be able to access lib/libfdio.so
            ("libfdio.so", true),
            // Should not be able to access lib/lib/ld.so.1
            ("lib/ld.so.1", false),
            // Should not be able to access lib/../lib/ld.so.1
            ("../lib/ld.so.1", false),
            // Should not be able to access lib/bin/test-runner-unit-tests
            ("bin/test-runner-unit-tests", false),
            // Should not be able to access bin/test-runner-unit-tests
            ("../bin/test-runner-unit-tests", false),
            // Should not be able to access meta/test-runner-unit-tests.cm
            ("../meta/test-runner-unit-tests.cm", false),
        ];
        for &(obj_name, should_succeed) in &tests {
            let (res, o_vmo) = loader_proxy.load_object(obj_name).await?;
            let map = cache.load_response_map.lock().await.get("").unwrap().clone();
            assert!(map.lock().await.contains_key(obj_name));
            if should_succeed {
                assert_eq!(zx::sys::ZX_OK, res, "loading {} did not succeed", obj_name);
                assert!(o_vmo.is_some());
                assert_matches!(map.lock().await.get(obj_name).unwrap().1, Some(_));
            } else {
                assert_ne!(zx::sys::ZX_OK, res, "loading {} did not fail", obj_name);
                assert!(o_vmo.is_none());
                assert_eq!(map.lock().await.get(obj_name).unwrap().1, None);
            }
        }

        // also test clone
        let (loader_proxy2, loader_service) = fidl::endpoints::create_proxy::<LoaderMarker>()?;
        assert_eq!(zx::sys::ZX_OK, loader_proxy.clone(loader_service).await?);
        for (obj_name, should_succeed) in tests {
            let (res, o_vmo) = loader_proxy2.load_object(obj_name).await?;
            if should_succeed {
                assert_eq!(zx::sys::ZX_OK, res, "loading {} did not succeed", obj_name);
                assert!(o_vmo.is_some());
            } else {
                assert_ne!(zx::sys::ZX_OK, res, "loading {} did not fail", obj_name);
                assert!(o_vmo.is_none());
            }
        }

        // test done
        loader_proxy2.done().expect("done should not fail");
        let err = loader_proxy2
            .load_object("some_name")
            .await
            .expect_err("Should fail with PEER_CLOSED.");

        let status = assert_matches!(err, fidl::Error::ClientChannelClosed{status, ..} => status);
        assert_eq!(status, zx::Status::PEER_CLOSED);
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
        let pkg_lib = fuchsia_fs::directory::open_in_namespace(
            "/pkg/lib/config_test/",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        )?;
        let (loader_proxy, loader_service) = fidl::endpoints::create_proxy::<LoaderMarker>()?;
        let cache = Arc::new(LibraryLoaderCache {
            lib_proxy: pkg_lib.into(),
            load_response_map: FutMutex::new(HashMap::new()),
        });
        serve_lib_loader(loader_service, Arc::downgrade(&cache)).detach();

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
            let map = cache
                .load_response_map
                .lock()
                .await
                .get(config.unwrap_or_default())
                .unwrap()
                .clone();
            if let Some(expected_result) = expected_result {
                assert_eq!(zx::sys::ZX_OK, res);
                let mut buf = vec![0; expected_result.len()];
                o_vmo.expect("missing vmo").read(&mut buf, 0)?;
                assert_eq!(expected_result.as_bytes(), buf.as_slice());
                assert_matches!(map.lock().await.get(obj_name).unwrap().1, Some(_));
            } else {
                assert_ne!(zx::sys::ZX_OK, res);
                assert!(o_vmo.is_none());
                assert_eq!(map.lock().await.get(obj_name).unwrap().1, None);
            }
        }
        Ok(())
    }
}
