// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests the property that pkg_resolver successfully
/// services fuchsia.pkg.PackageResolver.Resolve FIDL requests for
/// different types of packages and when blobfs and pkgfs are in
/// various intermediate states.
use {
    super::*,
    fidl_fuchsia_pkg_ext::MirrorConfigBuilder,
    fuchsia_merkle::{Hash, MerkleTree},
    fuchsia_pkg_testing::RepositoryBuilder,
    matches::assert_matches,
    std::{
        fs::File,
        io::{self, Read},
        sync::Arc,
    },
};

impl TestEnv<TestPkgFs> {
    fn add_slice_to_blobfs(&self, slice: &[u8]) {
        let merkle = MerkleTree::from_reader(slice).expect("merkle slice").root().to_string();
        let mut blob = self
            .pkgfs
            .blobfs()
            .as_dir()
            .expect("blobfs has root dir")
            .write_file(merkle, 0)
            .expect("create file in blobfs");
        blob.set_len(slice.len() as u64).expect("set_len");
        io::copy(&mut &slice[..], &mut blob).expect("copy from slice to blob");
    }

    fn add_file_with_merkle_to_blobfs(&self, mut file: File, merkle: &Hash) {
        let mut blob = self
            .pkgfs
            .blobfs()
            .as_dir()
            .expect("blobfs has root dir")
            .write_file(merkle.to_string(), 0)
            .expect("create file in blobfs");
        blob.set_len(file.metadata().expect("file has metadata").len()).expect("set_len");
        io::copy(&mut file, &mut blob).expect("copy file to blobfs");
    }

    fn add_file_to_pkgfs_at_path(&self, mut file: File, path: impl openat::AsPath) {
        let mut blob = self
            .pkgfs
            .root_dir()
            .expect("pkgfs root_dir")
            .new_file(path, 0)
            .expect("create file in pkgfs");
        blob.set_len(file.metadata().expect("file has metadata").len()).expect("set_len");
        io::copy(&mut file, &mut blob).expect("copy file to pkgfs");
    }

    fn partially_add_file_to_pkgfs_at_path(&self, mut file: File, path: impl openat::AsPath) {
        let full_len = file.metadata().expect("file has metadata").len();
        assert!(full_len > 1, "can't partially write 1 byte");
        let mut partial_bytes = vec![0; full_len as usize / 2];
        file.read_exact(partial_bytes.as_mut_slice()).expect("partial read of file");
        let mut blob = self
            .pkgfs
            .root_dir()
            .expect("pkgfs root_dir")
            .new_file(path, 0)
            .expect("create file in pkgfs");
        blob.set_len(full_len).expect("set_len");
        io::copy(&mut partial_bytes.as_slice(), &mut blob).expect("copy file to pkgfs");
    }

    fn partially_add_slice_to_pkgfs_at_path(&self, slice: &[u8], path: impl openat::AsPath) {
        assert!(slice.len() > 1, "can't partially write 1 byte");
        let partial_slice = &slice[0..slice.len() / 2];
        let mut blob = self
            .pkgfs
            .root_dir()
            .expect("pkgfs root_dir")
            .new_file(path, 0)
            .expect("create file in pkgfs");
        blob.set_len(slice.len() as u64).expect("set_len");
        io::copy(&mut &partial_slice[..], &mut blob).expect("copy file to pkgfs");
    }
}

#[fasync::run_singlethreaded(test)]
async fn package_resolution() -> Result<(), Error> {
    let env = TestEnv::new();

    let pkg = PackageBuilder::new("rolldice")
        .add_resource_at("bin/rolldice", ROLLDICE_BIN)?
        .add_resource_at("meta/rolldice.cmx", ROLLDICE_CMX)?
        .add_resource_at("data/duplicate_a", "same contents".as_bytes())?
        .add_resource_at("data/duplicate_b", "same contents".as_bytes())?
        .build()
        .await?;
    let repo =
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).add_package(&pkg).build().await?;
    let served_repository = repo.serve(env.launcher()).await?;

    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);

    env.proxies.repo_manager.add(repo_config.into()).await?;

    let package = env
        .resolve_package("fuchsia-pkg://test/rolldice")
        .await
        .expect("package to resolve without error");

    // Verify the served package directory contains the exact expected contents.
    pkg.verify_contents(&package).await.expect("correct package contents");

    // All blobs in the repository should now be present in blobfs.
    assert_eq!(env.pkgfs.blobfs().list_blobs().unwrap(), repo.list_blobs().unwrap());

    env.stop().await;

    Ok(())
}

async fn verify_separate_blobs_url(download_blob: bool) -> Result<(), Error> {
    let env = TestEnv::new();
    let pkg = make_rolldice_pkg_with_extra_blobs(3).await?;
    let repo =
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).add_package(&pkg).build().await?;
    let served_repository = repo.serve(env.launcher()).await?;

    // Rename the blobs directory so the blobs can't be found in the usual place.
    // Both amber and the package resolver currently require Content-Length headers when
    // downloading content blobs. "pm serve" will gzip compress paths that aren't prefixed with
    // "/blobs", which removes the Content-Length header. To ensure "pm serve" does not compress
    // the blobs stored at this alternate path, its name must start with "blobs".
    let repo_root = repo.path();
    std::fs::rename(repo_root.join("blobs"), repo_root.join("blobsbolb"))?;

    // Configure the repo manager with different TUF and blobs URLs.
    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let mut repo_config = served_repository.make_repo_config(repo_url);
    let mirror = &repo_config.mirrors()[0];
    let mirror = MirrorConfigBuilder::new(mirror.mirror_url())
        .subscribe(mirror.subscribe())
        .blob_mirror_url(format!("{}/blobsbolb", mirror.mirror_url()))
        .build();
    repo_config.insert_mirror(mirror).unwrap();
    env.proxies.repo_manager.add(repo_config.into()).await?;

    // Optionally use the new install flow.
    if download_blob {
        env.set_experiment_state(Experiment::DownloadBlob, true).await;
    }

    // Verify package installation from the split repo succeeds.
    let package = env
        .resolve_package("fuchsia-pkg://test/rolldice")
        .await
        .expect("package to resolve without error");
    pkg.verify_contents(&package).await.expect("correct package contents");
    std::fs::rename(repo_root.join("blobsbolb"), repo_root.join("blobs"))?;
    assert_eq!(env.pkgfs.blobfs().list_blobs().unwrap(), repo.list_blobs().unwrap());

    env.stop().await;

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn separate_blobs_url() -> Result<(), Error> {
    verify_separate_blobs_url(false).await
}

#[fasync::run_singlethreaded(test)]
async fn download_blob_separate_blobs_url() -> Result<(), Error> {
    verify_separate_blobs_url(true).await
}

async fn verify_download_blob_resolve_with_altered_env(
    pkg: Package,
    alter_env: impl FnOnce(&TestEnv, &Package),
) -> Result<(), Error> {
    let env = TestEnv::new();

    let repo =
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).add_package(&pkg).build().await?;
    let served_repository = repo.serve(env.launcher()).await?;

    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);

    env.proxies.repo_manager.add(repo_config.into()).await?;

    env.set_experiment_state(Experiment::DownloadBlob, true).await;

    alter_env(&env, &pkg);

    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());
    let package_dir = env.resolve_package(&pkg_url).await.expect("package to resolve");

    pkg.verify_contents(&package_dir).await.expect("correct package contents");
    assert_eq!(env.pkgfs.blobfs().list_blobs().unwrap(), repo.list_blobs().unwrap());

    env.stop().await;

    Ok(())
}

fn verify_download_blob_resolve(pkg: Package) -> impl Future<Output = Result<(), Error>> {
    verify_download_blob_resolve_with_altered_env(pkg, |_, _| {})
}

#[fasync::run_singlethreaded(test)]
async fn download_blob_meta_far_only() -> Result<(), Error> {
    verify_download_blob_resolve(PackageBuilder::new("uniblob").build().await?).await
}

#[fasync::run_singlethreaded(test)]
async fn download_blob_meta_far_and_empty_blob() -> Result<(), Error> {
    verify_download_blob_resolve(
        PackageBuilder::new("emptyblob")
            .add_resource_at("data/empty", "".as_bytes())?
            .build()
            .await?,
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn download_blob_experiment_large_blobs() -> Result<(), Error> {
    verify_download_blob_resolve(
        PackageBuilder::new("numbers")
            .add_resource_at("bin/numbers", ROLLDICE_BIN)?
            .add_resource_at("data/ones", io::repeat(1).take(1 * 1024 * 1024))?
            .add_resource_at("data/twos", io::repeat(2).take(2 * 1024 * 1024))?
            .add_resource_at("data/threes", io::repeat(3).take(3 * 1024 * 1024))?
            .build()
            .await?,
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn download_blob_experiment_many_blobs() -> Result<(), Error> {
    verify_download_blob_resolve(make_rolldice_pkg_with_extra_blobs(200).await?).await
}

#[fasync::run_singlethreaded(test)]
async fn download_blob_experiment_identity() -> Result<(), Error> {
    verify_download_blob_resolve(Package::identity().await?).await
}

#[fasync::run_singlethreaded(test)]
async fn download_blob_experiment_identity_hyper() -> Result<(), Error> {
    let env = TestEnv::new();

    let pkg = Package::identity().await?;
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).add_package(&pkg).build().await?,
    );
    let served_repository = repo.build_server().start()?;
    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);

    env.proxies.repo_manager.add(repo_config.into()).await?;

    env.set_experiment_state(Experiment::DownloadBlob, true).await;

    let pkg_url = format!("fuchsia-pkg://test/{}", pkg.name());
    let package_dir = env.resolve_package(&pkg_url).await.expect("package to resolve");

    pkg.verify_contents(&package_dir).await.expect("correct package contents");

    env.stop().await;

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn download_blob_experiment_uses_cached_package() -> Result<(), Error> {
    let env = TestEnv::new();

    let pkg = PackageBuilder::new("resolve-twice")
        .add_resource_at("data/foo", "bar".as_bytes())?
        .build()
        .await?;
    let repo =
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH).add_package(&pkg).build().await?;
    let served_repository = repo.serve(env.launcher()).await?;

    let repo_url = "fuchsia-pkg://test".parse().unwrap();
    let repo_config = served_repository.make_repo_config(repo_url);

    env.set_experiment_state(Experiment::DownloadBlob, true).await;

    // the package can't be resolved before the repository is configured.
    assert_matches!(
        env.resolve_package("fuchsia-pkg://test/resolve-twice").await,
        Err(Status::NOT_FOUND)
    );

    env.proxies.repo_manager.add(repo_config.into()).await?;

    // package resolves as expected.
    let package_dir =
        env.resolve_package("fuchsia-pkg://test/resolve-twice").await.expect("package to resolve");
    pkg.verify_contents(&package_dir).await.expect("correct package contents");

    // if no mirrors are accessible, the cached package is returned.
    served_repository.stop().await;
    let package_dir =
        env.resolve_package("fuchsia-pkg://test/resolve-twice").await.expect("package to resolve");
    pkg.verify_contents(&package_dir).await.expect("correct package contents");

    env.stop().await;

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn meta_far_installed_blobs_not_installed() -> Result<(), Error> {
    verify_download_blob_resolve_with_altered_env(
        make_rolldice_pkg_with_extra_blobs(3).await?,
        |env, pkg| {
            env.add_file_to_pkgfs_at_path(
                pkg.meta_far().expect("package has meta.far"),
                format!("install/pkg/{}", pkg.meta_far_merkle_root().to_string()),
            )
        },
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn meta_far_partially_installed() -> Result<(), Error> {
    verify_download_blob_resolve_with_altered_env(
        make_rolldice_pkg_with_extra_blobs(3).await?,
        |env, pkg| {
            env.partially_add_file_to_pkgfs_at_path(
                pkg.meta_far().expect("package has meta.far"),
                format!("install/pkg/{}", pkg.meta_far_merkle_root().to_string()),
            )
        },
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn meta_far_already_in_blobfs() -> Result<(), Error> {
    verify_download_blob_resolve_with_altered_env(
        make_rolldice_pkg_with_extra_blobs(3).await?,
        |env, pkg| {
            env.add_file_with_merkle_to_blobfs(
                pkg.meta_far().expect("package has meta.far"),
                pkg.meta_far_merkle_root(),
            )
        },
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn all_blobs_already_in_blobfs() -> Result<(), Error> {
    verify_download_blob_resolve_with_altered_env(
        make_rolldice_pkg_with_extra_blobs(3).await?,
        |env, pkg| {
            env.add_file_with_merkle_to_blobfs(
                pkg.meta_far().expect("package has meta.far"),
                pkg.meta_far_merkle_root(),
            );
            env.add_slice_to_blobfs(ROLLDICE_BIN);
            for i in 0..3 {
                env.add_slice_to_blobfs(extra_blob_contents(i).as_slice());
            }
        },
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn meta_far_installed_one_blob_in_blobfs() -> Result<(), Error> {
    verify_download_blob_resolve_with_altered_env(
        make_rolldice_pkg_with_extra_blobs(3).await?,
        |env, pkg| {
            env.add_file_to_pkgfs_at_path(
                pkg.meta_far().expect("package has meta.far"),
                format!("install/pkg/{}", pkg.meta_far_merkle_root().to_string()),
            );
            env.add_slice_to_blobfs(ROLLDICE_BIN);
        },
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn meta_far_installed_one_blob_partially_installed() -> Result<(), Error> {
    verify_download_blob_resolve_with_altered_env(
        make_rolldice_pkg_with_extra_blobs(3).await?,
        |env, pkg| {
            env.add_file_to_pkgfs_at_path(
                pkg.meta_far().expect("package has meta.far"),
                format!("install/pkg/{}", pkg.meta_far_merkle_root().to_string()),
            );
            env.partially_add_slice_to_pkgfs_at_path(
                ROLLDICE_BIN,
                format!(
                    "install/blob/{}",
                    MerkleTree::from_reader(ROLLDICE_BIN).expect("merkle slice").root().to_string()
                ),
            );
        },
    )
    .await
}
