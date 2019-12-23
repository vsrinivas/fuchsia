// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::{Error, ErrorKind};
use failure::ResultExt;
use fidl_fuchsia_io;
use fidl_fuchsia_pkg::{PackageResolverMarker, PackageResolverProxyInterface, UpdatePolicy};
use fuchsia_component::client::connect_to_service;
use fuchsia_merkle::Hash;
use fuchsia_zircon as zx;
use std::io::{self, BufRead, Read};

const UPDATE_PACKAGE_URL: &str = "fuchsia-pkg://fuchsia.com/update/0";

#[derive(PartialEq, Eq, Debug, Clone)]
pub enum SystemUpdateStatus {
    UpToDate {
        system_image: Hash,
        update_package: Hash,
    },
    UpdateAvailable {
        current_system_image: Hash,
        latest_system_image: Hash,
        latest_update_package: Hash,
    },
}

pub async fn check_for_system_update(
    last_known_update_merkle: Option<&Hash>,
) -> Result<SystemUpdateStatus, Error> {
    let mut file_system = RealFileSystem;
    let package_resolver =
        connect_to_service::<PackageResolverMarker>().context(ErrorKind::ConnectPackageResolver)?;
    check_for_system_update_impl(&mut file_system, &package_resolver, last_known_update_merkle)
        .await
}

// For mocking
trait FileSystem {
    fn read_to_string(&self, path: &str) -> io::Result<String>;
    fn remove_file(&mut self, path: &str) -> io::Result<()>;
}

struct RealFileSystem;

impl FileSystem for RealFileSystem {
    fn read_to_string(&self, path: &str) -> io::Result<String> {
        std::fs::read_to_string(path)
    }
    fn remove_file(&mut self, path: &str) -> io::Result<()> {
        std::fs::remove_file(path)
    }
}

async fn check_for_system_update_impl(
    file_system: &mut impl FileSystem,
    package_resolver: &impl PackageResolverProxyInterface,
    last_known_update_merkle: Option<&Hash>,
) -> Result<SystemUpdateStatus, Error> {
    let update = latest_update_package(package_resolver).await?;
    let latest_update_merkle = update_package_merkle(&update).await?;
    let current = current_system_image_merkle(file_system)?;
    let latest = latest_system_image_merkle(&update).await?;

    if let Some(last_known_update_merkle) = last_known_update_merkle {
        if *last_known_update_merkle != latest_update_merkle {
            return Ok(SystemUpdateStatus::UpdateAvailable {
                current_system_image: current,
                latest_system_image: latest,
                latest_update_package: latest_update_merkle,
            });
        }
    }

    if current == latest {
        Ok(SystemUpdateStatus::UpToDate {
            system_image: current,
            update_package: latest_update_merkle,
        })
    } else {
        Ok(SystemUpdateStatus::UpdateAvailable {
            current_system_image: current,
            latest_system_image: latest,
            latest_update_package: latest_update_merkle,
        })
    }
}

fn current_system_image_merkle(file_system: &impl FileSystem) -> Result<Hash, Error> {
    Ok(file_system
        .read_to_string("/system/meta")
        .context(ErrorKind::ReadSystemMeta)?
        .parse::<Hash>()
        .context(ErrorKind::ParseSystemMeta)?)
}

async fn latest_update_package(
    package_resolver: &impl PackageResolverProxyInterface,
) -> Result<fidl_fuchsia_io::DirectoryProxy, Error> {
    let (dir_proxy, dir_server_end) =
        fidl::endpoints::create_proxy().context(ErrorKind::CreateUpdatePackageDirectoryProxy)?;
    let fut = package_resolver.resolve(
        &UPDATE_PACKAGE_URL,
        &mut vec![].into_iter(),
        &mut UpdatePolicy { fetch_if_absent: true, allow_old_versions: false },
        dir_server_end,
    );
    let status = fut.await.context(ErrorKind::ResolveUpdatePackageFidl)?;
    zx::Status::ok(status).context(ErrorKind::ResolveUpdatePackage)?;
    Ok(dir_proxy)
}

async fn update_package_merkle(
    update_package: &fidl_fuchsia_io::DirectoryProxy,
) -> Result<Hash, Error> {
    let (file_end, file_server_end) = fidl::endpoints::create_endpoints()
        .context(ErrorKind::CreateUpdatePackagePackagesEndpoint)?;
    update_package
        .open(
            fidl_fuchsia_io::OPEN_FLAG_NOT_DIRECTORY | fidl_fuchsia_io::OPEN_RIGHT_READABLE,
            0,
            "meta",
            file_server_end,
        )
        .context(ErrorKind::OpenUpdatePackageMeta)?;

    // danger: synchronous io in async
    let mut meta_file =
        fdio::create_fd(file_end.into_channel().into()).context(ErrorKind::ReadUpdateMeta)?;
    let mut contents = String::new();
    meta_file.read_to_string(&mut contents).context(ErrorKind::ReadUpdateMeta)?;
    Ok(contents.parse::<Hash>().context(ErrorKind::ParseUpdateMeta)?)
}

pub async fn latest_update_merkle(
    package_resolver: &impl PackageResolverProxyInterface,
) -> Result<Hash, Error> {
    let update = latest_update_package(package_resolver).await?;
    update_package_merkle(&update).await
}

async fn latest_system_image_merkle(
    update_package: &fidl_fuchsia_io::DirectoryProxy,
) -> Result<Hash, Error> {
    let (file_end, file_server_end) = fidl::endpoints::create_endpoints()
        .context(ErrorKind::CreateUpdatePackagePackagesEndpoint)?;
    update_package
        .open(
            fidl_fuchsia_io::OPEN_FLAG_NOT_DIRECTORY | fidl_fuchsia_io::OPEN_RIGHT_READABLE,
            0,
            "packages",
            file_server_end,
        )
        .context(ErrorKind::OpenUpdatePackagePackages)?;

    // danger: synchronous io in async
    let packages_file =
        fdio::create_fd(file_end.into_channel().into()).context(ErrorKind::CreatePackagesFd)?;
    extract_system_image_merkle_from_update_packages(packages_file)
}

fn extract_system_image_merkle_from_update_packages(reader: impl io::Read) -> Result<Hash, Error> {
    for line in io::BufReader::new(reader).lines() {
        let line = line.context(ErrorKind::ReadPackages)?;
        if let Some(i) = line.rfind('=') {
            let (key, value) = line.split_at(i + 1);
            if key == "system_image/0=" {
                return Ok(value
                    .parse::<Hash>()
                    .context(ErrorKind::ParseLatestSystemImageMerkle { packages_entry: line })?);
            }
        }
    }
    Err(ErrorKind::MissingLatestSystemImageMerkle)?
}

#[cfg(test)]
pub mod test_check_for_system_update_impl {
    use super::*;
    use fuchsia_async::{self as fasync, futures::future};
    use lazy_static::lazy_static;
    use maplit::hashmap;
    use matches::assert_matches;
    use std::collections::hash_map::HashMap;
    use std::fs;
    use std::io::Write;

    const ACTIVE_SYSTEM_IMAGE_MERKLE: &str =
        "0000000000000000000000000000000000000000000000000000000000000000";
    const NEW_SYSTEM_IMAGE_MERKLE: &str =
        "1111111111111111111111111111111111111111111111111111111111111111";

    lazy_static! {
        static ref UPDATE_PACKAGE_MERKLE: Hash = { [0x22; 32].into() };
    }

    struct FakeFileSystem {
        contents: HashMap<String, String>,
    }
    impl FakeFileSystem {
        fn new_with_valid_system_meta() -> FakeFileSystem {
            FakeFileSystem {
                contents: hashmap![
                    "/system/meta".to_string() => ACTIVE_SYSTEM_IMAGE_MERKLE.to_string()
                ],
            }
        }
    }
    impl FileSystem for FakeFileSystem {
        fn read_to_string(&self, path: &str) -> io::Result<String> {
            self.contents
                .get(path)
                .ok_or(io::Error::new(
                    io::ErrorKind::NotFound,
                    format!("not present in fake file system: {}", path),
                ))
                .map(|s| s.to_string())
        }
        fn remove_file(&mut self, path: &str) -> io::Result<()> {
            self.contents.remove(path).and(Some(())).ok_or(io::Error::new(
                io::ErrorKind::NotFound,
                format!("fake file system cannot remove non-existent file: {}", path),
            ))
        }
    }

    pub struct PackageResolverProxyTempDir {
        temp_dir: tempfile::TempDir,
    }
    impl PackageResolverProxyTempDir {
        fn new_with_default_meta() -> PackageResolverProxyTempDir {
            let temp_dir = tempfile::tempdir().expect("create temp dir");
            fs::write(temp_dir.path().join("meta"), UPDATE_PACKAGE_MERKLE.to_string())
                .expect("write meta");
            PackageResolverProxyTempDir { temp_dir }
        }
        pub fn new_with_merkle(merkle: &Hash) -> PackageResolverProxyTempDir {
            let temp_dir = tempfile::tempdir().expect("create temp dir");
            fs::write(temp_dir.path().join("meta"), merkle.to_string()).expect("write meta");
            PackageResolverProxyTempDir { temp_dir }
        }
        fn new_with_empty_packages_file() -> PackageResolverProxyTempDir {
            let temp_dir = tempfile::tempdir().expect("create temp dir");
            fs::write(temp_dir.path().join("meta"), UPDATE_PACKAGE_MERKLE.to_string())
                .expect("write meta");
            fs::File::create(format!(
                "{}/packages",
                temp_dir.path().to_str().expect("path is utf8")
            ))
            .expect("create empty packages file");
            PackageResolverProxyTempDir { temp_dir }
        }
        fn new_with_latest_system_image_merkle(merkle: &str) -> PackageResolverProxyTempDir {
            let temp_dir = tempfile::tempdir().expect("create temp dir");
            fs::write(temp_dir.path().join("meta"), UPDATE_PACKAGE_MERKLE.to_string())
                .expect("write meta");
            let mut packages_file = fs::File::create(format!(
                "{}/packages",
                temp_dir.path().to_str().expect("path is utf8")
            ))
            .expect("create empty packages file");
            write!(&mut packages_file, "system_image/0={}\n", merkle)
                .expect("write to package file");
            PackageResolverProxyTempDir { temp_dir }
        }
    }
    impl PackageResolverProxyInterface for PackageResolverProxyTempDir {
        type ResolveResponseFut = future::Ready<Result<i32, fidl::Error>>;
        fn resolve(
            &self,
            package_url: &str,
            selectors: &mut dyn ExactSizeIterator<Item = &str>,
            update_policy: &mut UpdatePolicy,
            dir: fidl::endpoints::ServerEnd<fidl_fuchsia_io::DirectoryMarker>,
        ) -> Self::ResolveResponseFut {
            assert_eq!(package_url, UPDATE_PACKAGE_URL);
            assert_eq!(selectors.len(), 0);
            assert_eq!(
                update_policy,
                &UpdatePolicy { fetch_if_absent: true, allow_old_versions: false }
            );
            fdio::service_connect(
                self.temp_dir.path().to_str().expect("path is utf8"),
                dir.into_channel(),
            )
            .unwrap();
            future::ok(zx::sys::ZX_OK)
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_missing_system_meta_file() {
        let mut file_system = FakeFileSystem { contents: hashmap![] };
        let package_resolver = PackageResolverProxyTempDir::new_with_default_meta();

        let result = check_for_system_update_impl(&mut file_system, &package_resolver, None).await;

        assert_matches!(result.map_err(|e| e.kind()), Err(ErrorKind::ReadSystemMeta));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_malformatted_system_meta_file() {
        let mut file_system = FakeFileSystem {
            contents: hashmap![
                "/system/meta".to_string() => "not-a-merkle".to_string()
            ],
        };
        let package_resolver = PackageResolverProxyTempDir::new_with_default_meta();

        let result = check_for_system_update_impl(&mut file_system, &package_resolver, None).await;

        assert_matches!(result.map_err(|e| e.kind()), Err(ErrorKind::ParseSystemMeta));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_update_package_fidl_error() {
        struct PackageResolverProxyFidlError;
        impl PackageResolverProxyInterface for PackageResolverProxyFidlError {
            type ResolveResponseFut = future::Ready<Result<i32, fidl::Error>>;
            fn resolve(
                &self,
                _package_url: &str,
                _selectors: &mut dyn ExactSizeIterator<Item = &str>,
                _update_policy: &mut UpdatePolicy,
                _dir: fidl::endpoints::ServerEnd<fidl_fuchsia_io::DirectoryMarker>,
            ) -> Self::ResolveResponseFut {
                future::err(fidl::Error::Invalid)
            }
        }

        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyFidlError;

        let result = check_for_system_update_impl(&mut file_system, &package_resolver, None).await;

        assert_matches!(result.map_err(|e| e.kind()), Err(ErrorKind::ResolveUpdatePackageFidl));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_update_package_zx_error() {
        struct PackageResolverProxyZxError;
        impl PackageResolverProxyInterface for PackageResolverProxyZxError {
            type ResolveResponseFut = future::Ready<Result<i32, fidl::Error>>;
            fn resolve(
                &self,
                _package_url: &str,
                _selectors: &mut dyn ExactSizeIterator<Item = &str>,
                _update_policy: &mut UpdatePolicy,
                _dir: fidl::endpoints::ServerEnd<fidl_fuchsia_io::DirectoryMarker>,
            ) -> Self::ResolveResponseFut {
                future::ok(zx::sys::ZX_ERR_INTERNAL)
            }
        }

        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyZxError;

        let result = check_for_system_update_impl(&mut file_system, &package_resolver, None).await;

        assert_matches!(result.map_err(|e| e.kind()), Err(ErrorKind::ResolveUpdatePackage));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_update_package_directory_closed() {
        struct PackageResolverProxyDirectoryCloser;
        impl PackageResolverProxyInterface for PackageResolverProxyDirectoryCloser {
            type ResolveResponseFut = future::Ready<Result<i32, fidl::Error>>;
            fn resolve(
                &self,
                _package_url: &str,
                _selectors: &mut dyn ExactSizeIterator<Item = &str>,
                _update_policy: &mut UpdatePolicy,
                _dir: fidl::endpoints::ServerEnd<fidl_fuchsia_io::DirectoryMarker>,
            ) -> Self::ResolveResponseFut {
                future::ok(zx::sys::ZX_OK)
            }
        }

        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyDirectoryCloser;

        let result = check_for_system_update_impl(&mut file_system, &package_resolver, None).await;

        assert_matches!(result.map_err(|e| e.kind()), Err(ErrorKind::OpenUpdatePackageMeta));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_package_missing_packages_file() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_default_meta();

        let result = check_for_system_update_impl(&mut file_system, &package_resolver, None).await;

        assert_matches!(result.map_err(|e| e.kind()), Err(ErrorKind::CreatePackagesFd));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_package_empty_packages_file() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_empty_packages_file();

        let result = check_for_system_update_impl(&mut file_system, &package_resolver, None).await;

        assert_matches!(
            result.map_err(|e| e.kind()),
            Err(ErrorKind::MissingLatestSystemImageMerkle)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_package_bad_system_image_merkle() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver =
            PackageResolverProxyTempDir::new_with_latest_system_image_merkle("bad-merkle");

        let result = check_for_system_update_impl(&mut file_system, &package_resolver, None).await;

        assert_matches!(
            result.map_err(|e| e.kind()),
            Err(ErrorKind::ParseLatestSystemImageMerkle {
                ref packages_entry
            })
            if packages_entry == "system_image/0=bad-merkle"
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_up_to_date() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_latest_system_image_merkle(
            ACTIVE_SYSTEM_IMAGE_MERKLE,
        );

        let result = check_for_system_update_impl(
            &mut file_system,
            &package_resolver,
            Some(&UPDATE_PACKAGE_MERKLE),
        )
        .await;
        assert_matches!(
            result,
            Ok(SystemUpdateStatus::UpToDate { system_image, update_package: _ })
            if system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                .parse()
                .expect("active system image string literal")
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_available() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_latest_system_image_merkle(
            NEW_SYSTEM_IMAGE_MERKLE,
        );

        let result = check_for_system_update_impl(&mut file_system, &package_resolver, None).await;

        assert_matches!(
            result,
            Ok(SystemUpdateStatus::UpdateAvailable { current_system_image, latest_system_image, latest_update_package: _ })
            if
                current_system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                    .parse()
                    .expect("active system image string literal") &&
                latest_system_image == NEW_SYSTEM_IMAGE_MERKLE
                    .parse()
                    .expect("new system image string literal")
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_package_only_update_available() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_latest_system_image_merkle(
            ACTIVE_SYSTEM_IMAGE_MERKLE,
        );

        let previous_update_package = Hash::from([0x44; 32]);
        let result = check_for_system_update_impl(
            &mut file_system,
            &package_resolver,
            Some(&previous_update_package),
        )
        .await;

        assert_matches!(
            result,
            Ok(SystemUpdateStatus::UpdateAvailable { current_system_image, latest_system_image, latest_update_package })
            if
                current_system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                    .parse()
                    .expect("active system image string literal") &&
                latest_system_image == ACTIVE_SYSTEM_IMAGE_MERKLE
                    .parse()
                    .expect("new system image string literal") &&
                latest_update_package == *UPDATE_PACKAGE_MERKLE
        );
    }
}

#[cfg(test)]
mod test_real_file_system {
    use super::*;
    use matches::assert_matches;
    use proptest::prelude::*;
    use std::fs;
    use std::io::{self, Write};

    #[test]
    fn test_read_to_string_errors_on_missing_file() {
        let dir = tempfile::tempdir().expect("create temp dir");
        let read_res = RealFileSystem.read_to_string(
            dir.path().join("this-file-does-not-exist").to_str().expect("paths are utf8"),
        );
        assert_matches!(read_res.map_err(|e| e.kind()), Err(io::ErrorKind::NotFound));
    }

    proptest! {
        #[test]
        fn test_read_to_string_preserves_contents(
            contents in ".{0, 65}",
            file_name in "[^\\.\0/]{1,10}",
        ) {
            let dir = tempfile::tempdir().expect("create temp dir");
            let file_path = dir.path().join(file_name);
            let mut file = fs::File::create(&file_path).expect("create file");
            file.write_all(contents.as_bytes()).expect("write the contents");

            let read_contents = RealFileSystem
                .read_to_string(file_path.to_str().expect("paths are utf8"))
                .expect("read the file");

            prop_assert_eq!(read_contents, contents);
        }
    }
}
