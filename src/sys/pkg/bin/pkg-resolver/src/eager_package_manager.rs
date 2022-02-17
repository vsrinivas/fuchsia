// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Error},
    fuchsia_url::pkg_url::PkgUrl,
    serde::Deserialize,
    std::collections::BTreeMap,
};

const EAGER_PACKAGE_CONFIG_PATH: &str = "/config/data/eager_package_config.json";

#[derive(Debug)]
struct EagerPackage {
    #[allow(dead_code)]
    executable: bool,
}

#[derive(Debug)]
pub struct EagerPackageManager {
    packages: BTreeMap<PkgUrl, EagerPackage>,
}

impl EagerPackageManager {
    #[allow(dead_code)]
    pub async fn from_namespace() -> Result<Self, Error> {
        let config =
            EagerPackageConfigs::from_namespace().await.context("loading eager package config")?;
        Self::from_config(config)
    }

    fn from_config(config: EagerPackageConfigs) -> Result<Self, Error> {
        config
            .packages
            .into_iter()
            .map(|EagerPackageConfig { url, executable }| {
                if url.package_hash().is_none() {
                    Ok((url, EagerPackage { executable }))
                } else {
                    Err(anyhow!("pinned url not allowed in eager package config: '{}'", url))
                }
            })
            .collect::<Result<_, _>>()
            .map(|packages| Self { packages })
    }

    #[allow(dead_code)]
    pub fn is_eager_package(&self, url: &PkgUrl) -> bool {
        self.packages.contains_key(url)
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

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches};

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

    #[test]
    fn test_is_eager_package() {
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
        let manager = EagerPackageManager::from_config(config).unwrap();

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

    #[test]
    fn reject_pinned_urls() {
        let config = EagerPackageConfigs {
            packages: vec![
                EagerPackageConfig {
                    url: PkgUrl::parse("fuchsia-pkg://example.com/package?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef").unwrap(),
                    executable: true,
                },
            ],
        };
        assert_matches!(EagerPackageManager::from_config(config), Err(_));
    }
}
