// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cobalt_sw_delivery_registry as metrics, fuchsia_async as fasync,
    fuchsia_pkg_testing::{
        serve::{handler, HttpRange, RangeUriPathHandler},
        Package, PackageBuilder, RepositoryBuilder,
    },
    fuchsia_zircon::Status,
    futures::future::{BoxFuture, FutureExt as _},
    hyper::{Body, Response},
    lib::{TestEnvBuilder, EMPTY_REPO_PATH, FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING},
    std::{convert::TryInto as _, path::Path, sync::Arc},
};

fn meta_far_size(pkg: &Package) -> u64 {
    pkg.meta_far().unwrap().metadata().unwrap().len()
}

// TODO(fxbug.dev/71333) these test cases have a lot of repeated code, find an abstraction.

#[fasync::run_singlethreaded(test)]
async fn single_blob_resume_success() {
    let pkg = PackageBuilder::new("large_meta_far")
        .add_resource_at(
            "meta/large_file",
            vec![0; FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING].as_slice(),
        )
        .build()
        .await
        .unwrap();
    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());
    let path_to_override = format!("/blobs/{}", pkg.meta_far_merkle_root());

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );

    let get_handler =
        handler::ForPath::new(path_to_override.clone(), handler::OneByteShortThenError);

    let (range_handler, history) = handler::RecordingRange::new();

    let served_repository = repo
        .server()
        .uri_path_override_handler(get_handler)
        .range_uri_path_override_handler(range_handler)
        .start()
        .unwrap();

    let env = TestEnvBuilder::new().build().await;
    env.register_repo(&served_repository).await;
    let package_dir = env.resolve_package(&pkg_url).await.expect("package to resolve");
    pkg.verify_contents(&package_dir).await.expect("correct package contents");

    env.assert_count_events(
        metrics::FETCH_BLOB_METRIC_ID,
        vec![(
            metrics::FetchBlobMetricDimensionResult::Success,
            metrics::FetchBlobMetricDimensionResumed::True,
        )],
    )
    .await;

    let history = history.take();
    assert_eq!(history.len(), 1);
    assert_eq!(history[0].uri_path().to_str().unwrap(), path_to_override);
    let range: HttpRange = history[0].range().try_into().unwrap();
    assert!(range.start() > 0);
    assert_eq!(range.end() + 1, meta_far_size(&pkg));

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn two_blob_resume_success() {
    let pkg = PackageBuilder::new("large_meta_far")
        .add_resource_at(
            "meta/large_file",
            vec![0; 3 * FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING].as_slice(),
        )
        .build()
        .await
        .unwrap();
    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());
    let path_to_override = format!("/blobs/{}", pkg.meta_far_merkle_root());

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );

    let get_handler = handler::ForPath::new(
        path_to_override.clone(),
        handler::NBytesThenError::new(FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING),
    );

    let (range_recorder, history) = handler::RecordingRange::new();
    let range_handler = handler::RangeOnce::new(handler::RangeNBytesThenError::new(
        FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING,
    ));

    let served_repository = repo
        .server()
        .uri_path_override_handler(get_handler)
        .range_uri_path_override_handler(range_recorder)
        .range_uri_path_override_handler(range_handler)
        .start()
        .unwrap();

    let env = TestEnvBuilder::new().build().await;
    env.register_repo(&served_repository).await;
    let package_dir = env.resolve_package(&pkg_url).await.expect("package to resolve");
    pkg.verify_contents(&package_dir).await.expect("correct package contents");

    env.assert_count_events(
        metrics::FETCH_BLOB_METRIC_ID,
        vec![(
            metrics::FetchBlobMetricDimensionResult::Success,
            metrics::FetchBlobMetricDimensionResumed::True,
        )],
    )
    .await;

    let history = history.take();
    assert_eq!(history.len(), 2);
    assert_eq!(history[0].uri_path().to_str().unwrap(), path_to_override);
    assert_eq!(history[1].uri_path().to_str().unwrap(), path_to_override);
    let range0: HttpRange = history[0].range().try_into().unwrap();
    assert!(range0.start() > 0);
    let meta_far_size = meta_far_size(&pkg);
    assert_eq!(range0.end() + 1, meta_far_size);
    let range1: HttpRange = history[1].range().try_into().unwrap();
    assert!(range1.start() > range0.start());
    assert_eq!(range1.end() + 1, meta_far_size);

    env.stop().await;
}

// Sets the start of the Content-Range to 0, which will always be invalid for blob resumption.
struct ContentRangeCorruptor;
impl RangeUriPathHandler for ContentRangeCorruptor {
    fn handle(
        &self,
        _: &Path,
        _: &http::HeaderValue,
        mut response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        *response.headers_mut().get_mut(http::header::CONTENT_RANGE).unwrap() =
            http::HeaderValue::from_str("bytes 0-1/2").unwrap();
        futures::future::ready(response).boxed()
    }
}

#[fasync::run_singlethreaded(test)]
async fn resume_validates_content_range() {
    let pkg = PackageBuilder::new("large_meta_far")
        .add_resource_at(
            "meta/large_file",
            vec![0; FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING].as_slice(),
        )
        .build()
        .await
        .unwrap();
    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());
    let path_to_override = format!("/blobs/{}", pkg.meta_far_merkle_root());

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );

    let get_handler =
        handler::ForPath::new(path_to_override.clone(), handler::OneByteShortThenError);

    let served_repository = repo
        .server()
        .uri_path_override_handler(get_handler)
        .range_uri_path_override_handler(ContentRangeCorruptor)
        .start()
        .unwrap();

    let env = TestEnvBuilder::new().build().await;
    env.register_repo(&served_repository).await;
    assert_eq!(env.resolve_package(&pkg_url).await.unwrap_err(), Status::UNAVAILABLE);

    env.assert_count_events(
        metrics::FETCH_BLOB_METRIC_ID,
        vec![
            (
                metrics::FetchBlobMetricDimensionResult::InvalidContentRangeHeader,
                metrics::FetchBlobMetricDimensionResumed::True,
            );
            2
        ],
    )
    .await;

    env.stop().await;
}

// Sets the Content-Length to the returned Range + FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING
// If the hyper client batches more body than the bad Content-Length, the request will fail with a
// hyper error instead of making it to the application logic. This means that for the test to work
// the Range request needs to be for more bytes than
// FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING and the bad Content-Length needs to be for
// longer than the remaining bytes.
struct ContentLengthCorruptor;
impl RangeUriPathHandler for ContentLengthCorruptor {
    fn handle(
        &self,
        _: &Path,
        range: &http::HeaderValue,
        mut response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        let range: HttpRange = range.try_into().unwrap();
        let length = 1 + range.end() - range.start();
        let bad_length = length + FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING as u64;
        response.headers_mut().insert(
            http::header::CONTENT_LENGTH,
            http::HeaderValue::from_str(&format!("{}", bad_length)).unwrap(),
        );
        // The hyper server (if built with debug_assertions) validates the Content-Length header
        // against the actual Body size, if it is known, so to get the bad Content-Length through
        // to the application we need to prevent the server from determining the Body size.
        // https://github.com/hyperium/hyper/blob/8f93123/src/proto/h1/role.rs#L404-L421
        *response.body_mut() =
            Body::wrap_stream(futures::stream::pending::<Result<Vec<u8>, String>>());
        futures::future::ready(response).boxed()
    }
}

#[fasync::run_singlethreaded(test)]
async fn resume_validates_content_length() {
    let pkg = PackageBuilder::new("large_meta_far")
        .add_resource_at(
            "meta/large_file",
            vec![0; 3 * FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING].as_slice(),
        )
        .build()
        .await
        .unwrap();
    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());
    let path_to_override = format!("/blobs/{}", pkg.meta_far_merkle_root());

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );

    let get_handler = handler::ForPath::new(
        path_to_override.clone(),
        handler::NBytesThenError::new(FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING),
    );

    let served_repository = repo
        .server()
        .uri_path_override_handler(get_handler)
        .range_uri_path_override_handler(ContentLengthCorruptor)
        .start()
        .unwrap();

    let env = TestEnvBuilder::new().build().await;
    env.register_repo(&served_repository).await;
    assert_eq!(env.resolve_package(&pkg_url).await.unwrap_err(), Status::UNAVAILABLE);

    env.assert_count_events(
        metrics::FETCH_BLOB_METRIC_ID,
        vec![
            (
                metrics::FetchBlobMetricDimensionResult::ContentLengthContentRangeMismatch,
                metrics::FetchBlobMetricDimensionResumed::True,
            );
            2
        ],
    )
    .await;

    env.stop().await;
}

// Sets the status code to 200 (should be 206).
struct StatusCodeCorruptor;
impl RangeUriPathHandler for StatusCodeCorruptor {
    fn handle(
        &self,
        _: &Path,
        _: &http::HeaderValue,
        mut response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        *response.status_mut() = http::StatusCode::OK;
        futures::future::ready(response).boxed()
    }
}

#[fasync::run_singlethreaded(test)]
async fn resume_validates_206_status() {
    let pkg = PackageBuilder::new("large_meta_far")
        .add_resource_at(
            "meta/large_file",
            vec![0; FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING].as_slice(),
        )
        .build()
        .await
        .unwrap();
    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());
    let path_to_override = format!("/blobs/{}", pkg.meta_far_merkle_root());

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );

    let get_handler =
        handler::ForPath::new(path_to_override.clone(), handler::OneByteShortThenError);

    let served_repository = repo
        .server()
        .uri_path_override_handler(get_handler)
        .range_uri_path_override_handler(StatusCodeCorruptor)
        .start()
        .unwrap();

    let env = TestEnvBuilder::new().build().await;
    env.register_repo(&served_repository).await;
    assert_eq!(env.resolve_package(&pkg_url).await.unwrap_err(), Status::UNAVAILABLE);

    env.assert_count_events(
        metrics::FETCH_BLOB_METRIC_ID,
        vec![
            (
                metrics::FetchBlobMetricDimensionResult::ExpectedHttpStatus206,
                metrics::FetchBlobMetricDimensionResumed::True,
            );
            2
        ],
    )
    .await;

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn resume_enforces_max_resumption_limit() {
    let pkg = PackageBuilder::new("large_meta_far")
        .add_resource_at(
            "meta/large_file",
            vec![0; FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING].as_slice(),
        )
        .build()
        .await
        .unwrap();
    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());
    let path_to_override = format!("/blobs/{}", pkg.meta_far_merkle_root());

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );

    let get_handler = handler::ForPath::new(
        path_to_override.clone(),
        handler::NBytesThenError::new(FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING),
    );

    let served_repository = repo.server().uri_path_override_handler(get_handler).start().unwrap();

    let env = TestEnvBuilder::new().blob_download_resumption_attempts_limit(0).build().await;
    env.register_repo(&served_repository).await;
    assert_eq!(env.resolve_package(&pkg_url).await.unwrap_err(), Status::UNAVAILABLE);

    env.assert_count_events(
        metrics::FETCH_BLOB_METRIC_ID,
        vec![
            (
                metrics::FetchBlobMetricDimensionResult::ExceededResumptionAttemptLimit,
                metrics::FetchBlobMetricDimensionResumed::True,
            );
            2
        ],
    )
    .await;

    env.stop().await;
}
