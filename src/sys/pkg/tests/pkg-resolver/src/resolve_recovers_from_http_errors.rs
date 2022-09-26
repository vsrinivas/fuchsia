// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests the property that pkg_resolver does not enter a bad
/// state (successfully handles retries) when the TUF server errors while
/// servicing fuchsia.pkg.PackageResolver.Resolve FIDL requests.
use {
    assert_matches::assert_matches,
    fuchsia_merkle::MerkleTree,
    fuchsia_pkg_testing::{
        serve::{responder, Domain, HttpResponder},
        Package, PackageBuilder, RepositoryBuilder,
    },
    lib::{
        extra_blob_contents, make_pkg_with_extra_blobs, ResolverVariant, TestEnvBuilder,
        EMPTY_REPO_PATH, FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING,
    },
    std::{net::Ipv4Addr, sync::Arc},
};

async fn verify_resolve_fails_then_succeeds<H: HttpResponder>(
    pkg: Package,
    responder: H,
    failure_error: fidl_fuchsia_pkg::ResolveError,
) {
    let env = TestEnvBuilder::new().build().await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );
    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());

    let should_fail = responder::AtomicToggle::new(true);
    let served_repository = repo
        .server()
        .response_overrider(responder::Toggleable::new(&should_fail, responder))
        .response_overrider(responder::Filter::new(
            responder::is_range_request,
            responder::StaticResponseCode::server_error(),
        ))
        .start()
        .unwrap();
    env.register_repo(&served_repository).await;

    // First resolve fails with the expected error.
    assert_matches!(env.resolve_package(&pkg_url).await, Err(error) if error == failure_error);

    // Disabling the custom responder allows the subsequent resolves to succeed.
    should_fail.unset();
    let (package_dir, _resolved_context) =
        env.resolve_package(&pkg_url).await.expect("package to resolve");
    pkg.verify_contents(&package_dir).await.expect("correct package contents");

    env.stop().await;
}

#[fuchsia::test]
async fn second_resolve_succeeds_when_far_404() {
    let pkg = make_pkg_with_extra_blobs("second_resolve_succeeds_when_far_404", 1).await;
    let path_to_override = format!("/blobs/{}", pkg.meta_far_merkle_root());

    verify_resolve_fails_then_succeeds(
        pkg,
        responder::ForPath::new(path_to_override, responder::StaticResponseCode::not_found()),
        fidl_fuchsia_pkg::ResolveError::UnavailableBlob,
    )
    .await
}

#[fuchsia::test]
async fn second_resolve_succeeds_when_blob_404() {
    let pkg = make_pkg_with_extra_blobs("second_resolve_succeeds_when_blob_404", 1).await;
    let path_to_override = format!(
        "/blobs/{}",
        MerkleTree::from_reader(
            extra_blob_contents("second_resolve_succeeds_when_blob_404", 0).as_slice()
        )
        .expect("merkle slice")
        .root()
    );

    verify_resolve_fails_then_succeeds(
        pkg,
        responder::ForPath::new(path_to_override, responder::StaticResponseCode::not_found()),
        fidl_fuchsia_pkg::ResolveError::UnavailableBlob,
    )
    .await
}

#[fuchsia::test]
async fn second_resolve_succeeds_when_far_errors_mid_download() {
    let pkg = PackageBuilder::new("second_resolve_succeeds_when_far_errors_mid_download")
        .add_resource_at(
            "meta/large_file",
            vec![0; FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING].as_slice(),
        )
        .build()
        .await
        .unwrap();
    let path_to_override = format!("/blobs/{}", pkg.meta_far_merkle_root());

    verify_resolve_fails_then_succeeds(
        pkg,
        responder::ForPath::new(path_to_override, responder::OneByteShortThenError),
        fidl_fuchsia_pkg::ResolveError::UnavailableBlob,
    )
    .await
}

#[fuchsia::test]
async fn second_resolve_succeeds_when_blob_errors_mid_download() {
    let blob = vec![0; FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING];
    let pkg = PackageBuilder::new("second_resolve_succeeds_when_blob_errors_mid_download")
        .add_resource_at("blobbity/blob", blob.as_slice())
        .build()
        .await
        .unwrap();
    let path_to_override = format!(
        "/blobs/{}",
        MerkleTree::from_reader(blob.as_slice()).expect("merkle slice").root()
    );

    verify_resolve_fails_then_succeeds(
        pkg,
        responder::ForPath::new(path_to_override, responder::OneByteShortThenError),
        fidl_fuchsia_pkg::ResolveError::UnavailableBlob,
    )
    .await
}

#[fuchsia::test]
async fn second_resolve_succeeds_disconnect_before_far_complete() {
    let pkg = PackageBuilder::new("second_resolve_succeeds_disconnect_before_far_complete")
        .add_resource_at(
            "meta/large_file",
            vec![0; FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING].as_slice(),
        )
        .build()
        .await
        .unwrap();
    let path_to_override = format!("/blobs/{}", pkg.meta_far_merkle_root());

    verify_resolve_fails_then_succeeds(
        pkg,
        responder::ForPath::new(path_to_override, responder::OneByteShortThenDisconnect),
        fidl_fuchsia_pkg::ResolveError::UnavailableBlob,
    )
    .await
}

#[fuchsia::test]
async fn second_resolve_succeeds_disconnect_before_blob_complete() {
    let blob = vec![0; FILE_SIZE_LARGE_ENOUGH_TO_TRIGGER_HYPER_BATCHING];
    let pkg = PackageBuilder::new("second_resolve_succeeds_disconnect_before_blob_complete")
        .add_resource_at("blobbity/blob", blob.as_slice())
        .build()
        .await
        .unwrap();
    let path_to_override = format!(
        "/blobs/{}",
        MerkleTree::from_reader(blob.as_slice()).expect("merkle slice").root()
    );

    verify_resolve_fails_then_succeeds(
        pkg,
        responder::ForPath::new(path_to_override, responder::OneByteShortThenDisconnect),
        fidl_fuchsia_pkg::ResolveError::UnavailableBlob,
    )
    .await
}

#[fuchsia::test]
async fn second_resolve_succeeds_when_far_corrupted() {
    let pkg = make_pkg_with_extra_blobs("second_resolve_succeeds_when_far_corrupted", 1).await;
    let path_to_override = format!("/blobs/{}", pkg.meta_far_merkle_root());

    verify_resolve_fails_then_succeeds(
        pkg,
        responder::ForPath::new(path_to_override, responder::OneByteFlipped),
        fidl_fuchsia_pkg::ResolveError::Io,
    )
    .await
}

#[fuchsia::test]
async fn second_resolve_succeeds_when_blob_corrupted() {
    let pkg = make_pkg_with_extra_blobs("second_resolve_succeeds_when_blob_corrupted", 1).await;
    let blob = extra_blob_contents("second_resolve_succeeds_when_blob_corrupted", 0);
    let path_to_override = format!(
        "/blobs/{}",
        MerkleTree::from_reader(blob.as_slice()).expect("merkle slice").root()
    );

    verify_resolve_fails_then_succeeds(
        pkg,
        responder::ForPath::new(path_to_override, responder::OneByteFlipped),
        fidl_fuchsia_pkg::ResolveError::Io,
    )
    .await
}

#[fuchsia::test]
async fn second_resolve_succeeds_when_tuf_metadata_update_fails() {
    // pkg-resolver uses tuf::client::Client::with_trusted_root_keys to create its TUF client.
    // That method will only retrieve the specified version of the root metadata (1 for these
    // tests), with the rest of the metadata being retrieved during the first update. This means
    // that hanging all attempts for 2.snapshot.json metadata will allow tuf client creation to
    // succeed but still fail tuf client update.
    // We want to specifically verify recovery from update failure because if creation fails,
    // pkg-resolver will not make a Repository object, so the next resolve attempt would try again
    // from scratch, but if update fails, pkg-resolver will keep its Repository object which
    // contains a rust-tuf client in a possibly invalid state, and we want to verify that
    // pkg-resolver calls update on the client again and that this update recovers the client.
    let pkg = PackageBuilder::new("second_resolve_succeeds_when_tuf_metadata_update_fails")
        .build()
        .await
        .unwrap();
    verify_resolve_fails_then_succeeds(
        pkg,
        responder::ForPath::new("/2.snapshot.json", responder::OneByteShortThenDisconnect),
        fidl_fuchsia_pkg::ResolveError::Internal,
    )
    .await
}

// The hyper clients used by the pkg-resolver to download blobs and TUF metadata sometimes end up
// waiting on operations on their TCP connections that will never return (e.g. because of an
// upstream network partition). To detect this, the pkg-resolver wraps the hyper client response
// futures with timeout futures. To recover from this, the pkg-resolver drops the hyper client
// response futures when the timeouts are hit. This recovery plan requires that dropping the hyper
// response future causes hyper to close the underlying TCP connection and create a new one the
// next time hyper is asked to perform a network operation. This assumption holds for http1, but
// not for http2.
//
// This test verifies the "dropping a hyper response future prevents the underlying connection
// from being reused" requirement. It does so by verifying that if a resolve fails due to a blob
// download timeout and the resolve is retried, the retry will cause pkg-resolver to make an
// additional TCP connection to the blob mirror.
//
// This test uses https because the test exists to catch changes to the Fuchsia hyper client
// that would cause pkg-resolver to use http2 before the Fuchsia hyper client is able to recover
// from bad TCP connections when using http2. The pkg-resolver does not explicitly enable http2
// on its hyper clients, so the way this change would sneak in is if the hyper client is changed
// to use ALPN to prefer http2. The blob server used in this test has ALPN configured to prefer
// http2.
#[fuchsia::test]
async fn blob_timeout_causes_new_tcp_connection() {
    let pkg = PackageBuilder::new("blob_timeout_causes_new_tcp_connection").build().await.unwrap();
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
        .response_overrider(responder::ForPathPrefix::new(
            "/blobs/",
            responder::Once::new(responder::HangBody),
        ))
        .use_https_domain(Domain::TestFuchsiaCom)
        .bind_to_addr(Ipv4Addr::LOCALHOST)
        .start()
        .expect("Starting server succeeds");

    env.register_repo(&server).await;

    assert_eq!(server.connection_attempts(), 0);
    // The resolve request may not succeed despite the retry: the zero timeout on the blob body
    // future can fire prior to the body being downloaded on the retry. However, we expect to
    // observe three connections: one for the TUF client, one for the initial resolve that timed
    // out, and one for the retried resolve.
    match env.resolve_package("fuchsia-pkg://test/blob_timeout_causes_new_tcp_connection").await {
        Ok(_) | Err(fidl_fuchsia_pkg::ResolveError::UnavailableBlob) => {}
        Err(e) => {
            panic!("unexpected error: {:?}", e);
        }
    };
    assert_eq!(server.connection_attempts(), 3);

    env.stop().await;
}
