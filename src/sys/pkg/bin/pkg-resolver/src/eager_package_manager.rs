// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::resolver_service::PackageFetcher,
    anyhow::{anyhow, Context as _, Error},
    fidl_fuchsia_pkg::{
        CupData, CupRequest, CupRequestStream, GetInfoError, PackageUrl, WriteError,
    },
    fuchsia_pkg::PackageDirectory,
    fuchsia_syslog::fx_log_err,
    fuchsia_url::pkg_url::{Hash, PinnedPkgUrl, PkgUrl},
    futures::{lock::Mutex as AsyncMutex, prelude::*},
    omaha_client::protocol::response::Response,
    serde::Deserialize,
    std::{collections::BTreeMap, sync::Arc},
};

const EAGER_PACKAGE_CONFIG_PATH: &str = "/config/data/eager_package_config.json";

#[derive(Debug)]
struct EagerPackage {
    #[allow(dead_code)]
    executable: bool,
    package_directory: Option<PackageDirectory>,
    cup: Option<CupData>,
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
                    Ok((url, EagerPackage { executable, package_directory: None, cup: None }))
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
    ) -> Result<PackageDirectory, ResolvePinnedError> {
        let expected_hash = url.package_hash();
        let pkg_dir = crate::resolver_service::resolve(package_fetcher, url.into()).await?;
        let hash = pkg_dir.merkle_root().await?;
        if hash != expected_hash {
            return Err(ResolvePinnedError::HashMismatch(hash));
        }
        Ok(pkg_dir)
    }

    async fn cup_write(&mut self, url: &PackageUrl, cup: CupData) -> Result<(), CupWriteError> {
        // TODO(fxbug.dev/95296): verify CUP signature before parsing.
        let response = parse_omaha_response_from_cup(&cup)?;
        // The full URL must appear in the omaha response.
        let _app = response
            .apps
            .iter()
            .find(|app| {
                app.update_check
                    .as_ref()
                    .and_then(|uc| uc.get_all_full_urls().find(|u| u == &url.url))
                    .is_some()
            })
            .ok_or(CupWriteError::CupResponseURLNotFound)?;

        let pinned_url: PinnedPkgUrl = url.url.parse()?;
        let unpinned_url = pinned_url.strip_hash();
        // Make sure the url is an eager package before trying to resolve it.
        let package = self
            .packages
            .get_mut(&unpinned_url)
            .ok_or_else(|| CupWriteError::UnknownURL(unpinned_url))?;
        let pkg_dir = Self::resolve_pinned(&self.package_fetcher, pinned_url).await?;
        package.package_directory = Some(pkg_dir);
        package.cup = Some(cup);
        // TODO(fxbug.dev/95295): persist cup data.
        Ok(())
    }

    async fn cup_get_info(&self, url: &PackageUrl) -> Result<(String, String), CupGetInfoError> {
        let pkg_url = PkgUrl::parse(&url.url)?;
        let package =
            self.packages.get(&pkg_url).ok_or_else(|| CupGetInfoError::UnknownURL(pkg_url))?;
        let cup = package.cup.as_ref().ok_or(CupGetInfoError::CupDataNotAvailable)?;
        let response = parse_omaha_response_from_cup(&cup)?;
        let app = response
            .apps
            .iter()
            .find(|app| {
                app.update_check
                    .as_ref()
                    .and_then(|uc| uc.get_all_full_urls().find(|u| u.starts_with(&url.url)))
                    .is_some()
            })
            .ok_or(CupGetInfoError::CupResponseURLNotFound)?;
        let version = app.get_manifest_version().ok_or(CupGetInfoError::CupDataMissingVersion)?;
        let channel = app.cohort.name.clone().ok_or(CupGetInfoError::CupDataMissingChannel)?;
        Ok((version, channel))
    }
}

fn parse_omaha_response_from_cup(cup: &CupData) -> Result<Response, ParseCupResponseError> {
    Ok(omaha_client::protocol::response::parse_json_response(
        cup.response.as_ref().ok_or(ParseCupResponseError::CupDataNoResponse)?,
    )?)
}

#[derive(Debug, thiserror::Error)]
enum ParseCupResponseError {
    #[error("CUP data does not include a response")]
    CupDataNoResponse,
    #[error("while parsing JSON")]
    ParseJSON(#[from] serde_json::Error),
}

#[derive(Debug, thiserror::Error)]
enum ResolvePinnedError {
    #[error("while resolving package")]
    Resolve(#[from] fidl_fuchsia_pkg_ext::ResolveError),
    #[error("while reading package hash")]
    ReadHash(#[from] fuchsia_pkg::ReadHashError),
    #[error("resolved package hash '{0}' does not match")]
    HashMismatch(Hash),
}

#[derive(Debug, thiserror::Error)]
enum CupWriteError {
    #[error("while parsing package URL")]
    ParseURL(#[from] fuchsia_url::errors::ParseError),
    #[error("URL is not a known eager package: {0}")]
    UnknownURL(PkgUrl),
    #[error("while resolving package")]
    ResolvePinned(#[from] ResolvePinnedError),
    #[error("while parsing CUP omaha response")]
    ParseCupResponse(#[from] ParseCupResponseError),
    #[error("URL not found in CUP omaha response")]
    CupResponseURLNotFound,
}

impl From<&CupWriteError> for WriteError {
    fn from(err: &CupWriteError) -> WriteError {
        match err {
            CupWriteError::ParseURL(_) => WriteError::UnknownUrl,
            CupWriteError::UnknownURL(_) => WriteError::UnknownUrl,
            CupWriteError::ResolvePinned(_) => WriteError::Download,
            CupWriteError::ParseCupResponse(_) => WriteError::Verification,
            CupWriteError::CupResponseURLNotFound => WriteError::Verification,
        }
    }
}

#[derive(Debug, thiserror::Error)]
enum CupGetInfoError {
    #[error("while parsing package URL")]
    ParseURL(#[from] fuchsia_url::errors::ParseError),
    #[error("URL is not a known eager package: {0}")]
    UnknownURL(PkgUrl),
    #[error("CUP data is not available for this eager package")]
    CupDataNotAvailable,
    #[error("URL not found in CUP omaha response")]
    CupResponseURLNotFound,
    #[error("CUP data does not include version")]
    CupDataMissingVersion,
    #[error("CUP data does not include channel")]
    CupDataMissingChannel,
    #[error("while parsing CUP omaha response")]
    ParseCupResponse(#[from] ParseCupResponseError),
}

impl From<&CupGetInfoError> for GetInfoError {
    fn from(err: &CupGetInfoError) -> GetInfoError {
        match err {
            CupGetInfoError::ParseURL(_) => GetInfoError::UnknownUrl,
            CupGetInfoError::UnknownURL(_) => GetInfoError::UnknownUrl,
            CupGetInfoError::CupDataNotAvailable => GetInfoError::NotAvailable,
            CupGetInfoError::CupResponseURLNotFound => GetInfoError::Verification,
            CupGetInfoError::CupDataMissingVersion => GetInfoError::Verification,
            CupGetInfoError::CupDataMissingChannel => GetInfoError::Verification,
            CupGetInfoError::ParseCupResponse(_) => GetInfoError::Verification,
        }
    }
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

#[allow(dead_code)]
pub async fn run_cup_service(
    manager: Arc<AsyncMutex<EagerPackageManager>>,
    mut stream: CupRequestStream,
) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        match event {
            CupRequest::Write { url, cup, responder } => {
                let response = manager.lock().await.cup_write(&url, cup).await;
                responder.send(&mut response.map_err(|e| {
                    let write_error = (&e).into();
                    fx_log_err!("cup_write failed for url '{}': {:#}", url.url, anyhow!(e));
                    write_error
                }))?;
            }
            CupRequest::GetInfo { url, responder } => {
                let response = manager.lock().await.cup_get_info(&url).await;
                responder.send(&mut response.map_err(|e| {
                    let get_info_error = (&e).into();
                    fx_log_err!("cup_get_info failed for url '{}': {:#}", url.url, anyhow!(e));
                    get_info_error
                }))?;
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches, fuchsia_async as fasync};

    const TEST_URL: &str = "fuchsia-pkg://example.com/package";
    const TEST_HASH: &str = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    const TEST_PINNED_URL: &str = "fuchsia-pkg://example.com/package?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

    fn get_test_package_fetcher() -> PackageFetcher {
        let pkg_dir = PackageDirectory::open_from_namespace().unwrap();
        PackageFetcher::new_mock(move |_url| {
            let pkg_dir = pkg_dir.clone();
            async move { Ok(pkg_dir) }
        })
    }

    fn get_test_package_fetcher_with_hash(hash: &str) -> (PackageFetcher, tempfile::TempDir) {
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(dir.path().join("meta"), hash).unwrap();
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
        (package_fetcher, dir)
    }

    fn get_test_cup_data() -> CupData {
        let response = serde_json::json!({"response":{
          "server": "prod",
          "protocol": "3.0",
          "app": [{
            "appid": "appid",
            "cohortname": "stable",
            "status": "ok",
            "updatecheck": {
              "status": "ok",
              "urls":{
                "url":[
                  {"codebase": "fuchsia-pkg://example.com/"},
                ]
              },
              "manifest": {
                "version": "1.2.3.4",
                "actions": {
                  "action": [],
                },
                "packages": {
                  "package": [
                    {
                     "name": "package?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                     "required": true,
                     "fp": "",
                    }
                  ],
                },
              }
            }
          }],
        }});
        let response = serde_json::to_vec(&response).unwrap();
        CupData { response: Some(response), ..CupData::EMPTY }
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
    async fn config_reject_pinned_urls() {
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig {
                url: PkgUrl::parse(TEST_PINNED_URL).unwrap(),
                executable: true,
            }],
        };
        let package_fetcher = get_test_package_fetcher();
        assert_matches!(EagerPackageManager::from_config(config, package_fetcher), Err(_));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resolve_pinned_hash_mismatch() {
        let pinned_url =
            PinnedPkgUrl::from_url_and_hash(TEST_URL.parse().unwrap(), TEST_HASH.parse().unwrap());
        assert_matches!(
            EagerPackageManager::resolve_pinned(&get_test_package_fetcher(), pinned_url).await,
            Err(ResolvePinnedError::HashMismatch(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_write() {
        let url = PkgUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig { url: url.clone(), executable: true }],
        };
        let (package_fetcher, _test_dir) = get_test_package_fetcher_with_hash(TEST_HASH);

        let mut manager = EagerPackageManager::from_config(config, package_fetcher).unwrap();
        let cup = get_test_cup_data();
        manager.cup_write(&PackageUrl { url: TEST_PINNED_URL.into() }, cup.clone()).await.unwrap();
        assert!(manager.packages[&url].package_directory.is_some());
        assert_eq!(manager.packages[&url].cup, Some(cup));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_write_omaha_response_different_url() {
        let url = PkgUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig { url: url.clone(), executable: true }],
        };
        let (package_fetcher, _test_dir) = get_test_package_fetcher_with_hash(TEST_HASH);

        let mut manager = EagerPackageManager::from_config(config, package_fetcher).unwrap();
        let cup = get_test_cup_data();
        assert_matches!(
            manager
                .cup_write(&PackageUrl { url: format!("{url}?hash=beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead") }, cup)
                .await,
            Err(CupWriteError::CupResponseURLNotFound)
        );
        assert!(manager.packages[&url].package_directory.is_none());
        assert!(manager.packages[&url].cup.is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_write_unknown_url() {
        let url = PkgUrl::parse("fuchsia-pkg://example.com/package2").unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig { url: url.clone(), executable: true }],
        };
        let package_fetcher = get_test_package_fetcher();

        let mut manager = EagerPackageManager::from_config(config, package_fetcher).unwrap();
        let cup = get_test_cup_data();
        assert_matches!(
            manager.cup_write(&PackageUrl { url: TEST_PINNED_URL.into() }, cup).await,
            Err(CupWriteError::UnknownURL(_))
        );
        assert!(manager.packages[&url].package_directory.is_none());
        assert!(manager.packages[&url].cup.is_none());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_get_info() {
        let url = PkgUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig { url: url.clone(), executable: true }],
        };
        let package_fetcher = get_test_package_fetcher();

        let mut manager = EagerPackageManager::from_config(config, package_fetcher).unwrap();
        manager.packages.get_mut(&url).unwrap().cup = Some(get_test_cup_data());
        let (version, channel) =
            manager.cup_get_info(&PackageUrl { url: TEST_URL.into() }).await.unwrap();
        assert_eq!(version, "1.2.3.4");
        assert_eq!(channel, "stable");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_get_info_not_available() {
        let url = PkgUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig { url: url.clone(), executable: true }],
        };
        let package_fetcher = get_test_package_fetcher();

        let manager = EagerPackageManager::from_config(config, package_fetcher).unwrap();
        assert_matches!(
            manager.cup_get_info(&PackageUrl { url: TEST_URL.into() }).await,
            Err(CupGetInfoError::CupDataNotAvailable)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_get_info_unknown_url() {
        let url = PkgUrl::parse(TEST_URL).unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig { url: url.clone(), executable: true }],
        };
        let package_fetcher = get_test_package_fetcher();

        let mut manager = EagerPackageManager::from_config(config, package_fetcher).unwrap();
        manager.packages.get_mut(&url).unwrap().cup = Some(get_test_cup_data());
        assert_matches!(
            manager
                .cup_get_info(&PackageUrl { url: "fuchsia-pkg://example.com/package2".into() })
                .await,
            Err(CupGetInfoError::UnknownURL(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cup_get_info_omaha_response_different_url() {
        let url = PkgUrl::parse("fuchsia-pkg://example.com/package2").unwrap();
        let config = EagerPackageConfigs {
            packages: vec![EagerPackageConfig { url: url.clone(), executable: true }],
        };
        let package_fetcher = get_test_package_fetcher();

        let mut manager = EagerPackageManager::from_config(config, package_fetcher).unwrap();
        manager.packages.get_mut(&url).unwrap().cup = Some(get_test_cup_data());
        assert_matches!(
            manager
                .cup_get_info(&PackageUrl { url: "fuchsia-pkg://example.com/package2".into() })
                .await,
            Err(CupGetInfoError::CupResponseURLNotFound)
        );
    }
}
