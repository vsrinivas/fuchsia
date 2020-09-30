// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::{self, Error};
use fidl_fuchsia_pkg::{PackageResolverMarker, PackageResolverProxyInterface, UpdatePolicy};
use fuchsia_component::client::connect_to_service;
use fuchsia_hash::Hash;
use fuchsia_zircon as zx;
use std::io;
use update_package::UpdatePackage;

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
        connect_to_service::<PackageResolverMarker>().map_err(Error::ConnectPackageResolver)?;
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
) -> Result<SystemUpdateStatus, crate::errors::Error> {
    let update_pkg = latest_update_package(package_resolver).await?;
    let latest_update_merkle = update_pkg.hash().await.map_err(errors::UpdatePackage::Hash)?;
    let current_system_image = current_system_image_merkle(file_system)?;
    let latest_system_image = latest_system_image_merkle(&update_pkg).await?;

    if let Some(last_known_update_merkle) = last_known_update_merkle {
        if *last_known_update_merkle != latest_update_merkle {
            return Ok(SystemUpdateStatus::UpdateAvailable {
                current_system_image,
                latest_system_image,
                latest_update_package: latest_update_merkle,
            });
        }
    }

    if current_system_image == latest_system_image {
        Ok(SystemUpdateStatus::UpToDate {
            system_image: current_system_image,
            update_package: latest_update_merkle,
        })
    } else {
        Ok(SystemUpdateStatus::UpdateAvailable {
            current_system_image,
            latest_system_image,
            latest_update_package: latest_update_merkle,
        })
    }
}

fn current_system_image_merkle(
    file_system: &impl FileSystem,
) -> Result<Hash, crate::errors::Error> {
    Ok(file_system
        .read_to_string("/pkgfs/system/meta")
        .map_err(Error::ReadSystemMeta)?
        .parse::<Hash>()
        .map_err(Error::ParseSystemMeta)?)
}

async fn latest_update_package(
    package_resolver: &impl PackageResolverProxyInterface,
) -> Result<UpdatePackage, errors::UpdatePackage> {
    let (dir_proxy, dir_server_end) =
        fidl::endpoints::create_proxy().map_err(errors::UpdatePackage::CreateDirectoryProxy)?;
    let fut = package_resolver.resolve(
        &UPDATE_PACKAGE_URL,
        &mut vec![].into_iter(),
        &mut UpdatePolicy { fetch_if_absent: true, allow_old_versions: false },
        dir_server_end,
    );
    let () = fut
        .await
        .map_err(errors::UpdatePackage::ResolveFidl)?
        .map_err(|raw| errors::UpdatePackage::Resolve(zx::Status::from_raw(raw)))?;
    Ok(UpdatePackage::new(dir_proxy))
}

pub async fn latest_update_merkle(
    package_resolver: &impl PackageResolverProxyInterface,
) -> Result<Hash, errors::UpdatePackage> {
    let update = latest_update_package(package_resolver).await?;
    update.hash().await.map_err(errors::UpdatePackage::Hash)
}

async fn latest_system_image_merkle(
    update_package: &UpdatePackage,
) -> Result<Hash, errors::UpdatePackage> {
    let packages =
        update_package.packages().await.map_err(errors::UpdatePackage::ExtractPackagesManifest)?;
    let system_image = packages
        .into_iter()
        .find(|url| url.name() == "system_image" && url.variant() == Some("0"))
        .ok_or(errors::UpdatePackage::MissingSystemImage)?;
    let hash = system_image
        .package_hash()
        .ok_or(errors::UpdatePackage::UnPinnedSystemImage(system_image.clone()))?;
    Ok(hash.to_owned())
}

#[cfg(test)]
pub mod test_check_for_system_update_impl {
    use super::*;
    use fidl_fuchsia_pkg::{
        PackageResolverGetHashResult, PackageResolverResolveResult, PackageUrl,
    };
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
        static ref UPDATE_PACKAGE_MERKLE: Hash = [0x22; 32].into();
    }

    struct FakeFileSystem {
        contents: HashMap<String, String>,
    }
    impl FakeFileSystem {
        fn new_with_valid_system_meta() -> FakeFileSystem {
            FakeFileSystem {
                contents: hashmap![
                    "/pkgfs/system/meta".to_string() => ACTIVE_SYSTEM_IMAGE_MERKLE.to_string()
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

        fn new_with_empty_packages_json() -> PackageResolverProxyTempDir {
            let temp_dir = tempfile::tempdir().expect("create temp dir");
            fs::write(temp_dir.path().join("meta"), UPDATE_PACKAGE_MERKLE.to_string())
                .expect("write meta");
            let mut packages_json = fs::File::create(
                temp_dir.path().join("packages.json").to_str().expect("path is utf8"),
            )
            .expect("create packages.json");
            write!(&mut packages_json, r#"{{"version": "1", "content": []}}"#)
                .expect("write json with no contents");
            PackageResolverProxyTempDir { temp_dir }
        }

        fn new_with_latest_system_image(merkle: &str) -> PackageResolverProxyTempDir {
            let temp_dir = tempfile::tempdir().expect("create temp dir");
            fs::write(temp_dir.path().join("meta"), UPDATE_PACKAGE_MERKLE.to_string())
                .expect("write meta");
            let mut packages_json = fs::File::create(
                temp_dir.path().join("packages.json").to_str().expect("path is utf8"),
            )
            .expect("create packages.json");
            write!(
                &mut packages_json,
                r#"{{"version": "1", "content": ["fuchsia-pkg://fuchsia.com/system_image/0?hash={}"]}}"#,
                merkle
            )
            .expect("write to package file");
            PackageResolverProxyTempDir { temp_dir }
        }
    }
    impl PackageResolverProxyInterface for PackageResolverProxyTempDir {
        type ResolveResponseFut = future::Ready<Result<PackageResolverResolveResult, fidl::Error>>;
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
            future::ok(Ok(()))
        }

        type GetHashResponseFut = future::Ready<Result<PackageResolverGetHashResult, fidl::Error>>;
        fn get_hash(&self, _package_url: &mut PackageUrl) -> Self::GetHashResponseFut {
            panic!("get_hash not implemented");
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_missing_system_meta_file() {
        let mut file_system = FakeFileSystem { contents: hashmap![] };
        let package_resolver = PackageResolverProxyTempDir::new_with_default_meta();

        let result = check_for_system_update_impl(&mut file_system, &package_resolver, None).await;

        assert_matches!(result, Err(Error::ReadSystemMeta(_)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_malformatted_system_meta_file() {
        let mut file_system = FakeFileSystem {
            contents: hashmap![
                "/pkgfs/system/meta".to_string() => "not-a-merkle".to_string()
            ],
        };
        let package_resolver = PackageResolverProxyTempDir::new_with_default_meta();

        let result = check_for_system_update_impl(&mut file_system, &package_resolver, None).await;

        assert_matches!(result, Err(Error::ParseSystemMeta(_)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_update_package_fidl_error() {
        struct PackageResolverProxyFidlError;
        impl PackageResolverProxyInterface for PackageResolverProxyFidlError {
            type ResolveResponseFut =
                future::Ready<Result<PackageResolverResolveResult, fidl::Error>>;
            fn resolve(
                &self,
                _package_url: &str,
                _selectors: &mut dyn ExactSizeIterator<Item = &str>,
                _update_policy: &mut UpdatePolicy,
                _dir: fidl::endpoints::ServerEnd<fidl_fuchsia_io::DirectoryMarker>,
            ) -> Self::ResolveResponseFut {
                future::err(fidl::Error::Invalid)
            }
            type GetHashResponseFut =
                future::Ready<Result<PackageResolverGetHashResult, fidl::Error>>;
            fn get_hash(&self, _package_url: &mut PackageUrl) -> Self::GetHashResponseFut {
                panic!("get_hash not implemented");
            }
        }

        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyFidlError;

        let result = check_for_system_update_impl(&mut file_system, &package_resolver, None).await;

        assert_matches!(result, Err(Error::UpdatePackage(errors::UpdatePackage::ResolveFidl(_))));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_update_package_zx_error() {
        struct PackageResolverProxyZxError;
        impl PackageResolverProxyInterface for PackageResolverProxyZxError {
            type ResolveResponseFut =
                future::Ready<Result<PackageResolverResolveResult, fidl::Error>>;
            fn resolve(
                &self,
                _package_url: &str,
                _selectors: &mut dyn ExactSizeIterator<Item = &str>,
                _update_policy: &mut UpdatePolicy,
                _dir: fidl::endpoints::ServerEnd<fidl_fuchsia_io::DirectoryMarker>,
            ) -> Self::ResolveResponseFut {
                future::ok(Err(fuchsia_zircon::Status::INTERNAL.into_raw()))
            }
            type GetHashResponseFut =
                future::Ready<Result<PackageResolverGetHashResult, fidl::Error>>;
            fn get_hash(&self, _package_url: &mut PackageUrl) -> Self::GetHashResponseFut {
                panic!("get_hash not implemented");
            }
        }

        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyZxError;

        let result = check_for_system_update_impl(&mut file_system, &package_resolver, None).await;

        assert_matches!(result, Err(Error::UpdatePackage(errors::UpdatePackage::Resolve(_))));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_update_package_directory_closed() {
        struct PackageResolverProxyDirectoryCloser;
        impl PackageResolverProxyInterface for PackageResolverProxyDirectoryCloser {
            type ResolveResponseFut =
                future::Ready<Result<PackageResolverResolveResult, fidl::Error>>;
            fn resolve(
                &self,
                _package_url: &str,
                _selectors: &mut dyn ExactSizeIterator<Item = &str>,
                _update_policy: &mut UpdatePolicy,
                _dir: fidl::endpoints::ServerEnd<fidl_fuchsia_io::DirectoryMarker>,
            ) -> Self::ResolveResponseFut {
                future::ok(Ok(()))
            }
            type GetHashResponseFut =
                future::Ready<Result<PackageResolverGetHashResult, fidl::Error>>;
            fn get_hash(&self, _package_url: &mut PackageUrl) -> Self::GetHashResponseFut {
                panic!("get_hash not implemented");
            }
        }

        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyDirectoryCloser;

        let result = check_for_system_update_impl(&mut file_system, &package_resolver, None).await;

        assert_matches!(result, Err(Error::UpdatePackage(errors::UpdatePackage::Hash(_))));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_package_missing_packages_json() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_default_meta();

        let result = check_for_system_update_impl(&mut file_system, &package_resolver, None).await;

        assert_matches!(
            result,
            Err(Error::UpdatePackage(errors::UpdatePackage::ExtractPackagesManifest(_)))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_package_empty_packages_json() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver = PackageResolverProxyTempDir::new_with_empty_packages_json();

        let result = check_for_system_update_impl(&mut file_system, &package_resolver, None).await;

        assert_matches!(
            result,
            Err(Error::UpdatePackage(errors::UpdatePackage::MissingSystemImage))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_package_bad_system_image() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver =
            PackageResolverProxyTempDir::new_with_latest_system_image("bad-merkle");

        let result = check_for_system_update_impl(&mut file_system, &package_resolver, None).await;
        assert_matches!(
            result,
            Err(Error::UpdatePackage(errors::UpdatePackage::ExtractPackagesManifest(_)))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_up_to_date() {
        let mut file_system = FakeFileSystem::new_with_valid_system_meta();
        let package_resolver =
            PackageResolverProxyTempDir::new_with_latest_system_image(ACTIVE_SYSTEM_IMAGE_MERKLE);

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
        let package_resolver =
            PackageResolverProxyTempDir::new_with_latest_system_image(NEW_SYSTEM_IMAGE_MERKLE);

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
        let package_resolver =
            PackageResolverProxyTempDir::new_with_latest_system_image(ACTIVE_SYSTEM_IMAGE_MERKLE);

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
