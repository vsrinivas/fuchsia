// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::resolver_service::PackageFetcher,
    anyhow::{anyhow, Context as _, Error},
    fuchsia_pkg::PackageDirectory,
    fuchsia_url::pkg_url::{Hash, PinnedPkgUrl, PkgUrl},
    serde::Deserialize,
    std::collections::BTreeMap,
};

const EAGER_PACKAGE_CONFIG_PATH: &str = "/config/data/eager_package_config.json";

#[derive(Debug)]
struct EagerPackage {
    #[allow(dead_code)]
    executable: bool,
    package_directory: Option<PackageDirectory>,
}

#[derive(Debug)]
pub struct EagerPackageManager {
    // Map from unpinned eager package URL to `EagerPackage`.
    packages: BTreeMap<PkgUrl, EagerPackage>,
    package_fetcher: PackageFetcher,
}

impl EagerPackageManager {
    #[allow(dead_code)]
    pub async fn from_namespace(package_fetcher: PackageFetcher) -> Result<Self, Error> {
        let config =
            EagerPackageConfigs::from_namespace().await.context("loading eager package config")?;
        Self::from_config(config, package_fetcher)
    }

    fn from_config(
        config: EagerPackageConfigs,
        package_fetcher: PackageFetcher,
    ) -> Result<Self, Error> {
        config
            .packages
            .into_iter()
            .map(|EagerPackageConfig { url, executable }| {
                if url.package_hash().is_none() {
                    Ok((url, EagerPackage { executable, package_directory: None }))
                } else {
                    Err(anyhow!("pinned url not allowed in eager package config: '{}'", url))
                }
            })
            .collect::<Result<_, _>>()
            .map(|packages| Self { packages, package_fetcher })
    }

    #[allow(dead_code)]
    pub fn is_eager_package(&self, url: &PkgUrl) -> bool {
        self.packages.contains_key(url)
    }

    async fn resolve_pinned(
        package_fetcher: &PackageFetcher,
        url: PinnedPkgUrl,
    ) -> Result<PackageDirectory, EagerPackageManagerError> {
        let expected_hash = url.package_hash();
        let pkg_dir = crate::resolver_service::resolve(package_fetcher, url.into()).await?;
        let hash = pkg_dir.merkle_root().await?;
        if hash != expected_hash {
            return Err(EagerPackageManagerError::HashMismatch(hash));
        }
        Ok(pkg_dir)
    }

    #[allow(dead_code)]
    async fn resolve_pinned_and_save_package_directory(
        &mut self,
        url: PinnedPkgUrl,
    ) -> Result<(), EagerPackageManagerError> {
        let unpinned_url = url.strip_hash();
        let package = self
            .packages
            .get_mut(&unpinned_url)
            .ok_or_else(|| EagerPackageManagerError::UnknownURL(unpinned_url))?;
        let pkg_dir = Self::resolve_pinned(&self.package_fetcher, url).await?;
        package.package_directory = Some(pkg_dir);
        Ok(())
    }
}

#[derive(Debug, thiserror::Error)]
enum EagerPackageManagerError {
    #[error("URL is not a known eager package: {0}")]
    UnknownURL(PkgUrl),
    #[error("while resolving package")]
    Resolve(#[from] fidl_fuchsia_pkg_ext::ResolveError),
    #[error("while reading package hash")]
    ReadHash(#[from] fuchsia_pkg::ReadHashError),
    #[error("resolved package hash '{0}' does not match")]
    HashMismatch(Hash),
}

#[derive(Debug, Deserialize, PartialEq, Eq)]
struct EagerPackageConfigs {
    packages: Vec<EagerPackageConfig>,
}

#[derive(Debug, Deserialize, PartialEq, Eq)]
struct EagerPackageConfig {
    url: PkgUrl,
    #[serde(default)]
    executable: bool,
}

impl EagerPackageConfigs {
    async fn from_namespace() -> Result<Self, Error> {
        let json = io_util::file::read_in_namespace(EAGER_PACKAGE_CONFIG_PATH)
            .await
            .context("reading eager package config file")?;
        serde_json::from_slice(&json).context("parsing eager package config")
    }
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches, fuchsia_async as fasync};

    fn get_test_package_fetcher() -> PackageFetcher {
        let pkg_dir = PackageDirectory::open_from_namespace().unwrap();
        PackageFetcher::new_mock(move |_url| {
            let pkg_dir = pkg_dir.clone();
            async move { Ok(pkg_dir) }
        })
    }

    #[test]
    fn parse_eager_package_configs_json() {
        let json = br#"
        {
            "packages":
            [
                {
                    "url": "fuchsia-pkg://example.com/package",
                    "executable": true
                },
                {
                    "url": "fuchsia-pkg://example.com/package2",
                    "executable": false
                },
                {
                    "url": "fuchsia-pkg://example.com/package3"
                }
            ]
        }"#;
        assert_eq!(
            serde_json::from_slice::<EagerPackageConfigs>(json).unwrap(),
            EagerPackageConfigs {
                packages: vec![
                    EagerPackageConfig {
                        url: PkgUrl::parse("fuchsia-pkg://example.com/package").unwrap(),
                        executable: true,
                    },
                    EagerPackageConfig {
                        url: PkgUrl::parse("fuchsia-pkg://example.com/package2").unwrap(),
                        executable: false,
                    },
                    EagerPackageConfig {
                        url: PkgUrl::parse("fuchsia-pkg://example.com/package3").unwrap(),
                        executable: false,
                    },
                ]
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_is_eager_package() {
        let config = EagerPackageConfigs {
            packages: vec![
                EagerPackageConfig {
                    url: PkgUrl::parse("fuchsia-pkg://example.com/package").unwrap(),
                    executable: true,
                },
                EagerPackageConfig {
                    url: PkgUrl::parse("fuchsia-pkg://example.com/package2").unwrap(),
                    executable: false,
                },
            ],
        };
        let package_fetcher = get_test_package_fetcher();
        let manager = EagerPackageManager::from_config(config, package_fetcher).unwrap();

        for url in ["fuchsia-pkg://example.com/package", "fuchsia-pkg://example.com/package2"] {
            assert!(manager.is_eager_package(&PkgUrl::parse(url).unwrap()));
        }
        for url in [
            "fuchsia-pkg://example2.com/package",
            "fuchsia-pkg://example.com/package3",
            "fuchsia-pkg://example.com/package?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
        ] {
            assert!(!manager.is_eager_package(&PkgUrl::parse(url).unwrap()));
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn reject_pinned_urls() {
        let config = EagerPackageConfigs {
            packages: vec![
                EagerPackageConfig {
                    url: PkgUrl::parse("fuchsia-pkg://example.com/package?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef").unwrap(),
                    executable: true,
                },
            ],
        };
        let package_fetcher = get_test_package_fetcher();
        assert_matches!(EagerPackageManager::from_config(config, package_fetcher), Err(_));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_pinned_and_save_package_directory() {
        let url = PkgUrl::parse("fuchsia-pkg://example.com/package").unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig { url: url.clone(), executable: true }],
        };

        let dir = tempfile::tempdir().unwrap();
        std::fs::write(
            dir.path().join("meta"),
            "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
        )
        .unwrap();
        let proxy = io_util::open_directory_in_namespace(
            dir.path().to_str().unwrap(),
            io_util::OPEN_RIGHT_READABLE,
        )
        .unwrap();
        let pkg_dir = PackageDirectory::from_proxy(proxy);
        let package_fetcher = PackageFetcher::new_mock(move |_url| {
            let pkg_dir = pkg_dir.clone();
            async move { Ok(pkg_dir) }
        });

        let mut manager = EagerPackageManager::from_config(config, package_fetcher).unwrap();
        manager
            .resolve_pinned_and_save_package_directory(PinnedPkgUrl::from_url_and_hash(
                url.clone(),
                "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef".parse().unwrap(),
            ))
            .await
            .unwrap();
        assert!(manager.packages[&url].package_directory.is_some());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_pinned_and_save_package_directory_hash_mismatch() {
        let url = PkgUrl::parse("fuchsia-pkg://example.com/package").unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig { url: url.clone(), executable: true }],
        };
        let package_fetcher = get_test_package_fetcher();
        let mut manager = EagerPackageManager::from_config(config, package_fetcher).unwrap();
        assert_matches!(
            manager
                .resolve_pinned_and_save_package_directory(PinnedPkgUrl::from_url_and_hash(
                    url,
                    "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef"
                        .parse()
                        .unwrap(),
                ))
                .await,
            Err(EagerPackageManagerError::HashMismatch(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_pinned_and_save_package_directory_unknown_url() {
        let url = PkgUrl::parse("fuchsia-pkg://example.com/package").unwrap();
        let url2 = PkgUrl::parse("fuchsia-pkg://example.com/package2").unwrap();
        let config =
            EagerPackageConfigs { packages: vec![EagerPackageConfig { url, executable: true }] };
        let package_fetcher = get_test_package_fetcher();
        let mut manager = EagerPackageManager::from_config(config, package_fetcher).unwrap();
        assert_matches!(
            manager
                .resolve_pinned_and_save_package_directory(PinnedPkgUrl::from_url_and_hash(
                    url2.clone(),
                    "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef".parse().unwrap(),
                ))
                .await,
            Err(EagerPackageManagerError::UnknownURL(url)) if url == url2);
        assert_matches!(manager.packages.get(&url2), None);
    }
}
