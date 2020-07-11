// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, DirectoryRequest, DirectoryRequestStream},
    fidl_fuchsia_pkg::{PackageResolverRequestStream, PackageResolverResolveResponder},
    fuchsia_async as fasync,
    fuchsia_zircon::Status,
    futures::prelude::*,
    parking_lot::Mutex,
    std::{
        collections::HashMap,
        fs::{self, create_dir},
        path::{Path, PathBuf},
        sync::Arc,
    },
    tempfile::TempDir,
};

const PACKAGE_CONTENTS_PATH: &str = "package_contents";
const META_FAR_MERKLE_ROOT_PATH: &str = "meta";

#[derive(Debug)]
pub struct TestPackage {
    root: PathBuf,
}

impl TestPackage {
    pub fn new(root: PathBuf) -> Self {
        TestPackage { root }
    }

    pub fn add_file(self, path: impl AsRef<Path>, contents: impl AsRef<[u8]>) -> Self {
        fs::write(self.root.join(PACKAGE_CONTENTS_PATH).join(path), contents)
            .expect("create fake package file");
        self
    }
}

// Should roughly be kept in sync with the heuristic under Open in pkgfs/package_directory.go
fn should_redirect_request_to_merkle_file(path: &str, flags: u32, mode: u32) -> bool {
    let mode_file = mode & fidl_fuchsia_io::MODE_TYPE_MASK == fidl_fuchsia_io::MODE_TYPE_FILE;
    let file_flag = flags & fidl_fuchsia_io::OPEN_FLAG_NOT_DIRECTORY != 0;
    let dir_flag = flags & fidl_fuchsia_io::OPEN_FLAG_DIRECTORY != 0;
    let path_flag = flags & fidl_fuchsia_io::OPEN_FLAG_NODE_REFERENCE != 0;

    let open_as_file = mode_file || file_flag;
    let open_as_directory = dir_flag || path_flag;

    path == "meta" && (open_as_file || !open_as_directory)
}

/// Handles a stream of requests for a package directory,
/// redirecting file-mode Open requests for /meta to an internal file.
pub fn handle_package_directory_stream(
    mut stream: DirectoryRequestStream,
    backing_dir_proxy: DirectoryProxy,
) -> impl Future<Output = ()> {
    async move {
        let (package_contents_node_proxy, package_contents_dir_server_end) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_io::NodeMarker>().unwrap();
        backing_dir_proxy
            .open(
                fidl_fuchsia_io::OPEN_FLAG_DIRECTORY | fidl_fuchsia_io::OPEN_RIGHT_READABLE,
                fidl_fuchsia_io::MODE_TYPE_DIRECTORY,
                PACKAGE_CONTENTS_PATH,
                package_contents_dir_server_end,
            )
            .unwrap();

        // Effectively cast our package_contents_node_proxy to a directory, as it must be in order for these tests to work.
        let package_contents_dir_proxy =
            DirectoryProxy::new(package_contents_node_proxy.into_channel().unwrap());

        while let Some(req) = stream.next().await {
            match req.unwrap() {
                DirectoryRequest::Open { flags, mode, path, object, control_handle: _ } => {
                    // If the client is trying to read the meta directory _as a file_, redirect them
                    // to the file which actually holds the merkle for the purposes of these tests.
                    // Otherwise, redirect to the real package contents.

                    if path == "." {
                        panic!(
                            "Client would escape mock resolver directory redirects by opening '.', which might break further requests to /meta as a file"
                        )
                    }

                    if should_redirect_request_to_merkle_file(&path, flags, mode) {
                        backing_dir_proxy.open(flags, mode, &path, object).unwrap();
                    } else {
                        package_contents_dir_proxy.open(flags, mode, &path, object).unwrap();
                    }
                }
                DirectoryRequest::ReadDirents { max_bytes, responder } => {
                    let results = package_contents_dir_proxy
                        .read_dirents(max_bytes)
                        .await
                        .expect("read package contents dir");
                    responder.send(results.0, &results.1).expect("send ReadDirents response");
                }
                DirectoryRequest::Rewind { responder } => {
                    responder
                        .send(
                            package_contents_dir_proxy
                                .rewind()
                                .await
                                .expect("rewind to package_contents dir"),
                        )
                        .expect("could send Rewind Response");
                }
                DirectoryRequest::Close { responder } => {
                    // Don't do anything with this for now.
                    responder.send(Status::OK.into_raw()).expect("send Close response")
                }
                other => panic!("unhandled request type: {:?}", other),
            }
        }
    }
}

/// Mock package resolver which returns package directories that behave
/// roughly as if they're being served from pkgfs: /meta can be
/// opened as both a directory and a file.
pub struct MockResolverService {
    expectations: Mutex<HashMap<String, Result<PathBuf, Status>>>,
    resolve_hook: Box<dyn Fn(&str) + Send + Sync>,
    packages_dir: tempfile::TempDir,
}

impl MockResolverService {
    pub fn new(resolve_hook: Option<Box<dyn Fn(&str) + Send + Sync>>) -> Self {
        let packages_dir = TempDir::new().expect("create packages tempdir");
        Self {
            packages_dir,
            resolve_hook: resolve_hook.unwrap_or_else(|| Box::new(|_| ())),
            expectations: Mutex::new(HashMap::new()),
        }
    }

    pub fn register_custom_package(
        &self,
        name_for_url: impl AsRef<str>,
        meta_far_name: impl AsRef<str>,
        merkle: impl AsRef<str>,
        domain: &str,
    ) -> TestPackage {
        let name = name_for_url.as_ref();
        let merkle = merkle.as_ref();
        let meta_far_name = meta_far_name.as_ref();

        let root = self.packages_dir.path().join(merkle);

        // Create the package directory and the meta directory for the fake package.
        create_dir(&root).expect("package to not yet exist");
        create_dir(&root.join(PACKAGE_CONTENTS_PATH))
            .expect("package_contents dir to not yet exist");
        create_dir(&root.join(PACKAGE_CONTENTS_PATH).join("meta"))
            .expect("meta dir to not yet exist");

        let url = format!("fuchsia-pkg://{}/{}", domain, name);
        self.expectations.lock().insert(url.into(), Ok(root.clone()));

        // Create the file which holds the merkle root of the package, to redirect requests for 'meta' to.
        std::fs::write(root.join(META_FAR_MERKLE_ROOT_PATH), merkle)
            .expect("create fake package file");

        TestPackage::new(root).add_file(
            "meta/package",
            format!("{{\"name\": \"{}\", \"version\": \"0\"}}", meta_far_name),
        )
    }

    pub fn register_package(&self, name: impl AsRef<str>, merkle: impl AsRef<str>) -> TestPackage {
        // The update package must always be internally named update/0, to support space-savings gained with fxbug.dev/52767
        self.register_custom_package(name, "update", merkle, "fuchsia.com")
    }

    pub async fn run_resolver_service(
        self: Arc<Self>,
        mut stream: PackageResolverRequestStream,
    ) -> Result<(), Error> {
        while let Some(event) = stream.try_next().await? {
            match event {
                fidl_fuchsia_pkg::PackageResolverRequest::Resolve {
                    package_url,
                    selectors: _,
                    update_policy: _,
                    dir,
                    responder,
                } => self.handle_resolve(package_url, dir, responder).await?,
                fidl_fuchsia_pkg::PackageResolverRequest::GetHash {
                    package_url: _,
                    responder: _,
                } => panic!("GetHash not implemented"),
            }
        }
        Ok(())
    }

    async fn handle_resolve(
        &self,
        package_url: String,
        dir: ServerEnd<DirectoryMarker>,
        responder: PackageResolverResolveResponder,
    ) -> Result<(), Error> {
        eprintln!("TEST: Got resolve request for {:?}", package_url);

        let response = self
            .expectations
            .lock()
            .get(&package_url)
            .map(|entry| entry.clone())
            // Successfully resolve unexpected packages without serving a package dir. Log the
            // transaction so tests can decide if it was expected.
            // TODO(fxb/53187): change this to NOT_FOUND and fix the tests.
            .unwrap_or(Err(Status::OK));

        (*self.resolve_hook)(&package_url);

        let mut response = match response {
            Ok(package_dir) => {
                // Open the package directory using the directory request given by the client
                // asking to resolve the package, but proxy it through our handler so that we can
                // intercept requests for /meta.
                // Connect to the backing directory which we'll proxy _most_ requests to.
                let (backing_dir_proxy, server_end) =
                    fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>().unwrap();
                fdio::service_connect(package_dir.to_str().unwrap(), server_end.into_channel())
                    .unwrap();

                fasync::spawn(handle_package_directory_stream(
                    dir.into_stream().unwrap(),
                    backing_dir_proxy,
                ));

                Ok(())
            }
            // TODO(fxb/53187): remove this line and fix affected tests.
            Err(Status::OK) => Ok(()),
            Err(status) => Err(status.into_raw()),
        };
        responder.send(&mut response)?;
        Ok(())
    }

    pub fn mock_resolve_failure(&self, url: impl Into<String>, response_status: Status) {
        self.expectations.lock().insert(url.into(), Err(response_status));
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_pkg::UpdatePolicy};

    async fn read_file(dir_proxy: &DirectoryProxy, path: &str) -> String {
        let file_proxy =
            io_util::directory::open_file(dir_proxy, path, fidl_fuchsia_io::OPEN_RIGHT_READABLE)
                .await
                .unwrap();

        io_util::file::read_to_string(&file_proxy).await.unwrap()
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_mock_resolver() {
        let resolved_urls = Arc::new(Mutex::new(vec![]));
        let resolved_urls_clone = resolved_urls.clone();
        let resolver =
            Arc::new(MockResolverService::new(Some(Box::new(move |resolved_url: &str| {
                resolved_urls_clone.lock().push(resolved_url.to_owned())
            }))));

        let (resolver_proxy, resolver_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_pkg::PackageResolverMarker>()
                .expect("Creating resolver endpoints");
        fasync::spawn(
            Arc::clone(&resolver)
                .run_resolver_service(resolver_stream)
                .unwrap_or_else(|e| panic!("error running resolver service: {:?}", e)),
        );

        resolver
            .register_package("update", "upd4t3")
            .add_file(
                "packages",
                "system_image/0=42ade6f4fd51636f70c68811228b4271ed52c4eb9a647305123b4f4d0741f296\n",
            )
            .add_file("zbi", "fake zbi");

        // We should have no URLs resolved yet.
        assert_eq!(*resolved_urls.lock(), Vec::<String>::new());

        let (package_dir, package_dir_server_end) = fidl::endpoints::create_proxy().unwrap();
        let selectors: Vec<String> = vec![];
        resolver_proxy
            .resolve(
                "fuchsia-pkg://fuchsia.com/update",
                &mut selectors.iter().map(|s| s.as_str()),
                &mut UpdatePolicy { fetch_if_absent: true, allow_old_versions: true },
                package_dir_server_end,
            )
            .await
            .unwrap()
            .unwrap();

        // Check that we can read from /meta (meta-as-file mode)
        let meta_contents = read_file(&package_dir, "meta").await;
        assert_eq!(meta_contents, "upd4t3");

        // Check that we can read a file _within_ /meta (meta-as-dir mode)
        let package_info = read_file(&package_dir, "meta/package").await;
        assert_eq!(package_info, "{\"name\": \"update\", \"version\": \"0\"}");

        // Check that we can read files we expect to be in the package.
        let zbi_contents = read_file(&package_dir, "zbi").await;
        assert_eq!(zbi_contents, "fake zbi");

        // Make sure that our resolve hook was called properly
        assert_eq!(*resolved_urls.lock(), vec!["fuchsia-pkg://fuchsia.com/update"]);
    }
}
