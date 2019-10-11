// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests the property that pkg_resolver does not enter a bad
/// state (successfully handles retries) when the TUF server errors while
/// servicing fuchsia.pkg.PackageResolver.Resolve FIDL requests.
use {
    super::*,
    fuchsia_merkle::MerkleTree,
    fuchsia_pkg_testing::{serve::UriPathHandler, RepositoryBuilder},
    futures::{
        compat::Stream01CompatExt,
        future::{ready, BoxFuture},
    },
    hyper::{header::CONTENT_LENGTH, Body, Response},
    matches::assert_matches,
    parking_lot::Mutex,
    std::{
        path::{Path, PathBuf},
        sync::Arc,
    },
};

struct OverrideOnceUriPathHandler {
    already_overridden: Mutex<bool>,
    path_to_override: PathBuf,
    path_handler: Box<dyn UriPathHandler>,
}

impl OverrideOnceUriPathHandler {
    fn new(path_to_override: impl Into<PathBuf>, path_handler: impl UriPathHandler) -> Self {
        Self {
            already_overridden: Mutex::new(false),
            path_to_override: path_to_override.into(),
            path_handler: Box::new(path_handler),
        }
    }
}

impl UriPathHandler for OverrideOnceUriPathHandler {
    fn handle(&self, uri_path: &Path, response: Response<Body>) -> BoxFuture<Response<Body>> {
        if uri_path != self.path_to_override {
            return ready(response).boxed();
        }
        let mut already_overridden = self.already_overridden.lock();
        if *already_overridden {
            return ready(response).boxed();
        }
        *already_overridden = true;
        self.path_handler.handle(uri_path, response)
    }
}

async fn verify_resolve_fails_then_succeeds(
    pkg: Package,
    uri_handler: OverrideOnceUriPathHandler,
    failure_status: Status,
) -> Result<(), Error> {
    let env = TestEnv::new();
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).add_package(&pkg).build().await?,
    );
    let served_repository = repo.build_server().uri_path_override_handler(uri_handler).start()?;
    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);
    env.proxies.repo_manager.add(repo_config.into()).await?;
    env.set_experiment_state(Experiment::DownloadBlob, true).await;
    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());

    assert_matches!(env.resolve_package(&pkg_url).await, Err(status) if status == failure_status);
    let package_dir = env.resolve_package(&pkg_url).await.expect("package to resolve");

    pkg.verify_contents(&package_dir).await.expect("correct package contents");
    env.stop().await;
    Ok(())
}

struct NotFoundUriPathHandler;
impl UriPathHandler for NotFoundUriPathHandler {
    fn handle(&self, _uri_path: &Path, _response: Response<Body>) -> BoxFuture<Response<Body>> {
        ready(
            Response::builder()
                .status(hyper::StatusCode::NOT_FOUND)
                .body(Body::empty())
                .expect("valid response"),
        )
        .boxed()
    }
}

#[fasync::run_singlethreaded(test)]
async fn second_resolve_succeeds_when_far_404() -> Result<(), Error> {
    let pkg = make_rolldice_pkg_with_extra_blobs(1).await?;
    let path_to_override = format!("/blobs/{}", pkg.meta_far_merkle_root());

    verify_resolve_fails_then_succeeds(
        pkg,
        OverrideOnceUriPathHandler::new(path_to_override, NotFoundUriPathHandler),
        Status::UNAVAILABLE,
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn second_resolve_succeeds_when_blob_404() -> Result<(), Error> {
    let pkg = make_rolldice_pkg_with_extra_blobs(1).await?;
    let path_to_override = format!(
        "/blobs/{}",
        MerkleTree::from_reader(extra_blob_contents(0).as_slice()).expect("merkle slice").root()
    );

    verify_resolve_fails_then_succeeds(
        pkg,
        OverrideOnceUriPathHandler::new(path_to_override, NotFoundUriPathHandler),
        Status::UNAVAILABLE,
    )
    .await
}

async fn body_to_bytes(body: Body) -> Vec<u8> {
    body.compat().try_concat().await.expect("body stream to complete").to_vec()
}

struct OneByteShortThenErrorUriPathHandler;
impl UriPathHandler for OneByteShortThenErrorUriPathHandler {
    fn handle(&self, _uri_path: &Path, response: Response<Body>) -> BoxFuture<Response<Body>> {
        async {
            let mut bytes = body_to_bytes(response.into_body()).await;
            if let None = bytes.pop() {
                panic!("can't short 0 bytes");
            }
            Response::builder()
                .status(hyper::StatusCode::OK)
                .header(CONTENT_LENGTH, bytes.len() + 1)
                .body(Body::wrap_stream(
                    futures::stream::iter(vec![
                        Ok(bytes),
                        Err("all_but_one_byte_then_eror has sent all but one bytes".to_string()),
                    ])
                    .compat(),
                ))
                .expect("valid response")
        }
            .boxed()
    }
}

// If the body of an https response is not large enough, hyper will download the body
// along with the header in the initial fuchsia_hyper::HttpsClient.request(). This means
// that even if the body is implemented with a stream that fails before the transfer is
// complete, the failure will occur during the initial request and before the batch loop
// that writes to pkgfs/blobfs. Value was found experimentally.
const FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING: usize = 1_000_000;

#[fasync::run_singlethreaded(test)]
async fn second_resolve_succeeds_when_far_errors_mid_download() -> Result<(), Error> {
    let pkg = PackageBuilder::new("large_meta_far")
        .add_resource_at(
            "meta/large_file",
            vec![0; FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING].as_slice(),
        )?
        .build()
        .await?;
    let path_to_override = format!("/blobs/{}", pkg.meta_far_merkle_root());

    verify_resolve_fails_then_succeeds(
        pkg,
        OverrideOnceUriPathHandler::new(path_to_override, OneByteShortThenErrorUriPathHandler),
        Status::UNAVAILABLE,
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn second_resolve_succeeds_when_blob_errors_mid_download() -> Result<(), Error> {
    let blob = vec![0; FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING];
    let pkg = PackageBuilder::new("large_blob")
        .add_resource_at("blobbity/blob", blob.as_slice())?
        .build()
        .await?;
    let path_to_override = format!(
        "/blobs/{}",
        MerkleTree::from_reader(blob.as_slice()).expect("merkle slice").root()
    );

    verify_resolve_fails_then_succeeds(
        pkg,
        OverrideOnceUriPathHandler::new(path_to_override, OneByteShortThenErrorUriPathHandler),
        Status::UNAVAILABLE,
    )
    .await
}

struct OneByteShortThenDisconnectUriPathHandler;
impl UriPathHandler for OneByteShortThenDisconnectUriPathHandler {
    fn handle(&self, _uri_path: &Path, response: Response<Body>) -> BoxFuture<Response<Body>> {
        async {
            let mut bytes = body_to_bytes(response.into_body()).await;
            if let None = bytes.pop() {
                panic!("can't short 0 bytes");
            }
            Response::builder()
                .status(hyper::StatusCode::OK)
                .header(CONTENT_LENGTH, bytes.len() + 1)
                .body(Body::wrap_stream(
                    futures::stream::iter(vec![Result::<Vec<u8>, String>::Ok(bytes)]).compat(),
                ))
                .expect("valid response")
        }
            .boxed()
    }
}

#[fasync::run_singlethreaded(test)]
async fn second_resolve_succeeds_disconnect_before_far_complete() -> Result<(), Error> {
    let pkg = PackageBuilder::new("large_meta_far")
        .add_resource_at(
            "meta/large_file",
            vec![0; FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING].as_slice(),
        )?
        .build()
        .await?;
    let path_to_override = format!("/blobs/{}", pkg.meta_far_merkle_root());

    verify_resolve_fails_then_succeeds(
        pkg,
        OverrideOnceUriPathHandler::new(path_to_override, OneByteShortThenDisconnectUriPathHandler),
        Status::UNAVAILABLE,
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn second_resolve_succeeds_disconnect_before_blob_complete() -> Result<(), Error> {
    let blob = vec![0; FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING];
    let pkg = PackageBuilder::new("large_blob")
        .add_resource_at("blobbity/blob", blob.as_slice())?
        .build()
        .await?;
    let path_to_override = format!(
        "/blobs/{}",
        MerkleTree::from_reader(blob.as_slice()).expect("merkle slice").root()
    );

    verify_resolve_fails_then_succeeds(
        pkg,
        OverrideOnceUriPathHandler::new(path_to_override, OneByteShortThenDisconnectUriPathHandler),
        Status::UNAVAILABLE,
    )
    .await
}

struct OneByteFlippedUriPathHandler;
impl UriPathHandler for OneByteFlippedUriPathHandler {
    fn handle(&self, _uri_path: &Path, response: Response<Body>) -> BoxFuture<Response<Body>> {
        async {
            let mut bytes = body_to_bytes(response.into_body()).await;
            if bytes.is_empty() {
                panic!("can't flip 0 bytes");
            }
            bytes[0] = !bytes[0];
            Response::builder()
                .status(hyper::StatusCode::OK)
                .body(bytes.into())
                .expect("valid response")
        }
            .boxed()
    }
}

#[fasync::run_singlethreaded(test)]
async fn second_resolve_succeeds_when_far_corrupted() -> Result<(), Error> {
    let pkg = make_rolldice_pkg_with_extra_blobs(1).await?;
    let path_to_override = format!("/blobs/{}", pkg.meta_far_merkle_root());

    verify_resolve_fails_then_succeeds(
        pkg,
        OverrideOnceUriPathHandler::new(path_to_override, OneByteFlippedUriPathHandler),
        Status::IO,
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn second_resolve_succeeds_when_blob_corrupted() -> Result<(), Error> {
    let pkg = make_rolldice_pkg_with_extra_blobs(1).await?;
    let blob = extra_blob_contents(0);
    let path_to_override = format!(
        "/blobs/{}",
        MerkleTree::from_reader(blob.as_slice()).expect("merkle slice").root()
    );

    verify_resolve_fails_then_succeeds(
        pkg,
        OverrideOnceUriPathHandler::new(path_to_override, OneByteFlippedUriPathHandler),
        Status::IO,
    )
    .await
}
