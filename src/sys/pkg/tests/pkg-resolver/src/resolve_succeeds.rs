// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests the property that pkg_resolver successfully
/// services fuchsia.pkg.PackageResolver.Resolve FIDL requests for
/// different types of packages when blobfs is in various intermediate states.
use {
    assert_matches::assert_matches,
    fidl_fuchsia_pkg_ext::MirrorConfigBuilder,
    fuchsia_async as fasync,
    fuchsia_inspect::assert_data_tree,
    fuchsia_pkg_testing::{
        serve::{responder, Domain},
        Package, PackageBuilder, RepositoryBuilder,
    },
    futures::{join, prelude::*},
    http_uri_ext::HttpUriExt as _,
    lib::{
        extra_blob_contents, make_pkg_with_extra_blobs, resolve_package, test_package_bin,
        test_package_cml, ResolverVariant, TestEnv, TestEnvBuilder, EMPTY_REPO_PATH,
    },
    rand::prelude::*,
    std::{
        collections::HashSet,
        io::{self, Read},
        net::{IpAddr, Ipv4Addr, Ipv6Addr},
        path::Path,
        sync::Arc,
        time::Duration,
    },
};

#[fuchsia::test]
async fn package_resolution() {
    let env = TestEnvBuilder::new().build().await;
    let mut startup_blobs = env.blobfs.list_blobs().unwrap();

    let s = "package_resolution";
    let pkg = PackageBuilder::new(s)
        .add_resource_at(format!("bin/{}", s), &test_package_bin(s)[..])
        .add_resource_at(format!("meta/{}.cml", s), &test_package_cml(s)[..])
        .add_resource_at("data/duplicate_a", "same contents".as_bytes())
        .add_resource_at("data/duplicate_b", "same contents".as_bytes())
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
    let served_repository = Arc::clone(&repo).server().start().unwrap();

    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);

    let () = env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    let (package, _resolved_context) = env
        .resolve_package(format!("fuchsia-pkg://test/{}", s).as_str())
        .await
        .expect("package to resolve without error");

    // Verify the served package directory contains the exact expected contents.
    pkg.verify_contents(&package).await.unwrap();

    // Make sure repo also has the blobs that were in blobfs on startup.
    let mut repo_blobs = repo.list_blobs().unwrap();
    repo_blobs.append(&mut startup_blobs);

    // All blobs in the repository should now be present in blobfs.
    assert_eq!(env.blobfs.list_blobs().unwrap(), repo_blobs);

    env.stop().await;
}

#[fuchsia::test]
async fn separate_blobs_url() {
    let env = TestEnvBuilder::new().build().await;

    let mut startup_blobs = env.blobfs.list_blobs().unwrap();

    let pkg_name = "separate_blobs_url";
    let pkg = make_pkg_with_extra_blobs(pkg_name, 3).await;
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = Arc::clone(&repo).server().start().unwrap();

    // Rename the blobs directory so the blobs can't be found in the usual place.
    // The package resolver currently requires Content-Length headers when downloading content
    // blobs. "pm serve" will gzip compress paths that aren't prefixed with "/blobs", which removes
    // the Content-Length header. To ensure "pm serve" does not compress the blobs stored at this
    // alternate path, its name must start with "blobs".
    let repo_root = repo.path();
    std::fs::rename(repo_root.join("blobs"), repo_root.join("blobsbolb")).unwrap();

    // Configure the repo manager with different TUF and blobs URLs.
    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let mut repo_config = served_repository.make_repo_config(repo_url);
    let mirror = &repo_config.mirrors()[0];
    let mirror = MirrorConfigBuilder::new(mirror.mirror_url().to_owned())
        .unwrap()
        .subscribe(mirror.subscribe())
        .blob_mirror_url(mirror.mirror_url().to_owned().extend_dir_with_path("blobsbolb").unwrap())
        .unwrap()
        .build();
    repo_config.insert_mirror(mirror).unwrap();
    let () = env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    // Verify package installation from the split repo succeeds.
    let (package, _resolved_context) = env
        .resolve_package(format!("fuchsia-pkg://test/{}", pkg_name).as_str())
        .await
        .expect("package to resolve without error");
    pkg.verify_contents(&package).await.unwrap();
    std::fs::rename(repo_root.join("blobsbolb"), repo_root.join("blobs")).unwrap();

    let mut repo_blobs = repo.list_blobs().unwrap();
    repo_blobs.append(&mut startup_blobs);

    assert_eq!(env.blobfs.list_blobs().unwrap(), repo_blobs);

    env.stop().await;
}

async fn verify_resolve_with_altered_env(
    pkg: Package,
    alter_env: impl FnOnce(&TestEnv, &Package),
) -> () {
    let env = TestEnvBuilder::new().build().await;

    let mut startup_blobs = env.blobfs.list_blobs().unwrap();

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = Arc::clone(&repo).server().start().unwrap();

    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);

    let () = env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    alter_env(&env, &pkg);

    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();

    pkg.verify_contents(&package_dir).await.unwrap();

    let mut repo_blobs = repo.list_blobs().unwrap();
    repo_blobs.append(&mut startup_blobs);

    assert_eq!(env.blobfs.list_blobs().unwrap(), repo_blobs);

    env.stop().await;
}

fn verify_resolve(pkg: Package) -> impl Future<Output = ()> {
    verify_resolve_with_altered_env(pkg, |_, _| {})
}

#[fuchsia::test]
async fn meta_far_only() {
    verify_resolve(PackageBuilder::new("uniblob").build().await.unwrap()).await
}

#[fuchsia::test]
async fn meta_far_and_empty_blob() {
    verify_resolve(
        PackageBuilder::new("emptyblob")
            .add_resource_at("data/empty", "".as_bytes())
            .build()
            .await
            .unwrap(),
    )
    .await
}

#[fuchsia::test]
async fn large_compressible_blobs() {
    let s = "large-compressible-blobs";
    verify_resolve(
        PackageBuilder::new(s)
            .add_resource_at("bin/numbers", &test_package_bin(s)[..])
            .add_resource_at("data/ones", io::repeat(1).take(1 * 1024 * 1024))
            .add_resource_at("data/twos", io::repeat(2).take(2 * 1024 * 1024))
            .add_resource_at("data/threes", io::repeat(3).take(3 * 1024 * 1024))
            .build()
            .await
            .unwrap(),
    )
    .await
}

#[fuchsia::test]
async fn large_uncompressible_blobs() {
    let s = "large-uncompressible-blobs";

    let mut rng = StdRng::from_seed([0u8; 32]);
    let rng = &mut rng as &mut dyn RngCore;

    verify_resolve(
        PackageBuilder::new(s)
            .add_resource_at("data/1mb/1", rng.take(1 * 1024 * 1024))
            .add_resource_at("data/1mb/2", rng.take(1 * 1024 * 1024))
            .add_resource_at("data/1mb/3", rng.take(1 * 1024 * 1024))
            .add_resource_at("data/2mb", rng.take(2 * 1024 * 1024))
            .add_resource_at("data/3mb", rng.take(3 * 1024 * 1024))
            .build()
            .await
            .unwrap(),
    )
    .await
}

#[fuchsia::test]
async fn many_blobs() {
    verify_resolve(make_pkg_with_extra_blobs("many_blobs", 200).await).await
}

#[fuchsia::test]
async fn pinned_merkle_resolution() {
    let env = TestEnvBuilder::new().build().await;

    // Since our test harness doesn't yet include a way to update a package, we generate two
    // separate packages to test resolution with a pinned merkle root.
    // We can do this with two packages because the TUF metadata doesn't currently contain
    // package names, only the latest known merkle root for a given name.
    // So, generate two packages, and then resolve one package with the merkle of the other
    // to test resolution with a pinned merkle.
    let pkg1 = PackageBuilder::new("pinned-merkle-foo")
        .add_resource_at("data/foo", "foo".as_bytes())
        .build()
        .await
        .unwrap();
    let pkg2 = PackageBuilder::new("pinned-merkle-bar")
        .add_resource_at("data/bar", "bar".as_bytes())
        .build()
        .await
        .unwrap();

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg1)
            .add_package(&pkg2)
            .build()
            .await
            .unwrap(),
    );

    let served_repository = repo.server().start().unwrap();
    env.register_repo(&served_repository).await;

    let pkg1_url_with_pkg2_merkle =
        format!("fuchsia-pkg://test/pinned-merkle-foo?hash={}", pkg2.meta_far_merkle_root());

    let (package_dir, _resolved_context) =
        env.resolve_package(&pkg1_url_with_pkg2_merkle).await.unwrap();
    pkg2.verify_contents(&package_dir).await.unwrap();

    env.stop().await;
}

#[fuchsia::test]
async fn variant_resolution() {
    let env = TestEnvBuilder::new().build().await;
    let pkg = PackageBuilder::new("variant-foo")
        .add_resource_at("data/foo", "foo".as_bytes())
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

    let served_repository = repo.server().start().unwrap();
    env.register_repo(&served_repository).await;

    let pkg_url = &"fuchsia-pkg://test/variant-foo/0";

    let (package_dir, _resolved_context) = env.resolve_package(pkg_url).await.unwrap();
    pkg.verify_contents(&package_dir).await.unwrap();

    env.stop().await;
}

#[fuchsia::test(logging_tags = ["RESOLVE_TEST"])]
async fn error_codes() {
    let env = TestEnvBuilder::new().build().await;
    let pkg = PackageBuilder::new("error-foo")
        .add_resource_at("data/foo", "foo".as_bytes())
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

    let served_repository = repo.server().start().unwrap();
    env.register_repo(&served_repository).await;

    // Invalid URL
    assert_matches!(
        env.resolve_package("fuchsia-pkg://test/bad-url!").await,
        Err(fidl_fuchsia_pkg::ResolveError::InvalidUrl)
    );

    // Nonexistant repo
    assert_matches!(
        env.resolve_package("fuchsia-pkg://nonexistent-repo/a").await,
        Err(fidl_fuchsia_pkg::ResolveError::RepoNotFound)
    );

    // Nonexistant package
    assert_matches!(
        env.resolve_package("fuchsia-pkg://test/nonexistent").await,
        Err(fidl_fuchsia_pkg::ResolveError::PackageNotFound)
    );

    env.stop().await;
}

#[fuchsia::test]
async fn retries() {
    let env = TestEnvBuilder::new().build().await;

    let pkg = PackageBuilder::new("try-hard")
        .add_resource_at("data/foo", "bar".as_bytes())
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
    let served_repository = repo
        .server()
        .response_overrider(responder::ForPathPrefix::new(
            "/blobs",
            responder::OncePerPath::new(responder::StaticResponseCode::server_error()),
        ))
        .start()
        .unwrap();
    env.register_repo(&served_repository).await;
    let (package_dir, _resolved_context) =
        env.resolve_package("fuchsia-pkg://test/try-hard").await.unwrap();
    pkg.verify_contents(&package_dir).await.unwrap();

    let hierarchy = env.pkg_resolver_inspect_hierarchy().await;
    let repo_blob_url = format!("{}/blobs", served_repository.local_url());
    let repo_blob_url = &repo_blob_url;
    assert_data_tree!(
        hierarchy,
        root: contains {
            repository_manager: contains {
                stats: contains {
                    mirrors: {
                        var repo_blob_url: {
                            network_blips: 2u64,
                            network_rate_limits: 0u64,
                        },
                    },
                },
            },
        }
    );

    env.stop().await;
}

#[fuchsia::test]
async fn handles_429_responses() {
    let env = TestEnvBuilder::new().build().await;

    let pkg1 = PackageBuilder::new("rate-limit-far")
        .add_resource_at("data/foo", "foo".as_bytes())
        .build()
        .await
        .unwrap();
    let pkg2 = PackageBuilder::new("rate-limit-content")
        .add_resource_at("data/bar", "bar".as_bytes())
        .build()
        .await
        .unwrap();
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg1)
            .add_package(&pkg2)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo
        .server()
        .response_overrider(responder::ForPath::new(
            format!("/blobs/{}", pkg1.meta_far_merkle_root()),
            responder::ForRequestCount::new(2, responder::StaticResponseCode::too_many_requests()),
        ))
        .response_overrider(responder::ForPath::new(
            format!("/blobs/{}", pkg2.meta_contents().unwrap().contents()["data/bar"]),
            responder::ForRequestCount::new(2, responder::StaticResponseCode::too_many_requests()),
        ))
        .start()
        .unwrap();
    env.register_repo(&served_repository).await;

    // Simultaneously resolve both packages to minimize the time spent waiting for timeouts in
    // these tests.
    let proxy1 = env.connect_to_resolver();
    let proxy2 = env.connect_to_resolver();
    let pkg1_fut = resolve_package(&proxy1, "fuchsia-pkg://test/rate-limit-far");
    let pkg2_fut = resolve_package(&proxy2, "fuchsia-pkg://test/rate-limit-content");

    // The packages should resolve successfully.
    let (pkg1_res, pkg2_res) = join!(pkg1_fut, pkg2_fut);
    let (pkg1_dir, _resolved_context) = pkg1_res.unwrap();
    let (pkg2_dir, _resolved_context) = pkg2_res.unwrap();
    pkg1.verify_contents(&pkg1_dir).await.unwrap();
    pkg2.verify_contents(&pkg2_dir).await.unwrap();

    // And the inspect data for the package resolver should indicate that it handled 429 responses.
    let hierarchy = env.pkg_resolver_inspect_hierarchy().await;

    let repo_blob_url = format!("{}/blobs", served_repository.local_url());
    let repo_blob_url = &repo_blob_url;
    assert_data_tree!(
        hierarchy,
        root: contains {
            repository_manager: contains {
                stats: contains {
                    mirrors: {
                        var repo_blob_url: {
                            network_blips: 0u64,
                            network_rate_limits: 4u64,
                        },
                    },
                },
            },
        }
    );

    env.stop().await;
}

#[fuchsia::test]
async fn use_cached_package() {
    let env = TestEnvBuilder::new().build().await;

    let pkg = PackageBuilder::new("resolve-twice")
        .add_resource_at("data/foo", "bar".as_bytes())
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
    let fail_requests = responder::AtomicToggle::new(true);
    let served_repository = repo
        .server()
        .response_overrider(responder::Toggleable::new(
            &fail_requests,
            responder::StaticResponseCode::server_error(),
        ))
        .start()
        .unwrap();

    // the package can't be resolved before the repository is configured.
    assert_matches!(
        env.resolve_package("fuchsia-pkg://test/resolve-twice").await,
        Err(fidl_fuchsia_pkg::ResolveError::RepoNotFound)
    );

    env.register_repo(&served_repository).await;

    // the package can't be resolved before the repository can be updated without error.
    assert_matches!(
        env.resolve_package("fuchsia-pkg://test/resolve-twice").await,
        Err(fidl_fuchsia_pkg::ResolveError::UnavailableRepoMetadata)
    );

    // package resolves as expected.
    fail_requests.unset();
    let (package_dir, _resolved_context) =
        env.resolve_package("fuchsia-pkg://test/resolve-twice").await.unwrap();
    pkg.verify_contents(&package_dir).await.unwrap();

    // if no mirrors are accessible, the cached package is returned.
    fail_requests.set();
    let (package_dir, _resolved_context) =
        env.resolve_package("fuchsia-pkg://test/resolve-twice").await.unwrap();
    pkg.verify_contents(&package_dir).await.unwrap();

    env.stop().await;
    served_repository.stop().await;
}

#[fuchsia::test]
async fn meta_far_already_in_blobfs() {
    verify_resolve_with_altered_env(
        make_pkg_with_extra_blobs("meta_far_already_in_blobfs", 3).await,
        |env, pkg| {
            env.add_file_with_hash_to_blobfs(pkg.meta_far().unwrap(), pkg.meta_far_merkle_root())
        },
    )
    .await
}

#[fuchsia::test]
async fn all_blobs_already_in_blobfs() {
    let s = "all_blobs_already_in_blobfs";
    verify_resolve_with_altered_env(make_pkg_with_extra_blobs(s, 3).await, |env, pkg| {
        env.add_file_with_hash_to_blobfs(pkg.meta_far().unwrap(), pkg.meta_far_merkle_root());
        env.add_slice_to_blobfs(&test_package_bin(s)[..]);
        for i in 0..3 {
            env.add_slice_to_blobfs(extra_blob_contents(s, i).as_slice());
        }
    })
    .await
}

#[fuchsia::test]
async fn meta_far_and_one_content_blob_already_in_blobfs() {
    let s = "meta_far_and_one_content_blob_in_blobfs";
    verify_resolve_with_altered_env(make_pkg_with_extra_blobs(s, 3).await, |env, pkg| {
        env.add_file_with_hash_to_blobfs(pkg.meta_far().unwrap(), pkg.meta_far_merkle_root());
        env.add_slice_to_blobfs(&test_package_bin(s)[..]);
    })
    .await
}

#[fuchsia::test]
async fn test_concurrent_blob_writes() {
    // Create our test packages and find out the merkle of the duplicate blob
    let duplicate_blob_path = "blob/duplicate";
    let duplicate_blob_contents = &b"I am the duplicate"[..];
    let unique_blob_path = "blob/unique";
    let pkg1 = PackageBuilder::new("package1")
        .add_resource_at(duplicate_blob_path, duplicate_blob_contents)
        .build()
        .await
        .unwrap();
    let pkg2 = PackageBuilder::new("package2")
        .add_resource_at(duplicate_blob_path, duplicate_blob_contents)
        .add_resource_at(unique_blob_path, &b"I am unique"[..])
        .build()
        .await
        .unwrap();
    let duplicate_blob_merkle = pkg1.meta_contents().expect("extracted contents").contents()
        [duplicate_blob_path]
        .to_string();
    let unique_blob_merkle =
        pkg2.meta_contents().expect("extracted contents").contents()[unique_blob_path].to_string();

    // Create the responder and the channel to communicate with it
    let (blocking_responder, unblocking_closure_receiver) = responder::BlockResponseBodyOnce::new();
    let blocking_responder =
        responder::ForPath::new(format!("/blobs/{}", duplicate_blob_merkle), blocking_responder);

    // Construct the repo
    let env = TestEnvBuilder::new().build().await;
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg1)
            .add_package(&pkg2)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo.server().response_overrider(blocking_responder).start().unwrap();
    env.register_repo(&served_repository).await;

    // Construct the resolver proxies (clients)
    let resolver_proxy_1 = env.connect_to_resolver();
    let resolver_proxy_2 = env.connect_to_resolver();

    // Create a GET request to the hyper server for the duplicate blob
    let package1_resolution_fut =
        resolve_package(&resolver_proxy_1, &"fuchsia-pkg://test/package1");

    // Wait for GET request to be received by hyper server
    let send_shared_blob_body =
        unblocking_closure_receiver.await.expect("received unblocking future from hyper server");

    // Wait for duplicate blob to be truncated -- we know it is truncated when we get a
    // permission denied error when trying to update the blob in blobfs.
    let blobfs_dir = env.blobfs.root_dir().expect("blobfs has root dir");
    while blobfs_dir.update_file(Path::new(&duplicate_blob_merkle), 0).is_ok() {
        fasync::Timer::new(Duration::from_millis(10)).await;
    }

    // At this point, we are confident that the duplicate blob is truncated. So, if we enqueue
    // another package resolve for a package that contains the duplicate blob the package resolver
    // should block resolving the package on that blob fetch finishing.
    let package2_resolution_fut =
        resolve_package(&resolver_proxy_2, &"fuchsia-pkg://test/package2");

    // Wait for the unique blob to exist in blobfs.
    while blobfs_dir.update_file(Path::new(&unique_blob_merkle), 0).is_ok() {
        fasync::Timer::new(Duration::from_millis(10)).await;
    }

    // At this point, both package resolves should be blocked on the shared blob download. Unblock
    // the server and verify both packages resolve to valid directories.
    send_shared_blob_body();
    let ((), ()) = futures::join!(
        async move {
            let (package1_dir, _resolved_context1) = package1_resolution_fut.await.unwrap();
            pkg1.verify_contents(&package1_dir).await.unwrap();
        },
        async move {
            let (package2_dir, _resolved_context2) = package2_resolution_fut.await.unwrap();
            pkg2.verify_contents(&package2_dir).await.unwrap();
        },
    );

    env.stop().await;
}

#[fuchsia::test]
#[ignore] // TODO(65855): Fix to support lower concurrency limit.
async fn dedup_concurrent_content_blob_fetches() {
    let env = TestEnvBuilder::new().build().await;

    // Make a few test packages with no more than 6 blobs.  There is no guarantee what order the
    // package resolver will fetch blobs in other than it will fetch one of the meta FARs first and
    // it will fetch a meta FAR before fetching any unique content blobs for that package.
    //
    // Note that this test depends on the fact that the global queue has a concurrency limit of 5.
    // A concurrency limit less than 4 would cause this test to hang as it needs to be able to wait
    // for a unique blob request to come in for each package, and ordering of blob requests is not
    // guaranteed.
    let pkg1 = PackageBuilder::new("package1")
        .add_resource_at("data/unique1", "package1unique1".as_bytes())
        .add_resource_at("data/shared1", "shared1".as_bytes())
        .add_resource_at("data/shared2", "shared2".as_bytes())
        .build()
        .await
        .unwrap();
    let pkg2 = PackageBuilder::new("package2")
        .add_resource_at("data/unique1", "package2unique1".as_bytes())
        .add_resource_at("data/shared1", "shared1".as_bytes())
        .add_resource_at("data/shared2", "shared2".as_bytes())
        .build()
        .await
        .unwrap();

    // Create the request responder to block all content blobs until we are ready to unblock them.
    let content_blob_paths = {
        let pkg1_meta_contents = pkg1.meta_contents().expect("meta/contents to parse");
        let pkg2_meta_contents = pkg2.meta_contents().expect("meta/contents to parse");

        pkg1_meta_contents
            .contents()
            .values()
            .chain(pkg2_meta_contents.contents().values())
            .map(|blob| format!("/blobs/{}", blob).into())
            .collect::<HashSet<_>>()
    };
    let (request_responder, mut incoming_requests) = responder::BlockResponseHeaders::new();
    let request_responder =
        responder::ForPaths::new(content_blob_paths.iter().cloned().collect(), request_responder);

    // Serve and register the repo with our request responder that blocks headers for content blobs.
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg1)
            .add_package(&pkg2)
            .build()
            .await
            .expect("repo to build"),
    );
    let served_repository =
        repo.server().response_overrider(request_responder).start().expect("repo to serve");

    env.register_repo(&served_repository).await;

    // Start resolving both packages using distinct proxies, which should block waiting for the
    // meta FAR responses.
    let pkg1_fut = {
        let proxy = env.connect_to_resolver();
        resolve_package(&proxy, "fuchsia-pkg://test/package1")
    };
    let pkg2_fut = {
        let proxy = env.connect_to_resolver();
        resolve_package(&proxy, "fuchsia-pkg://test/package2")
    };

    // Wait for all content blob requests to come in to make sure they are maximally de-duped.
    let mut expected_requests = content_blob_paths.clone();
    let mut blocked_requests = vec![];
    while !expected_requests.is_empty() {
        let req = incoming_requests.next().await.expect("more incoming requests");
        // Panic if the blob request wasn't expected or has already happened and was not de-duped
        // as expected.
        assert!(expected_requests.remove(req.path()));
        blocked_requests.push(req);
    }

    // Unblock all content blobs, and verify both packages resolve without error.
    for req in blocked_requests {
        req.unblock();
    }

    let (pkg1_dir, _resolved_context) = pkg1_fut.await.expect("package 1 to resolve");
    let (pkg2_dir, _resolved_context) = pkg2_fut.await.expect("package 2 to resolve");

    pkg1.verify_contents(&pkg1_dir).await.unwrap();
    pkg2.verify_contents(&pkg2_dir).await.unwrap();

    env.stop().await;
}

async fn test_https_endpoint(pkg_name: &str, bind_addr: impl Into<IpAddr>) {
    let env = TestEnvBuilder::new().build().await;

    let pkg = PackageBuilder::new(pkg_name)
        .add_resource_at(format!("bin/{}", pkg_name), &test_package_bin(pkg_name)[..])
        .add_resource_at(format!("meta/{}.cml", pkg_name), &test_package_cml(pkg_name)[..])
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
    let served_repository = repo
        .server()
        .use_https_domain(Domain::TestFuchsiaCom)
        .bind_to_addr(bind_addr)
        .start()
        .unwrap();

    env.register_repo(&served_repository).await;

    let (package, _resolved_context) = env
        .resolve_package(format!("fuchsia-pkg://test/{}", pkg_name).as_str())
        .await
        .expect("package to resolve without error");
    pkg.verify_contents(&package).await.unwrap();

    env.stop().await;
}

#[fuchsia::test]
async fn https_endpoint_ipv6_only() {
    test_https_endpoint("https_endpoint_ipv6", Ipv6Addr::LOCALHOST).await
}

#[fuchsia::test]
async fn https_endpoint_ipv4_only() {
    test_https_endpoint("https_endpoint_ipv4", Ipv4Addr::LOCALHOST).await
}

#[fuchsia::test]
async fn verify_concurrent_resolve() {
    let env = TestEnvBuilder::new().build().await;

    let pkg1 = make_pkg_with_extra_blobs("first_concurrent_resolve_pkg", 1).await;
    let pkg2 = make_pkg_with_extra_blobs("second_concurrent_resolve_pkg", 1).await;

    // Make a repo with both packages.
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg1)
            .add_package(&pkg2)
            .build()
            .await
            .unwrap(),
    );

    let pkg1_url = "fuchsia-pkg://test/first_concurrent_resolve_pkg";
    let pkg2_url = "fuchsia-pkg://test/second_concurrent_resolve_pkg";

    let path = format!("/blobs/{}", pkg1.content_blob_files().next().unwrap().merkle);
    let (blocker, mut chan) = responder::BlockResponseHeaders::new();
    let responder = responder::ForPath::new(path, blocker);

    let served_repository = repo.server().response_overrider(responder).start().unwrap();
    env.register_repo(&served_repository).await;

    // First resolve should block per the responder we use.
    let pkg1_resolve_fut = env.resolve_package(&pkg1_url);
    // Get the BlockedResponse to make sure we're blocking the above resolve
    // before we try to resolve the second package.
    let blocked_response = chan.next().await.unwrap();
    // Now await on resolving another package.

    assert_matches!(env.resolve_package(&pkg2_url).await, Ok(_));
    // Finally unblock the first resolve to safe
    blocked_response.unblock();
    assert_matches!(pkg1_resolve_fut.await, Ok(_));

    // Tear down the test environment now so it doesn't live until another
    // test environment is created which could cause an OOM.
    env.stop().await;
}

// Merkle-pinned resolves verify that there is a package of that name in TUF, but then
// download the meta.far directly from the blob url. This test verifies that the resolver
// does not use the size of the meta.far found in TUF, since the pinned meta.far could
// differ in size.
#[fuchsia::test]
async fn merkle_pinned_meta_far_size_different_than_tuf_metadata() {
    let env = TestEnvBuilder::new().build().await;
    // Content chunks in FARs are 4k aligned, so a meta.far for an empty package will be 12k
    // because of meta/package, meta/fuchsia.abi/abi-revision, and meta/contents is empty..
    let pkg_12k_tuf = PackageBuilder::new("merkle-pin-size").build().await.unwrap();
    assert_eq!(pkg_12k_tuf.meta_far().unwrap().metadata().unwrap().len(), 3 * 4096);
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg_12k_tuf)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = Arc::clone(&repo).server().start().unwrap();

    // Put the larger, merkle-pinned package in /blobs.
    let pkg_16k_pinned = PackageBuilder::new("merkle-pin-size")
        .add_resource_at("meta/zero", &[0u8][..])
        .build()
        .await
        .unwrap();
    assert_eq!(pkg_16k_pinned.meta_far().unwrap().metadata().unwrap().len(), 4 * 4096);
    std::fs::copy(
        pkg_16k_pinned.artifacts().join("meta.far"),
        repo.path().join("blobs").join(pkg_16k_pinned.meta_far_merkle_root().to_string()),
    )
    .unwrap();

    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);
    let () = env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    let pinned_url = format!(
        "fuchsia-pkg://test/merkle-pin-size?hash={}",
        pkg_16k_pinned.meta_far_merkle_root()
    );
    let (resolved_pkg, _resolved_context) =
        env.resolve_package(&pinned_url).await.expect("package to resolve without error");
    pkg_16k_pinned.verify_contents(&resolved_pkg).await.unwrap();

    env.stop().await;
}

#[fuchsia::test]
async fn resolve_local_mirror() {
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
    let mut startup_blobs = env.blobfs.list_blobs().unwrap();
    let repo_config = repo.make_repo_config("fuchsia-pkg://test".parse().unwrap(), None, true);
    env.proxies.repo_manager.add(repo_config.into()).await.unwrap().unwrap();

    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();

    pkg.verify_contents(&package_dir).await.unwrap();
    let mut repo_blobs = repo.list_blobs().unwrap();
    repo_blobs.append(&mut startup_blobs);
    assert_eq!(env.blobfs.list_blobs().unwrap(), repo_blobs);

    env.stop().await;
}
