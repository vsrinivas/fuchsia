// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    cobalt_sw_delivery_registry as metrics,
    fidl_fuchsia_pkg_ext::RepositoryConfigBuilder,
    fuchsia_pkg_testing::{serve::responder, PackageBuilder, RepositoryBuilder},
    lib::{ResolverVariant, TestEnvBuilder, EMPTY_REPO_PATH},
    std::sync::Arc,
};

#[fuchsia::test]
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

    assert_eq!(result.unwrap_err(), fidl_fuchsia_pkg::ResolveError::UnavailableRepoMetadata);

    env.stop().await;
}

#[fuchsia::test]
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

    assert_eq!(result.unwrap_err(), fidl_fuchsia_pkg::ResolveError::UnavailableRepoMetadata);

    env.stop().await;
}

#[fuchsia::test]
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

    assert_eq!(result.unwrap_err(), fidl_fuchsia_pkg::ResolveError::UnavailableRepoMetadata);

    env.assert_count_events(
        metrics::CREATE_TUF_CLIENT_MIGRATED_METRIC_ID,
        vec![metrics::CreateTufClientMigratedMetricDimensionResult::DeadlineExceeded],
    )
    .await;

    env.stop().await;
}

#[fuchsia::test]
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
        metrics::UPDATE_TUF_CLIENT_MIGRATED_METRIC_ID,
        vec![metrics::UpdateTufClientMigratedMetricDimensionResult::DeadlineExceeded],
    )
    .await;

    env.stop().await;
}

#[fuchsia::test]
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
        metrics::FETCH_BLOB_MIGRATED_METRIC_ID,
        vec![
            (
                metrics::FetchBlobMigratedMetricDimensionResult::BlobHeaderDeadlineExceeded,
                metrics::FetchBlobMigratedMetricDimensionResumed::False
            );
            2
        ],
    )
    .await;
    env.stop().await;
}

#[fuchsia::test]
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
//  1.  Resolve a package that has at least 3*MAX_CONCURRENT_BLOB_FETCHES + 1 unique content blobs.
//  2.  Have the blob server return the meta.far successfully, so that the pkg-resolver
//      can enqueue all the content blobs.
//  3.  Have the blob server return an error for the first requested content blob path and then hang
//      requests for any subsequent content blob paths.
//  4.  The first resolve should fail, since failure to download a content blob fails the entire
//      resolve.
//  6.  Reset the blob server's served blobs counter.
//  7.  Have the blob server stop hanging the remaining content blob requests.
//  8.  Resolve a new package with a different meta.far. This forces the package resolver
//      to add the meta.far to the FIFO BlobFetchQueue and wait for it to be processed.
//  9.  Wait for the second resolve to finish.
//
//  If the pkg-resolver does not stop downloading content blobs when a resolve fails, then when
//  the blob server is unblocked in step 7, there will be at least 3*MAX_CONCURRENT_BLOB_FETCHES
//  in the blob queue. Worst case, all the active fetches in the queue have already made their
//  http requests, so, ignoring retries, by the time the second resolve completes, the blob server
//  would have seen at least MAX_CONCURRENT_BLOB_FETCHES + 2 http requests:
//    a. one for the second meta.far
//    b. MAX_CONCURRENT_BLOB_FETCHES + 1 to get the number of fetches in the queue down from
//       3*MAX to MAX-1 (assuming MAX had already made an http request and were hanging) so that
//       the second meta.far could be processed.
//
//  If the pkg-resolver does stop downloading content blobs when a resolve fails, then when the
//  blob server is unblocked in step 7, there will be at least 3*MAX_CONCURRENT_BLOB_FETCHES in the
//  queue. Worst case, all of the active fetches have already called NeededBlobs.OpenBlob (the call
//  that starts to fail after the resolve fails) before the first resolve failed, but have not made
//  their http requests. By the time the second resolve completes, the blob server could have seen
//  at most MAX_CONCURRENT_BLOB_FETCHES + 1 http requests:
//    a. one for the second meta.far
//    b. MAX_CONCURRENT_BLOB_FETCHES for each of the active fetches in the queue when the first
//       resolve failed. Retries for these blobs or fetches for the remaining content blobs from
//       the first package will not cause additional http requests because all these attempts would
//       occur after the first resolve completed, at which time NeededBlobs.Abort has been called
//       and awaited, so all calls to NeededBlobs.OpenBlob will fail, which occurs before the http
//       request is made.
//
//  Therefore the test passes if the blob server's served blobs counter
//  <= MAX_CONCURRENT_BLOB_FETCHES + 1 after the second resolve completes.
#[fuchsia::test]
async fn failed_resolve_stops_fetching_blobs() {
    let pkg_many_failing_content_blobs = {
        let mut pkg = PackageBuilder::new("many-blobs");
        // Must be at least 3*MAX_CONCURRENT_BLOB_FETCHES + 1.
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

    let (fail_content_blobs, unblock) = responder::FailOneThenTemporarilyBlock::new();
    let first_meta_far_http_path =
        format!("/blobs/{}", pkg_many_failing_content_blobs.meta_far_merkle_root());
    let second_meta_far_http_path =
        format!("/blobs/{}", pkg_only_meta_far_different_hash.meta_far_merkle_root());
    let fail_content_blobs = responder::Filter::new(
        move |req: &hyper::Request<hyper::Body>| {
            req.uri().path() != first_meta_far_http_path
                && req.uri().path() != second_meta_far_http_path
        },
        fail_content_blobs,
    );

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
    unblock.send(()).unwrap();

    let _ = env.resolve_package("fuchsia-pkg://test/different-hash").await.unwrap();

    let fetch_count = history.take().len();
    assert!(
        fetch_count <= 3,
        "fetch_count should be <= MAX_CONCURRENT_BLOB_FETCHES+1, was {}",
        fetch_count
    );
}
