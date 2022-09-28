// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cobalt_sw_delivery_registry as metrics,
    fuchsia_pkg_testing::{
        serve::{responder, HttpRange},
        Package, PackageBuilder, RepositoryBuilder,
    },
    futures::future::{BoxFuture, FutureExt as _},
    hyper::{Body, Response},
    lib::{
        ResolverVariant, TestEnvBuilder, EMPTY_REPO_PATH,
        FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING,
    },
    std::{convert::TryInto as _, sync::Arc},
};

fn meta_far_size(pkg: &Package) -> u64 {
    pkg.meta_far().unwrap().metadata().unwrap().len()
}

fn for_range_requests<T: fuchsia_pkg_testing::serve::HttpResponder>(
    responder: T,
) -> impl fuchsia_pkg_testing::serve::HttpResponder {
    responder::Filter::new(responder::is_range_request, responder)
}

fn for_not_range_requests<T: fuchsia_pkg_testing::serve::HttpResponder>(
    responder: T,
) -> impl fuchsia_pkg_testing::serve::HttpResponder {
    responder::Filter::new(
        |req: &hyper::Request<Body>| !responder::is_range_request(req),
        responder,
    )
}

#[fuchsia::test]
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

    let get_responder = for_not_range_requests(responder::ForPath::new(
        path_to_override.clone(),
        responder::OneByteShortThenError,
    ));

    let (range_responder, history) = responder::Record::new();
    let range_responder = for_range_requests(range_responder);

    let served_repository = repo
        .server()
        .response_overrider(get_responder)
        .response_overrider(range_responder)
        .start()
        .unwrap();

    let env = TestEnvBuilder::new().build().await;
    env.register_repo(&served_repository).await;
    let (package_dir, _resolved_context) =
        env.resolve_package(&pkg_url).await.expect("package to resolve");
    pkg.verify_contents(&package_dir).await.expect("correct package contents");

    env.assert_count_events(
        metrics::FETCH_BLOB_MIGRATED_METRIC_ID,
        vec![(
            metrics::FetchBlobMigratedMetricDimensionResult::Success,
            metrics::FetchBlobMigratedMetricDimensionResumed::True,
        )],
    )
    .await;

    let history = history.take();
    assert_eq!(history.len(), 1);
    assert_eq!(history[0].uri_path().to_str().unwrap(), path_to_override);
    let range: HttpRange =
        history[0].headers().get(http::header::RANGE).unwrap().try_into().unwrap();
    assert!(range.first_byte_pos() > 0);
    assert_eq!(range.last_byte_pos() + 1, meta_far_size(&pkg));

    env.stop().await;
}

#[fuchsia::test]
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

    let get_responder = for_not_range_requests(responder::ForPath::new(
        path_to_override.clone(),
        responder::NBytesThenError::new(FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING),
    ));

    let (recorder, history) = responder::Record::new();
    let range_responder = for_range_requests(responder::Chain::new(vec![
        Box::new(recorder),
        Box::new(responder::Once::new(responder::NBytesThenError::new(
            FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING,
        ))),
    ]));

    let served_repository = repo
        .server()
        .response_overrider(get_responder)
        .response_overrider(range_responder)
        .start()
        .unwrap();

    let env = TestEnvBuilder::new().build().await;
    env.register_repo(&served_repository).await;
    let (package_dir, _resolved_context) =
        env.resolve_package(&pkg_url).await.expect("package to resolve");
    pkg.verify_contents(&package_dir).await.expect("correct package contents");

    env.assert_count_events(
        metrics::FETCH_BLOB_MIGRATED_METRIC_ID,
        vec![(
            metrics::FetchBlobMigratedMetricDimensionResult::Success,
            metrics::FetchBlobMigratedMetricDimensionResumed::True,
        )],
    )
    .await;

    let history = history.take();
    assert_eq!(history.len(), 2);
    assert_eq!(history[0].uri_path().to_str().unwrap(), path_to_override);
    assert_eq!(history[1].uri_path().to_str().unwrap(), path_to_override);
    let range0: HttpRange =
        history[0].headers().get(http::header::RANGE).unwrap().try_into().unwrap();
    assert!(range0.first_byte_pos() > 0);
    let meta_far_size = meta_far_size(&pkg);
    assert_eq!(range0.last_byte_pos() + 1, meta_far_size);
    let range1: HttpRange =
        history[1].headers().get(http::header::RANGE).unwrap().try_into().unwrap();
    assert!(range1.first_byte_pos() > range0.first_byte_pos());
    assert_eq!(range1.last_byte_pos() + 1, meta_far_size);

    env.stop().await;
}

// Sets the start of the Content-Range to 0, which will always be invalid for blob resumption.
struct ContentRangeCorruptor;
impl fuchsia_pkg_testing::serve::HttpResponder for ContentRangeCorruptor {
    fn respond(
        &self,
        _: &http::Request<Body>,
        mut response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        *response.headers_mut().get_mut(http::header::CONTENT_RANGE).unwrap() =
            http::HeaderValue::from_str("bytes 0-1/2").unwrap();
        futures::future::ready(response).boxed()
    }
}

#[fuchsia::test]
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

    let get_responder =
        responder::ForPath::new(path_to_override.clone(), responder::OneByteShortThenError);

    let served_repository = repo
        .server()
        .response_overrider(for_not_range_requests(get_responder))
        .response_overrider(for_range_requests(ContentRangeCorruptor))
        .start()
        .unwrap();

    let env = TestEnvBuilder::new().build().await;
    env.register_repo(&served_repository).await;
    assert_eq!(
        env.resolve_package(&pkg_url).await.unwrap_err(),
        fidl_fuchsia_pkg::ResolveError::UnavailableBlob
    );

    env.assert_count_events(
        metrics::FETCH_BLOB_MIGRATED_METRIC_ID,
        vec![
            (
                metrics::FetchBlobMigratedMetricDimensionResult::InvalidContentRangeHeader,
                metrics::FetchBlobMigratedMetricDimensionResumed::True,
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
impl fuchsia_pkg_testing::serve::HttpResponder for ContentLengthCorruptor {
    fn respond(
        &self,
        request: &hyper::Request<Body>,
        mut response: Response<Body>,
    ) -> BoxFuture<'_, Response<Body>> {
        let range: HttpRange = request
            .headers()
            .get(http::header::RANGE)
            .expect("missing header")
            .try_into()
            .expect("invalid header");
        let length = 1 + range.last_byte_pos() - range.first_byte_pos();
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

#[fuchsia::test]
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

    let get_responder = responder::ForPath::new(
        path_to_override.clone(),
        responder::NBytesThenError::new(FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING),
    );

    let served_repository = repo
        .server()
        .response_overrider(for_not_range_requests(get_responder))
        .response_overrider(for_range_requests(ContentLengthCorruptor))
        .start()
        .unwrap();

    let env = TestEnvBuilder::new().build().await;
    env.register_repo(&served_repository).await;
    assert_eq!(
        env.resolve_package(&pkg_url).await.unwrap_err(),
        fidl_fuchsia_pkg::ResolveError::UnavailableBlob
    );

    env.assert_count_events(
        metrics::FETCH_BLOB_MIGRATED_METRIC_ID,
        vec![
            (
                metrics::FetchBlobMigratedMetricDimensionResult::ContentLengthContentRangeMismatch,
                metrics::FetchBlobMigratedMetricDimensionResumed::True,
            );
            2
        ],
    )
    .await;

    env.stop().await;
}

#[fuchsia::test]
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

    let get_responder =
        responder::ForPath::new(path_to_override.clone(), responder::OneByteShortThenError);

    let served_repository = repo
        .server()
        .response_overrider(for_not_range_requests(get_responder))
        .response_overrider(for_range_requests(responder::OverwriteStatusCode::new(
            http::StatusCode::OK,
        )))
        .start()
        .unwrap();

    let env = TestEnvBuilder::new().build().await;
    env.register_repo(&served_repository).await;
    assert_eq!(
        env.resolve_package(&pkg_url).await.unwrap_err(),
        fidl_fuchsia_pkg::ResolveError::UnavailableBlob
    );

    env.assert_count_events(
        metrics::FETCH_BLOB_MIGRATED_METRIC_ID,
        vec![
            (
                metrics::FetchBlobMigratedMetricDimensionResult::ExpectedHttpStatus206,
                metrics::FetchBlobMigratedMetricDimensionResumed::True,
            );
            2
        ],
    )
    .await;

    env.stop().await;
}

#[fuchsia::test]
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

    let get_responder = responder::ForPath::new(
        path_to_override.clone(),
        responder::NBytesThenError::new(FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING),
    );

    let served_repository = repo.server().response_overrider(get_responder).start().unwrap();

    let env = TestEnvBuilder::new()
        .resolver_variant(ResolverVariant::ZeroBlobDownloadResumptionAttemptsLimit)
        .build()
        .await;
    env.register_repo(&served_repository).await;
    assert_eq!(
        env.resolve_package(&pkg_url).await.unwrap_err(),
        fidl_fuchsia_pkg::ResolveError::UnavailableBlob
    );

    env.assert_count_events(
        metrics::FETCH_BLOB_MIGRATED_METRIC_ID,
        vec![
            (
                metrics::FetchBlobMigratedMetricDimensionResult::ExceededResumptionAttemptLimit,
                metrics::FetchBlobMigratedMetricDimensionResumed::True,
            );
            2
        ],
    )
    .await;

    env.stop().await;
}
