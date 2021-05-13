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
