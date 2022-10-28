// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{get_missing_blobs, write_blob, TestEnv},
    assert_matches::assert_matches,
    blobfs_ramdisk::{BlobfsRamdisk, Ramdisk},
    fidl_fuchsia_io as fio, fidl_fuchsia_paver as fpaver,
    fidl_fuchsia_pkg::{self as fpkg, NeededBlobsMarker},
    fidl_fuchsia_pkg_ext::BlobId,
    fidl_fuchsia_space::ErrorCode,
    fuchsia_async::{self as fasync, OnSignals},
    fuchsia_pkg_testing::{Package, PackageBuilder, SystemImageBuilder},
    fuchsia_zircon::{self as zx, Status},
    futures::TryFutureExt,
    mock_paver::{hooks as mphooks, MockPaverServiceBuilder, PaverEvent},
    rand::prelude::*,
    std::{
        collections::{BTreeSet, HashMap, HashSet},
        io::Read as _,
    },
};

// TODO(fxbug.dev/76724): Deduplicate this function.
async fn do_fetch(package_cache: &fpkg::PackageCacheProxy, pkg: &Package) {
    let mut meta_blob_info =
        fpkg::BlobInfo { blob_id: BlobId::from(*pkg.meta_far_merkle_root()).into(), length: 0 };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let (dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let get_fut = package_cache
        .get(&mut meta_blob_info, needed_blobs_server_end, Some(dir_server_end))
        .map_ok(|res| res.map_err(Status::from_raw));

    let (meta_far, contents) = pkg.contents();
    let mut contents = contents
        .into_iter()
        .map(|(hash, bytes)| (BlobId::from(hash), bytes))
        .collect::<HashMap<_, Vec<u8>>>();

    let (meta_blob, meta_blob_server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
    let res = needed_blobs.open_meta_blob(meta_blob_server_end).await.unwrap().unwrap();
    assert!(res);
    let () = write_blob(&meta_far.contents, meta_blob).await.unwrap();

    let missing_blobs = get_missing_blobs(&needed_blobs).await;
    for mut blob in missing_blobs {
        let buf = contents.remove(&blob.blob_id.into()).unwrap();

        let (content_blob, content_blob_server_end) =
            fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
        assert!(needed_blobs
            .open_blob(&mut blob.blob_id, content_blob_server_end)
            .await
            .unwrap()
            .unwrap());

        let () = write_blob(&buf, content_blob).await.unwrap();
    }

    let () = get_fut.await.unwrap().unwrap();
    let () = pkg.verify_contents(&dir).await.unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn gc_error_pending_commit() {
    let (throttle_hook, throttler) = mphooks::throttle();

    let system_image_package = SystemImageBuilder::new().build().await;
    let env = TestEnv::builder()
        .blobfs_from_system_image(&system_image_package)
        .paver_service_builder(
            MockPaverServiceBuilder::new()
                .insert_hook(throttle_hook)
                .insert_hook(mphooks::config_status(|_| Ok(fpaver::ConfigurationStatus::Pending))),
        )
        .build()
        .await;

    // Allow the paver to emit enough events to unblock the CommitStatusProvider FIDL server, but
    // few enough to guarantee the commit is still pending.
    let () = throttler.emit_next_paver_events(&[
        PaverEvent::QueryCurrentConfiguration,
        PaverEvent::QueryConfigurationStatus { configuration: fpaver::Configuration::A },
    ]);
    assert_matches!(env.proxies.space_manager.gc().await, Ok(Err(ErrorCode::PendingCommit)));

    // When the commit completes, GC should unblock as well.
    let () = throttler.emit_next_paver_events(&[
        PaverEvent::SetConfigurationHealthy { configuration: fpaver::Configuration::A },
        PaverEvent::SetConfigurationUnbootable { configuration: fpaver::Configuration::B },
        PaverEvent::BootManagerFlush,
    ]);
    let event_pair =
        env.proxies.commit_status_provider.is_current_system_committed().await.unwrap();
    assert_eq!(OnSignals::new(&event_pair, zx::Signals::USER_0).await, Ok(zx::Signals::USER_0));
    assert_matches!(env.proxies.space_manager.gc().await, Ok(Ok(())));
}

/// Sets up the test environment and writes the packages out to base.
async fn setup_test_env(
    blobfs: Option<BlobfsRamdisk>,
    static_packages: &[&Package],
) -> (TestEnv, Package) {
    let blobfs = match blobfs {
        Some(fs) => fs,
        None => BlobfsRamdisk::start().unwrap(),
    };
    let system_image_package =
        SystemImageBuilder::new().static_packages(static_packages).build().await;
    system_image_package.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    for pkg in static_packages {
        pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
    }

    let env = TestEnv::builder()
        .blobfs_and_system_image_hash(blobfs, Some(*system_image_package.meta_far_merkle_root()))
        .build()
        .await;
    env.block_until_started().await;
    (env, system_image_package)
}

/// Assert that performing a GC does nothing on a blobfs that only includes the system image and
/// static packages.
#[fasync::run_singlethreaded(test)]
async fn gc_noop_system_image() {
    let static_package = PackageBuilder::new("static-package")
        .add_resource_at("resource", &[][..])
        .build()
        .await
        .unwrap();
    let (env, _) = setup_test_env(None, &[&static_package]).await;
    let original_blobs = env.blobfs.list_blobs().expect("to get an initial list of blobs");

    assert_matches!(env.proxies.space_manager.gc().await, Ok(Ok(())));
    assert_eq!(env.blobfs.list_blobs().expect("to get new blobs"), original_blobs);
}

/// Assert that any blobs protected by the dynamic index are ineligible for garbage collection.
/// Furthermore, ensure that an incomplete package does not lose blobs, and that the previous
/// packages' blobs survive until the new package is entirely written.
#[fasync::run_singlethreaded(test)]
async fn gc_dynamic_index_protected() {
    let (env, sysimg_pkg) = setup_test_env(None, &[]).await;

    let pkg = PackageBuilder::new("gc_dynamic_index_protected_pkg_cache")
        .add_resource_at("bin/x", "bin-x-version-1".as_bytes())
        .add_resource_at("data/unchanging", "unchanging-content".as_bytes())
        .build()
        .await
        .unwrap();
    do_fetch(&env.proxies.package_cache, &pkg).await;

    // Ensure that the just-fetched blobs are not reaped by a GC cycle.
    let mut test_blobs = env.blobfs.list_blobs().expect("to get an initial list of blobs");

    assert_matches!(env.proxies.space_manager.gc().await, Ok(Ok(())));
    assert_eq!(env.blobfs.list_blobs().expect("to get new blobs"), test_blobs);

    // Fetch an updated package, skipping both its content blobs to guarantee that there are
    // missing blobs. This helps us ensure that the meta.far is not lost.
    let pkgprime = PackageBuilder::new("gc_dynamic_index_protected_pkg_cache")
        .add_resource_at("bin/x", "bin-x-version-2".as_bytes())
        .add_resource_at("bin/y", "bin-y-version-1".as_bytes())
        .add_resource_at("data/unchanging", "unchanging-content".as_bytes())
        .build()
        .await
        .unwrap();

    // We can't call do_fetch here because the NeededBlobs protocol can be "canceled". This means
    // that if the channel is closed before the protocol is completed, the blobs mentioned in the
    // meta.far are no longer protected by the dynamic index.
    // That's WAI, but complicating the do_fetch interface further isn't worth it.
    //
    // Here, we persist the meta.far
    let mut meta_blob_info = fpkg::BlobInfo {
        blob_id: BlobId::from(*pkgprime.meta_far_merkle_root()).into(),
        length: 0,
    };
    let package_cache = &env.proxies.package_cache;

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let (dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let get_fut = package_cache
        .get(&mut meta_blob_info, needed_blobs_server_end, Some(dir_server_end))
        .map_ok(|res| res.map_err(Status::from_raw));

    let (meta_far, contents) = pkgprime.contents();
    let mut contents = contents
        .into_iter()
        .map(|(hash, bytes)| (BlobId::from(hash), bytes))
        .collect::<HashMap<_, Vec<u8>>>();

    let (meta_blob, meta_blob_server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
    let res = needed_blobs.open_meta_blob(meta_blob_server_end).await.unwrap().unwrap();
    assert!(res);
    let () = write_blob(&meta_far.contents, meta_blob).await.unwrap();

    // Ensure that the new meta.far is persisted despite having missing blobs, and the "old" blobs
    // are not removed.
    assert_matches!(env.proxies.space_manager.gc().await, Ok(Ok(())));
    test_blobs.insert(*pkgprime.meta_far_merkle_root());
    assert_eq!(env.blobfs.list_blobs().expect("to get new blobs"), test_blobs);

    // Fully fetch pkgprime, and ensure that blobs from the old package are not persisted past GC.
    let missing_blobs = get_missing_blobs(&needed_blobs).await;
    for mut blob in missing_blobs {
        let buf = contents.remove(&blob.blob_id.into()).unwrap();

        let (content_blob, content_blob_server_end) =
            fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
        assert!(needed_blobs
            .open_blob(&mut blob.blob_id, content_blob_server_end)
            .await
            .unwrap()
            .unwrap());

        let () = write_blob(&buf, content_blob).await.unwrap();

        // Run a GC to try to reap blobs protected by meta far.
        assert_matches!(env.proxies.space_manager.gc().await, Ok(Ok(())));
    }

    let () = get_fut.await.unwrap().unwrap();
    let () = pkgprime.verify_contents(&dir).await.unwrap();
    assert_matches!(env.proxies.space_manager.gc().await, Ok(Ok(())));

    // At this point, we expect blobfs to only contain the blobs from the system image package and
    // from pkgprime.
    let expected_blobs = sysimg_pkg
        .list_blobs()
        .unwrap()
        .union(&pkgprime.list_blobs().unwrap())
        .cloned()
        .collect::<BTreeSet<_>>();

    assert_eq!(env.blobfs.list_blobs().expect("all blobs"), expected_blobs);
}

/// Test that a blobfs with blobs not belonging to a known package will lose those blobs on GC.
#[fasync::run_singlethreaded(test)]
async fn gc_random_blobs() {
    let static_package = PackageBuilder::new("static-package")
        .add_resource_at("resource", &[][..])
        .build()
        .await
        .unwrap();
    let blobfs = BlobfsRamdisk::builder()
        .with_blob(b"blobby mcblobberson".to_vec())
        .start()
        .expect("blobfs creation to succeed with stray blob");
    let gced_blob = blobfs
        .list_blobs()
        .expect("to find initial blob")
        .into_iter()
        .next()
        .expect("to get initial blob");
    let (env, _) = setup_test_env(Some(blobfs), &[&static_package]).await;
    let mut original_blobs = env.blobfs.list_blobs().expect("to get an initial list of blobs");

    assert_matches!(env.proxies.space_manager.gc().await, Ok(Ok(())));
    assert!(original_blobs.remove(&gced_blob));
    assert_eq!(env.blobfs.list_blobs().expect("to read current blobfs state"), original_blobs);
}

/// Effectively the same as gc_dynamic_index_protected, except that the updated package also
/// existed as a static package as well.
#[fasync::run_singlethreaded(test)]
async fn gc_updated_static_package() {
    let static_package = PackageBuilder::new("gc_updated_static_package_pkg_cache")
        .add_resource_at("bin/x", "bin-x-version-0".as_bytes())
        .add_resource_at("data/unchanging", "unchanging-content".as_bytes())
        .build()
        .await
        .unwrap();

    let (env, _) = setup_test_env(None, &[&static_package]).await;
    let initial_blobs = env.blobfs.list_blobs().expect("to get initial blob list");

    let pkg = PackageBuilder::new("gc_updated_static_package_pkg_cache")
        .add_resource_at("bin/x", "bin-x-version-1".as_bytes())
        .add_resource_at("data/unchanging", "unchanging-content".as_bytes())
        .build()
        .await
        .unwrap();
    do_fetch(&env.proxies.package_cache, &pkg).await;

    // Ensure that the just-fetched blobs are not reaped by a GC cycle.
    let mut test_blobs = env.blobfs.list_blobs().expect("to get an initial list of blobs");

    assert_matches!(env.proxies.space_manager.gc().await, Ok(Ok(())));
    assert_eq!(env.blobfs.list_blobs().expect("to get new blobs"), test_blobs);

    let pkgprime = PackageBuilder::new("gc_updated_static_package_pkg_cache")
        .add_resource_at("bin/x", "bin-x-version-2".as_bytes())
        .add_resource_at("bin/y", "bin-y-version-1".as_bytes())
        .add_resource_at("data/unchanging", "unchanging-content".as_bytes())
        .build()
        .await
        .unwrap();
    // We can't call do_fetch here because the NeededBlobs protocol can be "canceled". This means
    // that if the channel is closed before the protocol is completed, the blobs mentioned in the
    // meta.far are no longer protected by the dynamic index.
    // That's WAI, but complicating the do_fetch interface further isn't worth it.
    //
    // Here, we persist the meta.far
    let mut meta_blob_info = fpkg::BlobInfo {
        blob_id: BlobId::from(*pkgprime.meta_far_merkle_root()).into(),
        length: 0,
    };
    let package_cache = &env.proxies.package_cache;

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let (dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let get_fut = package_cache
        .get(&mut meta_blob_info, needed_blobs_server_end, Some(dir_server_end))
        .map_ok(|res| res.map_err(Status::from_raw));

    let (meta_far, contents) = pkgprime.contents();
    let mut contents = contents
        .into_iter()
        .map(|(hash, bytes)| (BlobId::from(hash), bytes))
        .collect::<HashMap<_, Vec<u8>>>();

    let (meta_blob, meta_blob_server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
    let res = needed_blobs.open_meta_blob(meta_blob_server_end).await.unwrap().unwrap();
    assert!(res);
    let () = write_blob(&meta_far.contents, meta_blob).await.unwrap();

    // Ensure that the new meta.far is persisted despite having missing blobs, and the "old" blobs
    // are not removed.
    assert_matches!(env.proxies.space_manager.gc().await, Ok(Ok(())));
    test_blobs.insert(*pkgprime.meta_far_merkle_root());
    assert_eq!(env.blobfs.list_blobs().expect("to get new blobs"), test_blobs);

    // Fully fetch pkgprime, and ensure that blobs from the old package are not persisted past GC.
    let missing_blobs = get_missing_blobs(&needed_blobs).await;
    for mut blob in missing_blobs {
        let buf = contents.remove(&blob.blob_id.into()).unwrap();

        let (content_blob, content_blob_server_end) =
            fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
        assert!(needed_blobs
            .open_blob(&mut blob.blob_id, content_blob_server_end)
            .await
            .unwrap()
            .unwrap());

        let () = write_blob(&buf, content_blob).await.unwrap();

        // Run a GC to try to reap blobs protected by meta far.
        assert_matches!(env.proxies.space_manager.gc().await, Ok(Ok(())));
    }

    let () = get_fut.await.unwrap().unwrap();
    let () = pkgprime.verify_contents(&dir).await.unwrap();
    assert_matches!(env.proxies.space_manager.gc().await, Ok(Ok(())));

    // At this point, we expect blobfs to only contain the blobs from the system image package and
    // from pkgprime.
    let expected_blobs =
        initial_blobs.union(&pkgprime.list_blobs().unwrap()).cloned().collect::<BTreeSet<_>>();

    assert_eq!(env.blobfs.list_blobs().expect("all blobs"), expected_blobs);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn blob_write_fails_when_out_of_space() {
    let system_image_package = SystemImageBuilder::new().build().await;

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

    // A very large version of the same package, to put in the repo.
    // Critically, this package contains an incompressible 4MB asset in the meta.far,
    // which is larger than our blobfs, and attempting to resolve this package will result in
    // blobfs returning out of space.
    const LARGE_ASSET_FILE_SIZE: u64 = 2 * 1024 * 1024;
    let mut rng = StdRng::from_seed([0u8; 32]);
    let rng = &mut rng as &mut dyn RngCore;
    let pkg = PackageBuilder::new("pkg-a")
        .add_resource_at("meta/asset", rng.take(LARGE_ASSET_FILE_SIZE))
        .build()
        .await
        .expect("build large package");

    // The size of the meta far should be the size of our asset, plus three 4k-aligned files:
    //  - meta/contents
    //  - meta/fuchsia.abi/abi-revision
    //  - meta/package
    // Content chunks in FARs are 4KiB-aligned, so the most empty FAR we can get is 12KiB:
    // meta/package at one alignment boundary, meta/fuchsia.abi/abi-revision at the next alignment
    // boundary, and meta/contents is empty.
    // This FAR should be 12KiB + the size of our asset file.
    assert_eq!(
        pkg.meta_far().unwrap().metadata().unwrap().len(),
        LARGE_ASSET_FILE_SIZE + 4096 + 4096 + 4096
    );

    let env = TestEnv::builder()
        .blobfs_and_system_image_hash(
            very_small_blobfs,
            Some(*system_image_package.meta_far_merkle_root()),
        )
        .build()
        .await;

    let mut meta_blob_info =
        fpkg::BlobInfo { blob_id: BlobId::from(*pkg.meta_far_merkle_root()).into(), length: 0 };

    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let (_dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let _get_fut = env
        .proxies
        .package_cache
        .get(&mut meta_blob_info, needed_blobs_server_end, Some(dir_server_end))
        .map_ok(|res| res.map_err(Status::from_raw));

    let (meta_blob, meta_blob_server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
    let res = needed_blobs.open_meta_blob(meta_blob_server_end).await.unwrap().unwrap();
    assert!(res);

    let (meta_far, _contents) = pkg.contents();
    assert_eq!(write_blob(&meta_far.contents, meta_blob).await, Err(Status::NO_SPACE));
}

enum GcProtection {
    Dynamic,
    Retained,
}

async fn subpackage_blobs_protected_from_gc(gc_protection: GcProtection) {
    let env = TestEnv::builder().enable_subpackages().build().await;

    let subpackage = PackageBuilder::new("subpackage")
        .add_resource_at("subpackage-blob-0", "subpackage-blob-contents-0".as_bytes())
        .add_resource_at("subpackage-blob-1", "subpackage-blob-contents-1".as_bytes())
        .build()
        .await
        .unwrap();

    let superpackage = PackageBuilder::new("superpackage")
        .add_subpackage("my-subpackage", &subpackage)
        .build()
        .await
        .unwrap();

    match gc_protection {
        GcProtection::Retained => {
            crate::replace_retained_packages(
                &env.proxies.retained_packages,
                &[(*superpackage.meta_far_merkle_root()).into()],
            )
            .await
        }

        // Ephemeral packages are added to the dynamic index unless they are already in the
        // retained index.
        GcProtection::Dynamic => (),
    }

    // Start the Get.
    let package_cache = &env.proxies.package_cache;
    let mut meta_blob_info = fpkg::BlobInfo {
        blob_id: BlobId::from(*superpackage.meta_far_merkle_root()).into(),
        length: 0,
    };
    let (needed_blobs, needed_blobs_server_end) =
        fidl::endpoints::create_proxy::<NeededBlobsMarker>().unwrap();
    let (dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let get_fut = package_cache
        .get(&mut meta_blob_info, needed_blobs_server_end, Some(dir_server_end))
        .map_ok(|res| res.map_err(Status::from_raw));

    // Write the meta.far.
    let meta_far = superpackage.contents().0;
    let (meta_blob, meta_blob_server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
    assert!(needed_blobs.open_meta_blob(meta_blob_server_end).await.unwrap().unwrap());
    let () = write_blob(&meta_far.contents, meta_blob).await.unwrap();

    // Get the missing blobs iterator.
    let (blob_iterator, blob_iterator_server_end) =
        fidl::endpoints::create_proxy::<fpkg::BlobInfoIteratorMarker>().unwrap();
    let () = needed_blobs.get_missing_blobs(blob_iterator_server_end).unwrap();

    // Read the subpackage meta.far from missing blobs to guarantee the blob is protected from GC
    // and pkg-cache is ready to receive it.
    assert_eq!(
        blob_iterator.next().await.unwrap(),
        vec![fpkg::BlobInfo {
            blob_id: BlobId::from(*subpackage.meta_far_merkle_root()).into(),
            length: 0
        }]
    );

    // Subpackage meta.far should not be in blobfs yet.
    assert!(!env.blobfs.list_blobs().unwrap().contains(subpackage.meta_far_merkle_root()));

    // Write the subpackage meta.far.
    let subpackage_meta_far = subpackage.contents().0;
    let (subpackage_meta_blob, subpackage_meta_blob_server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
    assert!(needed_blobs
        .open_blob(
            &mut BlobId::from(*subpackage.meta_far_merkle_root()).into(),
            subpackage_meta_blob_server_end
        )
        .await
        .unwrap()
        .unwrap());
    let () = write_blob(&subpackage_meta_far.contents, subpackage_meta_blob).await.unwrap();

    // Subpackage meta.far should now be in blobfs.
    assert!(env.blobfs.list_blobs().unwrap().contains(subpackage.meta_far_merkle_root()));

    // GC should not delete the subpackage meta.far
    let () = env.proxies.space_manager.gc().await.unwrap().unwrap();
    assert!(env.blobfs.list_blobs().unwrap().contains(subpackage.meta_far_merkle_root()));

    // Read the subpackage content blobs from missing blobs to guarantee the blobs are protected
    // from GC and pkg-cache is ready to receive them.
    let subpackage_content_files = subpackage.contents().1.into_iter().collect::<Vec<_>>();
    assert_eq!(
        blob_iterator.next().await.unwrap().into_iter().collect::<HashSet<_>>(),
        HashSet::from_iter([
            fpkg::BlobInfo {
                blob_id: BlobId::from(subpackage_content_files[0].0).into(),
                length: 0
            },
            fpkg::BlobInfo {
                blob_id: BlobId::from(subpackage_content_files[1].0).into(),
                length: 0
            },
        ])
    );

    // Subpackage content blob should not be in blobfs yet.
    assert!(!env.blobfs.list_blobs().unwrap().contains(&subpackage_content_files[0].0));

    // Write a subpackage content blob.
    let (subpackage_content_blob_a, subpackage_content_blob_a_server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
    assert!(needed_blobs
        .open_blob(
            &mut BlobId::from(subpackage_content_files[0].0).into(),
            subpackage_content_blob_a_server_end
        )
        .await
        .unwrap()
        .unwrap());
    let () = write_blob(&subpackage_content_files[0].1, subpackage_content_blob_a).await.unwrap();

    // Subpackage content blob should now be in blobfs.
    assert!(env.blobfs.list_blobs().unwrap().contains(&subpackage_content_files[0].0));

    // GC should not delete the subpackage content blob.
    let () = env.proxies.space_manager.gc().await.unwrap().unwrap();
    assert!(env.blobfs.list_blobs().unwrap().contains(&subpackage_content_files[0].0));

    // Write the other subpackage content blob.
    let (subpackage_content_blob_b, subpackage_content_blob_b_server_end) =
        fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
    assert!(needed_blobs
        .open_blob(
            &mut BlobId::from(subpackage_content_files[1].0).into(),
            subpackage_content_blob_b_server_end
        )
        .await
        .unwrap()
        .unwrap());
    let () = write_blob(&subpackage_content_files[1].1, subpackage_content_blob_b).await.unwrap();

    // Other subpackage content blob should now be in blobfs.
    assert!(env.blobfs.list_blobs().unwrap().contains(&subpackage_content_files[1].0));

    // GC should not delete the other subpackage content blob.
    let () = env.proxies.space_manager.gc().await.unwrap().unwrap();
    assert!(env.blobfs.list_blobs().unwrap().contains(&subpackage_content_files[1].0));

    // Complete the Get.
    assert_eq!(blob_iterator.next().await.unwrap(), vec![]);
    let () = get_fut.await.unwrap().unwrap();
    let () = superpackage.verify_contents(&dir).await.unwrap();

    // GC still shouldn't delete any subpackage blobs.
    let () = env.proxies.space_manager.gc().await.unwrap().unwrap();
    let blobfs_contents = env.blobfs.list_blobs().unwrap();
    assert!(blobfs_contents.contains(subpackage.meta_far_merkle_root()));
    assert!(blobfs_contents.contains(&subpackage_content_files[0].0));
    assert!(blobfs_contents.contains(&subpackage_content_files[1].0));

    let () = env.stop().await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn subpackage_blobs_protected_from_gc_by_retained_index() {
    let () = subpackage_blobs_protected_from_gc(GcProtection::Retained).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn subpackage_blobs_protected_from_gc_by_dynamic_index() {
    let () = subpackage_blobs_protected_from_gc(GcProtection::Dynamic).await;
}
