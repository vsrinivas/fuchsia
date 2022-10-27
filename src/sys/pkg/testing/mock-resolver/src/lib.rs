// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use {
    anyhow::{anyhow, Error},
    fidl::endpoints::{Proxy, ServerEnd},
    fidl_fuchsia_io as fio,
    fidl_fuchsia_pkg::{
        self as fpkg, PackageResolverMarker, PackageResolverProxy, PackageResolverRequestStream,
        PackageResolverResolveResponder,
    },
    fuchsia_async as fasync,
    futures::{channel::oneshot, prelude::*},
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
    fn new(root: PathBuf) -> Self {
        TestPackage { root }
    }

    pub fn add_file(self, path: impl AsRef<Path>, contents: impl AsRef<[u8]>) -> Self {
        fs::write(self.root.join(PACKAGE_CONTENTS_PATH).join(path), contents)
            .expect("create fake package file");
        self
    }

    fn serve_on(&self, dir_request: ServerEnd<fio::DirectoryMarker>) {
        // Connect to the backing directory which we'll proxy _most_ requests to.
        let (backing_dir_proxy, server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        fdio::service_connect(self.root.to_str().unwrap(), server_end.into_channel()).unwrap();

        // Open the package directory using the directory request given by the client
        // asking to resolve the package, but proxy it through our handler so that we can
        // intercept requests for /meta.
        fasync::Task::spawn(handle_package_directory_stream(
            dir_request.into_stream().unwrap(),
            backing_dir_proxy,
        ))
        .detach();
    }
}

// Should roughly be kept in sync with the heuristic under Open in pkgfs/package_directory.go
fn should_redirect_request_to_merkle_file(path: &str, flags: fio::OpenFlags, mode: u32) -> bool {
    let mode_file = mode & fio::MODE_TYPE_MASK == fio::MODE_TYPE_FILE;
    let file_flag = flags.intersects(fio::OpenFlags::NOT_DIRECTORY);
    let dir_flag = flags.intersects(fio::OpenFlags::DIRECTORY);
    let path_flag = flags.intersects(fio::OpenFlags::NODE_REFERENCE);

    let open_as_file = mode_file || file_flag;
    let open_as_directory = dir_flag || path_flag;

    path == "meta" && (open_as_file || !open_as_directory)
}

/// Handles a stream of requests for a package directory,
/// redirecting file-mode Open requests for /meta to an internal file.
pub fn handle_package_directory_stream(
    mut stream: fio::DirectoryRequestStream,
    backing_dir_proxy: fio::DirectoryProxy,
) -> impl Future<Output = ()> {
    async move {
        let (package_contents_node_proxy, package_contents_dir_server_end) =
            fidl::endpoints::create_proxy::<fio::NodeMarker>().unwrap();
        backing_dir_proxy
            .open(
                fio::OpenFlags::DIRECTORY | fio::OpenFlags::RIGHT_READABLE,
                fio::MODE_TYPE_DIRECTORY,
                PACKAGE_CONTENTS_PATH,
                package_contents_dir_server_end,
            )
            .unwrap();

        // Effectively cast our package_contents_node_proxy to a directory, as it must be in order for these tests to work.
        let package_contents_dir_proxy =
            fio::DirectoryProxy::new(package_contents_node_proxy.into_channel().unwrap());

        while let Some(req) = stream.next().await {
            match req.unwrap() {
                fio::DirectoryRequest::Open { flags, mode, path, object, control_handle: _ } => {
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
                fio::DirectoryRequest::ReadDirents { max_bytes, responder } => {
                    let results = package_contents_dir_proxy
                        .read_dirents(max_bytes)
                        .await
                        .expect("read package contents dir");
                    responder.send(results.0, &results.1).expect("send ReadDirents response");
                }
                fio::DirectoryRequest::Rewind { responder } => {
                    responder
                        .send(
                            package_contents_dir_proxy
                                .rewind()
                                .await
                                .expect("rewind to package_contents dir"),
                        )
                        .expect("could send Rewind Response");
                }
                fio::DirectoryRequest::Close { responder } => {
                    // Don't do anything with this for now.
                    responder.send(&mut Ok(())).expect("send Close response")
                }
                other => panic!("unhandled request type: {:?}", other),
            }
        }
    }
}

#[derive(Debug)]
enum Expectation {
    ImmediateConstant(Result<TestPackage, fidl_fuchsia_pkg::ResolveError>),
    ImmediateVec(Vec<Result<TestPackage, fidl_fuchsia_pkg::ResolveError>>),
    BlockOnce(Option<oneshot::Sender<PendingResolve>>),
}

/// Mock package resolver which returns package directories that behave
/// roughly as if they're being served from pkgfs: /meta can be
/// opened as both a directory and a file.
pub struct MockResolverService {
    expectations: Mutex<HashMap<String, Expectation>>,
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

    /// Consider using Self::package/Self::url instead to clarify the usage of these 4 str params.
    pub fn register_custom_package(
        &self,
        name_for_url: impl AsRef<str>,
        meta_far_name: impl AsRef<str>,
        merkle: impl AsRef<str>,
        domain: &str,
    ) -> TestPackage {
        let name_for_url = name_for_url.as_ref();
        let merkle = merkle.as_ref();
        let meta_far_name = meta_far_name.as_ref();

        let url = format!("fuchsia-pkg://{}/{}", domain, name_for_url);
        let pkg = self.package(meta_far_name, merkle);
        self.url(url).resolve(&pkg);
        pkg
    }

    pub fn register_package(&self, name: impl AsRef<str>, merkle: impl AsRef<str>) -> TestPackage {
        self.register_custom_package(&name, &name, merkle, "fuchsia.com")
    }

    pub fn mock_resolve_failure(
        &self,
        url: impl Into<String>,
        error: fidl_fuchsia_pkg::ResolveError,
    ) {
        self.url(url).fail(error);
    }

    /// Registers a package with the given name and merkle root, returning a handle to add files to
    /// the package.
    ///
    /// This method does not register the package to be served by any fuchsia-pkg URLs. See
    /// [`MockResolverService::url`]
    pub fn package(&self, name: impl AsRef<str>, merkle: impl AsRef<str>) -> TestPackage {
        let name = name.as_ref();
        let merkle = merkle.as_ref();

        let root = self.packages_dir.path().join(merkle);

        // Create the package directory and the meta directory for the fake package.
        create_dir(&root).expect("package to not yet exist");
        create_dir(&root.join(PACKAGE_CONTENTS_PATH))
            .expect("package_contents dir to not yet exist");
        create_dir(&root.join(PACKAGE_CONTENTS_PATH).join("meta"))
            .expect("meta dir to not yet exist");

        // Create the file which holds the merkle root of the package, to redirect requests for 'meta' to.
        std::fs::write(root.join(META_FAR_MERKLE_ROOT_PATH), merkle)
            .expect("create fake package file");

        TestPackage::new(root)
            .add_file("meta/package", format!("{{\"name\": \"{}\", \"version\": \"0\"}}", name))
    }

    /// Equivalent to `self.url(format!("fuchsia-pkg://fuchsia.com/{}", path))`
    pub fn path(&self, path: impl AsRef<str>) -> ForUrl<'_> {
        self.url(format!("fuchsia-pkg://fuchsia.com/{}", path.as_ref()))
    }

    /// Returns an object to configure the handler for the given URL.
    pub fn url(&self, url: impl Into<String>) -> ForUrl<'_> {
        ForUrl { svc: self, url: url.into() }
    }

    pub fn spawn_resolver_service(self: Arc<Self>) -> PackageResolverProxy {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();

        fasync::Task::spawn(self.run_resolver_service(stream).unwrap_or_else(|e| {
            panic!("error running package resolver service: {:#}", anyhow!(e))
        }))
        .detach();

        proxy
    }

    /// Serves the fuchsia.pkg.PackageResolver protocol on the given request stream.
    pub async fn run_resolver_service(
        self: Arc<Self>,
        mut stream: PackageResolverRequestStream,
    ) -> Result<(), Error> {
        while let Some(event) = stream.try_next().await.expect("received request") {
            match event {
                fidl_fuchsia_pkg::PackageResolverRequest::Resolve {
                    package_url,
                    dir,
                    responder,
                } => self.handle_resolve(package_url, dir, responder).await?,
                fidl_fuchsia_pkg::PackageResolverRequest::ResolveWithContext {
                    package_url: _,
                    context: _,
                    dir: _,
                    responder: _,
                } => panic!("ResolveWithContext not implemented"),
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
        dir: ServerEnd<fio::DirectoryMarker>,
        responder: PackageResolverResolveResponder,
    ) -> Result<(), Error> {
        eprintln!("TEST: Got resolve request for {:?}", package_url);

        (*self.resolve_hook)(&package_url);

        match self.expectations.lock().get_mut(&package_url).unwrap_or(
            &mut Expectation::ImmediateConstant(Err(
                fidl_fuchsia_pkg::ResolveError::PackageNotFound,
            )),
        ) {
            Expectation::ImmediateConstant(Ok(package)) => {
                package.serve_on(dir);
                responder.send(&mut Ok(fpkg::ResolutionContext { bytes: vec![] }))?;
            }
            Expectation::ImmediateConstant(Err(error)) => {
                responder.send(&mut Err(*error))?;
            }
            Expectation::BlockOnce(handler) => {
                let handler = handler.take().unwrap();
                handler.send(PendingResolve { responder, dir_request: dir }).unwrap();
            }
            Expectation::ImmediateVec(expected_results) => {
                if expected_results.is_empty() {
                    panic!("expected_results should be >= number of resolve requests");
                }
                match expected_results.remove(0) {
                    Ok(package) => {
                        package.serve_on(dir);
                        responder.send(&mut Ok(fpkg::ResolutionContext { bytes: vec![] }))?;
                    }
                    Err(e) => {
                        responder.send(&mut Err(e))?;
                    }
                };
            }
        }
        Ok(())
    }
}

#[must_use]
pub struct ForUrl<'a> {
    svc: &'a MockResolverService,
    url: String,
}

impl<'a> ForUrl<'a> {
    /// Fail resolve requests for the given URL with the given error status.
    pub fn fail(self, error: fidl_fuchsia_pkg::ResolveError) {
        self.svc.expectations.lock().insert(self.url, Expectation::ImmediateConstant(Err(error)));
    }

    /// Succeed resolve requests for the given URL by serving the given package.
    pub fn resolve(self, pkg: &TestPackage) {
        // Manually construct a new TestPackage referring to the same root dir. Note that it would
        // be invalid for TestPackage to impl Clone, as add_file would affect all Clones of a
        // package.
        let pkg = TestPackage::new(pkg.root.clone());
        self.svc.expectations.lock().insert(self.url, Expectation::ImmediateConstant(Ok(pkg)));
    }

    /// Blocks requests for the given URL once, allowing the returned handler control the response.
    /// Panics on further requests for that URL.
    pub fn block_once(self) -> ResolveHandler {
        let (send, recv) = oneshot::channel();

        self.svc.expectations.lock().insert(self.url, Expectation::BlockOnce(Some(send)));
        ResolveHandler::Waiting(recv)
    }

    /// Respond to resolve requests serially with a list of pre-defined immediate responses. This is
    /// useful if the caller wants to make several resolve calls for the same url and have each
    /// resolve call return something different.
    ///
    /// This API is different from the other ForUrl APIs because the mock resolver will use each
    /// response exactly once. In the other APIs, the resolver will always return the given response
    /// for a url regardless of how many times resolve() is called.
    pub fn respond_serially(
        self,
        responses: Vec<Result<TestPackage, fidl_fuchsia_pkg::ResolveError>>,
    ) {
        self.svc.expectations.lock().insert(self.url, Expectation::ImmediateVec(responses));
    }
}

#[derive(Debug)]
pub struct PendingResolve {
    responder: PackageResolverResolveResponder,
    dir_request: ServerEnd<fio::DirectoryMarker>,
}

#[derive(Debug)]
pub enum ResolveHandler {
    Waiting(oneshot::Receiver<PendingResolve>),
    Blocked(PendingResolve),
}

impl ResolveHandler {
    /// Waits for the mock package resolver to receive a resolve request for this handler.
    pub async fn wait(&mut self) {
        match self {
            ResolveHandler::Waiting(receiver) => {
                *self = ResolveHandler::Blocked(receiver.await.unwrap());
            }
            ResolveHandler::Blocked(_) => {}
        }
    }

    async fn into_pending(self) -> PendingResolve {
        match self {
            ResolveHandler::Waiting(receiver) => receiver.await.unwrap(),
            ResolveHandler::Blocked(pending) => pending,
        }
    }

    /// Wait for the request and fail the resolve with the given status.
    pub async fn fail(self, error: fidl_fuchsia_pkg::ResolveError) {
        self.into_pending().await.responder.send(&mut Err(error)).unwrap();
    }

    /// Wait for the request and succeed the resolve by serving the given package.
    pub async fn resolve(self, pkg: &TestPackage) {
        let PendingResolve { responder, dir_request } = self.into_pending().await;

        pkg.serve_on(dir_request);
        responder.send(&mut Ok(fpkg::ResolutionContext { bytes: vec![] })).unwrap();
    }
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches, fidl_fuchsia_pkg::ResolveError};

    async fn read_file(dir_proxy: &fio::DirectoryProxy, path: &str) -> String {
        let file_proxy =
            fuchsia_fs::directory::open_file(dir_proxy, path, fio::OpenFlags::RIGHT_READABLE)
                .await
                .unwrap();

        fuchsia_fs::file::read_to_string(&file_proxy).await.unwrap()
    }

    fn do_resolve(
        proxy: &PackageResolverProxy,
        url: &str,
    ) -> impl Future<Output = Result<(fio::DirectoryProxy, fpkg::ResolutionContext), ResolveError>>
    {
        let (package_dir, package_dir_server_end) = fidl::endpoints::create_proxy().unwrap();
        let fut = proxy.resolve(url, package_dir_server_end);

        async move {
            let resolve_context = fut.await.unwrap()?;
            Ok((package_dir, resolve_context))
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_mock_resolver() {
        let resolved_urls = Arc::new(Mutex::new(vec![]));
        let resolved_urls_clone = resolved_urls.clone();
        let resolver =
            Arc::new(MockResolverService::new(Some(Box::new(move |resolved_url: &str| {
                resolved_urls_clone.lock().push(resolved_url.to_owned())
            }))));

        let resolver_proxy = Arc::clone(&resolver).spawn_resolver_service();

        resolver
            .register_package("update", "upd4t3")
            .add_file(
                "packages",
                "system_image/0=42ade6f4fd51636f70c68811228b4271ed52c4eb9a647305123b4f4d0741f296\n",
            )
            .add_file("zbi", "fake zbi");

        // We should have no URLs resolved yet.
        assert_eq!(*resolved_urls.lock(), Vec::<String>::new());

        let (package_dir, _resolved_context) =
            do_resolve(&resolver_proxy, "fuchsia-pkg://fuchsia.com/update").await.unwrap();

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

    #[fasync::run_singlethreaded(test)]
    async fn block_once_blocks() {
        let resolver = Arc::new(MockResolverService::new(None));
        let mut handle_first = resolver.url("fuchsia-pkg://fuchsia.com/first").block_once();
        let handle_second = resolver.path("second").block_once();

        let proxy = Arc::clone(&resolver).spawn_resolver_service();

        let first_fut = do_resolve(&proxy, "fuchsia-pkg://fuchsia.com/first");
        let second_fut = do_resolve(&proxy, "fuchsia-pkg://fuchsia.com/second");

        handle_first.wait().await;

        handle_second.fail(fidl_fuchsia_pkg::ResolveError::PackageNotFound).await;
        assert_matches!(second_fut.await, Err(fidl_fuchsia_pkg::ResolveError::PackageNotFound));

        let pkg = resolver.package("second", "fake merkle");
        handle_first.resolve(&pkg).await;

        let (first_pkg, _resolved_context) = first_fut.await.unwrap();
        assert_eq!(read_file(&first_pkg, "meta").await, "fake merkle");
    }

    #[fasync::run_singlethreaded(test)]
    async fn multiple_predefined_responses() {
        let resolver = Arc::new(MockResolverService::new(None));
        let resolver_proxy = Arc::clone(&resolver).spawn_resolver_service();

        resolver.url("fuchsia-pkg://fuchsia.com/update").respond_serially(vec![
            Err(ResolveError::NoSpace),
            Ok(resolver.package("update", "upd4t3")),
        ]);

        // First resolve should fail with the error.
        assert_matches!(
            do_resolve(&resolver_proxy, "fuchsia-pkg://fuchsia.com/update").await,
            Err(ResolveError::NoSpace)
        );

        // Second resolve should succeed and give us the expected package dir.
        let (package_dir, _resolved_context) =
            do_resolve(&resolver_proxy, "fuchsia-pkg://fuchsia.com/update").await.unwrap();
        let meta_contents = read_file(&package_dir, "meta").await;
        assert_eq!(meta_contents, "upd4t3");
    }

    #[fasync::run_singlethreaded(test)]
    #[should_panic(expected = "expected_results should be >= number of resolve requests")]
    async fn panics_when_not_enough_predefined_responses() {
        let resolver = Arc::new(MockResolverService::new(None));
        let resolver_proxy = Arc::clone(&resolver).spawn_resolver_service();

        resolver.url("fuchsia-pkg://fuchsia.com/update").respond_serially(vec![]);

        // Since there are no expected responses, the mock resolver should panic.
        let _ = do_resolve(&resolver_proxy, "fuchsia-pkg://fuchsia.com/update").await;
    }
}
