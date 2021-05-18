// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    cobalt_sw_delivery_registry as metrics,
    fidl_fuchsia_pkg_ext::RepositoryConfigBuilder,
    fuchsia_async as fasync,
    fuchsia_pkg_testing::{serve::responder, PackageBuilder, RepositoryBuilder},
    lib::{ResolverVariant, TestEnvBuilder, EMPTY_REPO_PATH},
    std::sync::Arc,
};

#[fasync::run_singlethreaded(test)]
async fn resolve_disallow_local_mirror_fails() {
    let pkg = PackageBuilder::new("test")
        .add_resource_at("test_file", "hi there".as_bytes())
        .build()
        .await
        .unwrap();

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );

    // Local mirror defaults not being enabled.
    let env = TestEnvBuilder::new()
        .local_mirror_repo(&repo, "fuchsia-pkg://test".parse().unwrap())
        .build()
        .await;
    let repo_config = repo.make_repo_config("fuchsia-pkg://test".parse().unwrap(), None, true);
    env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());
    let result = env.resolve_package(&pkg_url).await;

    assert_eq!(result.unwrap_err(), fidl_fuchsia_pkg::ResolveError::Internal);

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn resolve_local_and_remote_mirrors_fails() {
    let pkg = PackageBuilder::new("test")
        .add_resource_at("test_file", "hi there".as_bytes())
        .build()
        .await
        .unwrap();

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );

    let env = TestEnvBuilder::new()
        .resolver_variant(ResolverVariant::AllowLocalMirror)
        .local_mirror_repo(&repo, "fuchsia-pkg://test".parse().unwrap())
        .build()
        .await;
    let server = repo.server().start().expect("Starting server succeeds");
    let repo_config = server.make_repo_config("fuchsia-pkg://test".parse().unwrap());
    let repo_config = RepositoryConfigBuilder::from(repo_config).use_local_mirror(true).build();
    env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());
    let result = env.resolve_package(&pkg_url).await;

    assert_eq!(result.unwrap_err(), fidl_fuchsia_pkg::ResolveError::Internal);

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn create_tuf_client_timeout() {
    let repo =
        Arc::new(RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).build().await.unwrap());

    let env = TestEnvBuilder::new()
        .resolver_variant(ResolverVariant::ZeroTufMetadataTimeout)
        .build()
        .await;
    let server = repo
        .server()
        .response_overrider(responder::ForPath::new("/1.root.json", responder::Hang))
        .start()
        .expect("Starting server succeeds");
    let repo_config = server.make_repo_config("fuchsia-pkg://test".parse().unwrap());
    env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    // The package does not need to exist in the repository, because the resolve will fail before
    // it obtains metadata.
    let result = env.resolve_package("fuchsia-pkg://test/missing-package").await;

    assert_eq!(result.unwrap_err(), fidl_fuchsia_pkg::ResolveError::Internal);

    env.assert_count_events(
        metrics::CREATE_TUF_CLIENT_METRIC_ID,
        vec![metrics::CreateTufClientMetricDimensionResult::DeadlineExceeded],
    )
    .await;

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn update_tuf_client_timeout() {
    let repo =
        Arc::new(RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).build().await.unwrap());

    // pkg-resolver uses this timeout when creating and updating tuf metadata, so since this test
    // hangs the update, the timeout needs to be long enough for the create to succeed.
    // TODO(fxbug.dev/66946) have separate tuf client create and update timeout durations.
    let env = TestEnvBuilder::new()
        .resolver_variant(ResolverVariant::ShortTufMetadataTimeout)
        .build()
        .await;

    // pkg-resolver uses tuf::client::Client::with_trusted_root_keys to create its TUF client.
    // That method will only retrieve the specified version of the root metadata (1 for these
    // tests), with the rest of the metadata being retrieved during the first update. This means
    // that hanging all attempts for timestamp.json metadata will allow tuf client creation to
    // succeed but still fail tuf client update.
    let server = repo
        .server()
        .response_overrider(responder::ForPath::new("/timestamp.json", responder::Hang))
        .start()
        .expect("Starting server succeeds");
    let repo_config = server.make_repo_config("fuchsia-pkg://test".parse().unwrap());
    env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    // The package does not need to exist in the repository, because the resolve will fail before
    // it obtains metadata.
    let result = env.resolve_package("fuchsia-pkg://test/missing-package").await;

    // The resolve will still fail, even though pkg-resolver normally ignores failed tuf updates,
    // see fxbug.dev/43646, because the tuf client actually downloads most of the metadata during
    // the first update, not during creation.
    assert_eq!(result.unwrap_err(), fidl_fuchsia_pkg::ResolveError::Internal);

    env.assert_count_events(
        metrics::UPDATE_TUF_CLIENT_METRIC_ID,
        vec![metrics::UpdateTufClientMetricDimensionResult::DeadlineExceeded],
    )
    .await;

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn download_blob_header_timeout() {
    let pkg = PackageBuilder::new("test").build().await.unwrap();
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );

    let env = TestEnvBuilder::new()
        .resolver_variant(ResolverVariant::ZeroBlobNetworkHeaderTimeout)
        .build()
        .await;

    let server = repo
        .server()
        .response_overrider(responder::ForPathPrefix::new("/blobs/", responder::Hang))
        .start()
        .expect("Starting server succeeds");
    let repo_config = server.make_repo_config("fuchsia-pkg://test".parse().unwrap());
    env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    let result = env.resolve_package("fuchsia-pkg://test/test").await;
    assert_eq!(result.unwrap_err(), fidl_fuchsia_pkg::ResolveError::UnavailableBlob);

    env.assert_count_events(
        metrics::FETCH_BLOB_METRIC_ID,
        vec![
            (
                metrics::FetchBlobMetricDimensionResult::BlobHeaderDeadlineExceeded,
                metrics::FetchBlobMetricDimensionResumed::False
            );
            2
        ],
    )
    .await;
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn download_blob_body_timeout() {
    let pkg = PackageBuilder::new("test").build().await.unwrap();
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );

    let env = TestEnvBuilder::new()
        .resolver_variant(ResolverVariant::ZeroBlobNetworkBodyTimeout)
        .build()
        .await;

    let server = repo
        .server()
        .response_overrider(responder::ForPathPrefix::new("/blobs/", responder::HangBody))
        .start()
        .expect("Starting server succeeds");
    let repo_config = server.make_repo_config("fuchsia-pkg://test".parse().unwrap());
    env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    let result = env.resolve_package("fuchsia-pkg://test/test").await;
    assert_eq!(result.unwrap_err(), fidl_fuchsia_pkg::ResolveError::UnavailableBlob);
}

// Verify that the pkg-resolver stops downloading content blobs when a package fails to resolve.
// Steps:
//  1.  Resolve a package that has a lot more than double MAX_CONCURRENT_BLOB_FETCHES content blobs.
//  2.  Have the blob server return the meta.far successfully, so that the pkg-resolver
//      can enqueue all the content blobs.
//  3.  Have the blob server fail to return any of the content blobs.
//  4.  The first resolve should fail, since none of the content blobs are obtainable.
//  5.  After the first resolve fails, only about MAX_CONCURRENT_BLOB_FETCHES blobs should have been
//      removed from the fetch queue (unfortunately we cannot upper bound the number of blobs
//      removed from the fetch queue, because the blob fetch queue fetches blobs concurrently with
//      the resolve, so there is a window of time in between MAX_CONCURRENT_BLOB_FETCHES fetches
//      failing and the pkg-resolver cancelling the PackageCache.Get in which the BlobFetchQueue
//      could start another fetch. In practice, this does not happen.)
//  6.  Reset the blob server's served blobs counter.
//  7.  Assuming the original package had enough content blobs, there should still be more than
//      MAX_CONCURRENT_BLOB_FETCHES blobs in the BlobFetchQueue.
//  8.  Resolve a new package with a different meta.far. This forces the package resolver
//      to add the meta.far to the BlobFetchQueue and wait for it to be processed.
//  9.  Wait for the second resolve to finish. Because the BlobFetchQueue is FIFO and had more than
//      MAX_CONCURRENT_BLOB_FETCHES elements, at least one more content blob from the original
//      package has to have been processed.
//  10. The blob server's served blob counter should be one (the meta.far for the second package),
//      because processing the remaining content blobs should fail at the pkg-cache layer.
#[fasync::run_singlethreaded(test)]
async fn failed_resolve_stops_fetching_blobs() {
    let pkg_many_failing_content_blobs = {
        let mut pkg = PackageBuilder::new("many-blobs");
        // Must be much larger than double MAX_CONCURRENT_BLOB_FETCHES.
        for i in 0..40 {
            pkg = pkg.add_resource_at(&format!("blob_{}", i), format!("contents_{}", i).as_bytes());
        }
        pkg.build().await.unwrap()
    };
    let pkg_only_meta_far_different_hash = PackageBuilder::new("different-hash")
        .add_resource_at("meta/file", &b"random words"[..])
        .build()
        .await
        .unwrap();
    assert_ne!(
        pkg_many_failing_content_blobs.meta_far_merkle_root(),
        pkg_only_meta_far_different_hash.meta_far_merkle_root()
    );

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg_many_failing_content_blobs)
            .add_package(&pkg_only_meta_far_different_hash)
            .build()
            .await
            .unwrap(),
    );

    let env = TestEnvBuilder::new().build().await;

    let (record, history) = responder::Record::new();

    let meta_far_http_path =
        format!("/blobs/{}", pkg_many_failing_content_blobs.meta_far_merkle_root());
    let fail_content_blobs = responder::Filter::new(
        move |req: &hyper::Request<hyper::Body>| req.uri().path() != meta_far_http_path,
        responder::StaticResponseCode::not_found(),
    );
    let should_fail = responder::AtomicToggle::new(true);
    let fail_content_blobs = responder::Toggleable::new(&should_fail, fail_content_blobs);

    let server = repo
        .server()
        .response_overrider(responder::ForPathPrefix::new("/blobs/", record))
        .response_overrider(responder::ForPathPrefix::new("/blobs/", fail_content_blobs))
        .start()
        .expect("Starting server succeeds");
    let repo_config = server.make_repo_config("fuchsia-pkg://test".parse().unwrap());
    env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    let result = env.resolve_package("fuchsia-pkg://test/many-blobs").await;
    history.take();
    assert_eq!(result.unwrap_err(), fidl_fuchsia_pkg::ResolveError::UnavailableBlob);

    should_fail.unset();

    let _ = env.resolve_package("fuchsia-pkg://test/different-hash").await.unwrap();

    assert_eq!(history.take().len(), 1);
}
