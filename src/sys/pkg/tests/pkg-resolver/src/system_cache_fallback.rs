// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    assert_matches::assert_matches,
    blobfs_ramdisk::Ramdisk,
    fidl_fuchsia_pkg_rewrite_ext::Rule,
    fuchsia_hash::Hash,
    fuchsia_inspect::assert_data_tree,
    fuchsia_pkg_testing::{
        serve::responder, Package, PackageBuilder, RepositoryBuilder, SystemImageBuilder,
    },
    fuchsia_zircon::Status,
    lib::{TestEnvBuilder, EMPTY_REPO_PATH},
    rand::prelude::*,
    std::io::Read,
    std::sync::Arc,
};

async fn test_package(name: &str, contents: &str) -> Package {
    PackageBuilder::new(name)
        .add_resource_at("p/t/o", format!("contents: {}\n", contents).as_bytes())
        .build()
        .await
        .expect("build package")
}

// The package is in the cache. Networking is totally down. Fallback succeeds.
#[fuchsia::test]
async fn test_cache_fallback_succeeds_no_network() {
    let pkg_name = "test_cache_fallback_succeeds_no_network";
    let cache_pkg = test_package(pkg_name, "cache").await;
    // Put a copy of the package with altered contents in the repo to make sure
    // we're getting the one from the cache.
    let repo_pkg = test_package(pkg_name, "repo").await;
    let system_image_package =
        SystemImageBuilder::new().cache_packages(&[&cache_pkg]).build().await;
    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&cache_pkg])
        .build()
        .await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&system_image_package)
            .add_package(&repo_pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo
        .server()
        .response_overrider(responder::StaticResponseCode::server_error())
        .start()
        .unwrap();
    // System cache fallback is only triggered for fuchsia.com repos.
    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;
    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", pkg_name);
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    // Make sure we got the cache version, not the repo version.
    cache_pkg.verify_contents(&package_dir).await.unwrap();
    assert!(repo_pkg.verify_contents(&package_dir).await.is_err());

    // Check that get_hash fallback behavior matches resolve.
    let hash = env.get_hash(pkg_url).await;
    assert_eq!(hash.unwrap(), cache_pkg.meta_far_merkle_root().clone().into());

    env.stop().await;
}

// The package is in the cache_packages manifest and has the same hash as the pinned URL. Networking
// is totally down. Fallback succeeds.
#[fuchsia::test]
async fn test_cache_fallback_succeeds_if_url_merkle_matches() {
    let pkg_name = "test_cache_fallback_succeeds_if_url_merkle_matches";
    let pkg = test_package(pkg_name, "some-contents").await;
    let system_image_package = SystemImageBuilder::new().cache_packages(&[&pkg]).build().await;
    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&pkg])
        .build()
        .await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&system_image_package)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo
        .server()
        .response_overrider(responder::StaticResponseCode::server_error())
        .start()
        .unwrap();
    // System cache fallback is only triggered for fuchsia.com repos.
    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;

    let pkg_url =
        format!("fuchsia-pkg://fuchsia.com/{}?hash={}", pkg_name, pkg.meta_far_merkle_root());
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    pkg.verify_contents(&package_dir).await.unwrap();

    let hash = env.get_hash(pkg_url).await;
    assert_eq!(hash.unwrap(), pkg.meta_far_merkle_root().clone().into());

    env.stop().await;
}

fn make_different_hash(h: &Hash) -> Hash {
    let mut bytes: [u8; fuchsia_hash::HASH_SIZE] = h.to_owned().into();
    bytes[0] = !bytes[0];
    bytes.into()
}

// The package is in the cache_packages manifest and has a different hash than the pinned URL.
// Networking is totally down. Fallback fails.
#[fuchsia::test]
async fn test_cache_fallback_fails_if_url_merkle_differs() {
    let pkg_name = "test_cache_fallback_fails_if_url_merkle_differs";
    let pkg = test_package(pkg_name, "some-contents").await;
    let system_image_package = SystemImageBuilder::new().cache_packages(&[&pkg]).build().await;
    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&pkg])
        .build()
        .await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&system_image_package)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo
        .server()
        .response_overrider(responder::StaticResponseCode::server_error())
        .start()
        .unwrap();
    // System cache fallback is only triggered for fuchsia.com repos.
    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;
    let wrong_hash = make_different_hash(pkg.meta_far_merkle_root());
    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}?hash={}", pkg_name, wrong_hash);
    assert_matches!(
        env.resolve_package(&pkg_url).await,
        Err(fidl_fuchsia_pkg::ResolveError::UnavailableRepoMetadata)
    );

    // Check that get_hash fallback behavior matches resolve.
    assert_matches!(env.get_hash(pkg_url).await, Err(Status::UNAVAILABLE));

    env.stop().await;
}

// The package is in the cache. Fallback is triggered because requests for targets.json fail.
#[fuchsia::test]
async fn test_cache_fallback_succeeds_no_targets() {
    let pkg_name = "test_cache_fallback_succeeds_no_targets";
    let cache_pkg = test_package(pkg_name, "cache").await;
    // Put a copy of the package with altered contents in the repo to make sure
    // we're getting the one from the cache.
    let repo_pkg = test_package(pkg_name, "repo").await;
    let system_image_package =
        SystemImageBuilder::new().cache_packages(&[&cache_pkg]).build().await;
    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&cache_pkg])
        .build()
        .await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&system_image_package)
            .add_package(&repo_pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo
        .server()
        // TODO(ampearce): add a suffix responder so the version
        // won't matter. This work, it just seems brittle.
        .response_overrider(responder::ForPath::new(
            "/2.targets.json",
            responder::StaticResponseCode::server_error(),
        ))
        .start()
        .unwrap();
    // System cache fallback is only triggered for fuchsia.com repos.
    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;
    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", pkg_name);
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    // Make sure we got the cache version, not the repo version.
    cache_pkg.verify_contents(&package_dir).await.unwrap();
    assert!(repo_pkg.verify_contents(&package_dir).await.is_err());

    // Check that get_hash fallback behavior matches resolve.
    let hash = env.get_hash(pkg_url).await;
    assert_eq!(hash.unwrap(), cache_pkg.meta_far_merkle_root().clone().into());

    env.stop().await;
}

// The package is in the cache. Rewrite rule applies. Networking is totally down. Fallback succeeds.
#[fuchsia::test]
async fn test_cache_fallback_succeeds_rewrite_rule() {
    let pkg_name = "test_cache_fallback_succeeds_rewrite_rule";
    let cache_pkg = test_package(pkg_name, "cache").await;
    // Put a copy of the package with altered contents in the repo to make sure
    // we're getting the one from the cache.
    let repo_pkg = test_package(pkg_name, "repo").await;
    let system_image_package =
        SystemImageBuilder::new().cache_packages(&[&cache_pkg]).build().await;
    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&cache_pkg])
        .build()
        .await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&system_image_package)
            .add_package(&repo_pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo
        .server()
        .response_overrider(responder::StaticResponseCode::server_error())
        .start()
        .unwrap();
    // System cache fallback is only triggered for fuchsia.com repos, but
    // a fuchsia.com transformed by a rewrite rule should still work.
    env.register_repo_at_url(&served_repository, "fuchsia-pkg://test").await;
    let (edit_transaction, edit_transaction_server) = fidl::endpoints::create_proxy().unwrap();
    env.proxies.rewrite_engine.start_edit_transaction(edit_transaction_server).unwrap();
    let rule = Rule::new("fuchsia.com", "test", "/", "/").unwrap();
    let () = edit_transaction.add(&mut rule.clone().into()).await.unwrap().unwrap();
    let () = edit_transaction.commit().await.unwrap().unwrap();

    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", pkg_name);
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    // Make sure we got the cache version, not the repo version.
    cache_pkg.verify_contents(&package_dir).await.unwrap();
    assert!(repo_pkg.verify_contents(&package_dir).await.is_err());

    // Check that get_hash fallback behavior matches resolve.
    let hash = env.get_hash(pkg_url).await;
    assert_eq!(hash.unwrap(), cache_pkg.meta_far_merkle_root().clone().into());

    env.stop().await;
}

// If blobfs is out of space, pkg-resolver currently should not fall back to cache_packages if it's
// asked to resolve a new version of a package in that list, if that package has a very large meta/
// directory
#[fuchsia::test]
async fn test_blobfs_out_of_space_does_not_fall_back_to_cache_packages_with_large_meta_far() {
    let pkg_name =
        "test_blobfs_out_of_space_does_not_fall_back_to_cache_packages_with_large_meta_far";

    // A very small package to cache.
    let cache_pkg = test_package(pkg_name, "cache").await;

    let system_image_package =
        SystemImageBuilder::new().cache_packages(&[&cache_pkg]).build().await;

    // Create a 2MB blobfs (4096 blocks * 512 bytes / block), which is about the minimum size blobfs
    // will fit in and still be able to write our small package.
    // See https://fuchsia.dev/fuchsia-src/concepts/filesystems/blobfs for information on the
    // blobfs format and metadata overhead.
    let very_small_blobfs = Ramdisk::builder()
        .block_count(4096)
        .into_blobfs_builder()
        .expect("made blobfs builder")
        .start()
        .expect("started blobfs");
    system_image_package
        .write_to_blobfs_dir(&very_small_blobfs.root_dir().expect("wrote system image to blobfs"));
    cache_pkg
        .write_to_blobfs_dir(&very_small_blobfs.root_dir().expect("wrote cache package to blobfs"));

    // A very large version of the same package, to put in the repo.
    // Critically, this package contains an incompressible 4MB asset in the meta.far,
    // which is larger than our blobfs, and attempting to resolve this package will result in
    // blobfs returning out of space.
    const LARGE_ASSET_FILE_SIZE: u64 = 4 * 1024 * 1024;
    let mut rng = StdRng::from_seed([0u8; 32]);
    let rng = &mut rng as &mut dyn RngCore;
    let repo_pkg = PackageBuilder::new(pkg_name)
        .add_resource_at("meta/asset", rng.take(LARGE_ASSET_FILE_SIZE))
        .build()
        .await
        .expect("build large package");

    // The size of the meta far should be the size of our asset, plus three 4k-aligned files:
    //  - meta/package
    //  - meta/fuchsia.abi/abi-revision
    //  - meta/contents
    // Content chunks in FARs are 4KiB-aligned, so the most empty FAR we can get is 12KiB:
    // meta/package at one alignment boundary, meta/fuchsia.abi/abi-revision at the next alignment
    // boundary, and meta/contents is empty.
    // This FAR should be 12KiB + the size of our asset file.
    assert_eq!(
        repo_pkg.meta_far().unwrap().metadata().unwrap().len(),
        LARGE_ASSET_FILE_SIZE + 4096 + 4096 + 4096
    );

    let env = TestEnvBuilder::new()
        .blobfs_and_system_image_hash(
            very_small_blobfs,
            Some(*system_image_package.meta_far_merkle_root()),
        )
        .build()
        .await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&repo_pkg)
            .build()
            .await
            .unwrap(),
    );

    let served_repository = repo.server().start().unwrap();
    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;

    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", pkg_name);
    assert_matches!(
        env.resolve_package(&pkg_url).await,
        Err(fidl_fuchsia_pkg::ResolveError::NoSpace)
    );

    env.stop().await;
}

// If blobfs is out of space, pkg-resolver currently should not fall back to cache_packages if it's
// asked to resolve a new version of a package in that list.
#[fuchsia::test]
async fn test_blobfs_out_of_space_does_not_fall_back_to_cache_packages() {
    let pkg_name = "test_blobfs_out_of_space_does_not_fall_back_to_cache_packages";

    // A very small package to cache.
    let cache_pkg = test_package(pkg_name, "cache").await;

    let system_image_package =
        SystemImageBuilder::new().cache_packages(&[&cache_pkg]).build().await;

    let very_small_blobfs = Ramdisk::builder()
        .block_count(4096)
        .into_blobfs_builder()
        .expect("made blobfs builder")
        .start()
        .expect("started blobfs");
    system_image_package
        .write_to_blobfs_dir(&very_small_blobfs.root_dir().expect("wrote system image to blobfs"));
    cache_pkg
        .write_to_blobfs_dir(&very_small_blobfs.root_dir().expect("wrote cache package to blobfs"));

    // A very large version of the same package, to put in the repo.
    let mut rng = StdRng::from_seed([0u8; 32]);
    let rng = &mut rng as &mut dyn RngCore;
    let repo_pkg = PackageBuilder::new(pkg_name)
        .add_resource_at("p/t/o", rng.take(4 * 1024 * 1024))
        .build()
        .await
        .expect("build large package");
    let env = TestEnvBuilder::new()
        .blobfs_and_system_image_hash(
            very_small_blobfs,
            Some(*system_image_package.meta_far_merkle_root()),
        )
        .build()
        .await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&repo_pkg)
            .build()
            .await
            .unwrap(),
    );

    let served_repository = repo.server().start().unwrap();
    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;

    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", pkg_name);
    assert_matches!(
        env.resolve_package(&pkg_url).await,
        Err(fidl_fuchsia_pkg::ResolveError::NoSpace)
    );

    env.stop().await;
}

// If blobfs is out of space, we shouldn't fall back to a previously-resolved version of an
// ephemeral package.
#[fuchsia::test]
async fn test_blobfs_out_of_space_does_not_fall_back_to_previous_ephemeral_package() {
    let pkg_name = "test_blobfs_out_of_space_fallback_to_previous_version";
    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", pkg_name);

    let very_small_blobfs = Ramdisk::builder()
        .block_count(4096)
        .into_blobfs_builder()
        .expect("made blobfs builder")
        .start()
        .expect("started blobfs");
    let system_image_package = SystemImageBuilder::new();
    let system_image_package = system_image_package.build().await;
    system_image_package.write_to_blobfs_dir(&very_small_blobfs.root_dir().unwrap());

    let env = TestEnvBuilder::new()
        .blobfs_and_system_image_hash(
            very_small_blobfs,
            Some(*system_image_package.meta_far_merkle_root()),
        )
        .build()
        .await;

    let small_pkg = test_package(pkg_name, "cache").await;
    let repo_with_small_package = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&small_pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo_with_small_package.server().start().unwrap();
    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;

    // Resolving and caching a small package should work fine.
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    small_pkg.verify_contents(&package_dir).await.unwrap();

    // Stop the running repository, fire up a new one with a very large package in it,
    // which won't fit in blobfs.
    let () = served_repository.stop().await;

    // A very large version of the same package, to put in the repo.
    let mut rng = StdRng::from_seed([0u8; 32]);
    let rng = &mut rng as &mut dyn RngCore;
    let large_pkg = PackageBuilder::new(pkg_name)
        .add_resource_at("p/t/o", rng.take(4 * 1024 * 1024))
        .build()
        .await
        .expect("build large package");

    let repo_with_large_package = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&large_pkg)
            .build()
            .await
            .unwrap(),
    );

    let served_repository = repo_with_large_package.server().start().unwrap();
    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;

    // pkg-resolver should refuse to fall back to a previous version of the package, and fail
    // with NO_SPACE
    assert_matches!(
        env.resolve_package(&pkg_url).await,
        Err(fidl_fuchsia_pkg::ResolveError::NoSpace)
    );

    env.stop().await;
}

// The package is in the cache but not known to the repository. Don't fall back.
#[fuchsia::test]
#[ignore] // TODO(fxbug.dev/50764): reenable this after we've normalized SDK client behavior wrt cache fallback.
async fn test_resolve_fails_not_in_repo() {
    let pkg_name = "test_resolve_fails_not_in_repo";
    let pkg = test_package(pkg_name, "stuff").await;
    let system_image_package = SystemImageBuilder::new().cache_packages(&[&pkg]).build().await;
    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&pkg])
        .build()
        .await;

    // Repo doesn't need any fault injection, it just doesn't know about the package.
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&system_image_package)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo.server().start().unwrap();
    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;

    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", pkg.name());
    let res = env.resolve_package(&pkg_url).await;
    assert_matches!(res, Err(fidl_fuchsia_pkg::ResolveError::PackageNotFound));

    // Check that get_hash fallback behavior matches resolve.
    let hash = env.get_hash(pkg_url).await;
    assert_matches!(hash, Err(Status::NOT_FOUND));

    env.stop().await;
}

// The package is in the cache but not known to the repository. Fall back to the on-disk package.
// TODO(fxbug.dev/50764): This is the wrong behavior. Delete this after we've normalized SDK client behavior
// wrt cache fallback.
#[fuchsia::test]
async fn test_resolve_falls_back_not_in_repo() {
    let pkg_name = "test_resolve_falls_back_not_in_repo";

    let cache_pkg = test_package(pkg_name, "cache").await;
    let system_image_package =
        SystemImageBuilder::new().cache_packages(&[&cache_pkg]).build().await;
    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&cache_pkg])
        .build()
        .await;

    // Repo doesn't need any fault injection, it just doesn't know about the package.
    let repo =
        Arc::new(RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).build().await.unwrap());
    let served_repository = repo.server().start().unwrap();
    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;

    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", cache_pkg.name());
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();

    // Make sure we got the cache version
    cache_pkg.verify_contents(&package_dir).await.unwrap();

    // Check that get_hash fallback behavior matches resolve for the on-disk package
    let hash = env.get_hash(pkg_url).await;
    assert_eq!(hash.unwrap(), cache_pkg.meta_far_merkle_root().clone().into());

    // Make sure that the inspect metric for this feature notes that we fell back
    // to an on-disk package.
    // (We've fallen back twice, once for the actual package resolve and one for the get_hash call)
    let hierarchy = env.pkg_resolver_inspect_hierarchy().await;
    assert_data_tree!(
        hierarchy,
        root: contains {
            resolver_service: contains {
                cache_fallbacks_due_to_not_found: 2u64,
            }
        }
    );

    // Now check a URL which is definitely not found in the repo, but is also not on disk.
    // We should return NOT_FOUND, and the inspect metric should not increment.
    let nonexistent_pkg_url = "fuchsia-pkg://fuchsia.com/definitely_not_found".to_string();
    let res = env.resolve_package(&nonexistent_pkg_url).await;
    assert_matches!(res, Err(fidl_fuchsia_pkg::ResolveError::PackageNotFound));

    // Check that get_hash fallback behavior matches resolve for the nonexistent package
    let hash = env.get_hash(nonexistent_pkg_url).await;
    assert_matches!(hash, Err(Status::NOT_FOUND));

    // We didn't fall back to an on-disk package, so the inspect counter should not have changed.
    let hierarchy = env.pkg_resolver_inspect_hierarchy().await;
    assert_data_tree!(
        hierarchy,
        root: contains {
            resolver_service: contains {
                cache_fallbacks_due_to_not_found: 2u64,
            }
        }
    );

    env.stop().await;
}

// A package with the same name is in the cache and the repository. Prefer the repo package.
#[fuchsia::test]
async fn test_resolve_prefers_repo() {
    let pkg_name = "test_resolve_prefers_repo";
    let cache_pkg = test_package(pkg_name, "cache_package").await;
    let repo_pkg = test_package(pkg_name, "repo_package").await;
    let system_image_package =
        SystemImageBuilder::new().cache_packages(&[&cache_pkg]).build().await;
    let env = TestEnvBuilder::new()
        .system_image_and_extra_packages(&system_image_package, &[&cache_pkg])
        .build()
        .await;

    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&system_image_package)
            .add_package(&repo_pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo.server().start().unwrap();
    env.register_repo_at_url(&served_repository, "fuchsia-pkg://fuchsia.com").await;

    let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", pkg_name);
    let (package_dir, _resolved_context) = env.resolve_package(&pkg_url).await.unwrap();
    // Make sure we got the repo version, not the cache version.
    repo_pkg.verify_contents(&package_dir).await.unwrap();
    assert!(cache_pkg.verify_contents(&package_dir).await.is_err());

    // Check that get_hash fallback behavior matches resolve.
    let hash = env.get_hash(pkg_url).await;
    assert_eq!(hash.unwrap(), repo_pkg.meta_far_merkle_root().clone().into());

    env.stop().await;
}
