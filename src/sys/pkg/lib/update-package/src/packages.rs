// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io::DirectoryProxy,
    fuchsia_url::pkg_url::PkgUrl,
    serde::{Deserialize, Serialize},
    thiserror::Error,
};

// In order to support packages.json with version as a string or int, we define a
// custom deserializer to parse a value as an int or string.
//
// TODO(fxbug.dev/50754) Remove this once we remove support for version as an int.
fn deserialize_string_or_int<'de, D>(deserializer: D) -> Result<String, D::Error>
where
    D: serde::de::Deserializer<'de>,
{
    struct MyVisitor;

    impl<'de> serde::de::Visitor<'de> for MyVisitor {
        type Value = String;

        fn expecting(&self, fmt: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            fmt.write_str("integer or string")
        }

        fn visit_u64<E>(self, val: u64) -> Result<Self::Value, E>
        where
            E: serde::de::Error,
        {
            self.visit_string(val.to_string())
        }

        fn visit_str<E>(self, val: &str) -> Result<Self::Value, E>
        where
            E: serde::de::Error,
        {
            self.visit_string(val.to_string())
        }

        fn visit_string<E>(self, val: String) -> Result<Self::Value, E>
        where
            E: serde::de::Error,
        {
            Ok(val)
        }
    }

    deserializer.deserialize_any(MyVisitor)
}

// While traditionally this should be represented as an enum, instead we
// use a struct so that we can use a custom deserializer for version.
// We enforce that version must be 1 in the parse_packages_json fn.
//
// TODO(fxbug.dev/50754) Once we remove support for version as an int, we can replace
// this with something like:
// #[serde(tag = "version", content = "content", deny_unknown_fields)]
// enum Packages {
//     #[serde(rename = "1")]
//     V1(Vec<PkgUrl>),
// }
#[derive(Serialize, Deserialize, Debug)]
#[serde(deny_unknown_fields)]
struct Packages {
    #[serde(deserialize_with = "deserialize_string_or_int")]
    version: String,
    content: Vec<PkgUrl>,
}

/// ParsePackageError represents any error which might occur while reading
/// `packages.json` from an update package.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum ParsePackageError {
    #[error("could not open `packages.json`")]
    FailedToOpen(#[source] io_util::node::OpenError),

    #[error("could not parse url from line: {0:?}")]
    URLParseError(String, #[source] fuchsia_url::errors::ParseError),

    #[error("error reading file `packages.json`")]
    ReadError(#[source] io_util::file::ReadError),

    #[error("json parsing error while reading `packages.json`")]
    JsonError(#[source] serde_json::error::Error),

    // TODO(fxbug.dev/50754) Remove this error once we remove support for version as an int.
    // At that point, unsupported versions will surface as Json errors.
    #[error("packages.json version not supported: '{0}'")]
    VersionNotSupported(String),
}

pub(crate) fn parse_packages_json(contents: &str) -> Result<Vec<PkgUrl>, ParsePackageError> {
    match serde_json::from_str(&contents).map_err(ParsePackageError::JsonError)? {
        Packages { ref version, content } if version == "1" => Ok(content),
        Packages { version, .. } => Err(ParsePackageError::VersionNotSupported(version)),
    }
}

/// Returns the list of package urls that go in the universe of this update package.
pub(crate) async fn packages(proxy: &DirectoryProxy) -> Result<Vec<PkgUrl>, ParsePackageError> {
    let file = io_util::directory::open_file(
        &proxy,
        "packages.json",
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
    )
    .await
    .map_err(ParsePackageError::FailedToOpen)?;

    let contents =
        io_util::file::read_to_string(&file).await.map_err(|e| ParsePackageError::ReadError(e))?;
    parse_packages_json(&contents)
}

#[cfg(test)]
mod tests {
    use {super::*, crate::TestUpdatePackage, matches::assert_matches, serde_json::json};

    fn pkg_urls(v: Vec<&str>) -> Vec<PkgUrl> {
        v.into_iter().map(|s| PkgUrl::parse(s).unwrap()).collect()
    }

    // TODO(fxbug.dev/50754) Use the new Packages implementation, which only supports version as a string.
    #[test]
    fn smoke_test_parse_packages_json() {
        let packages = Packages {
            version: "1".to_string(),
            content: pkg_urls(vec![
            "fuchsia-pkg://fuchsia.com/ls/0?hash=71bad1a35b87a073f72f582065f6b6efec7b6a4a129868f37f6131f02107f1ea",
            "fuchsia-pkg://fuchsia.com/pkg-resolver/0?hash=26d43a3fc32eaa65e6981791874b6ab80fae31fbfca1ce8c31ab64275fd4e8c0",
        ])
        };
        let packages_json = serde_json::to_string(&packages).unwrap();
        assert_eq!(parse_packages_json(&packages_json).unwrap(), packages.content);
    }

    #[test]
    fn expect_failure_parse_packages_json() {
        assert_matches!(parse_packages_json("{}"), Err(ParsePackageError::JsonError(_)));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn smoke_test_packages_json_version_string() {
        let update_pkg = TestUpdatePackage::new();
        let pkg_list = vec![
            "fuchsia-pkg://fuchsia.com/ls/0?hash=71bad1a35b87a073f72f582065f6b6efec7b6a4a129868f37f6131f02107f1ea",
            "fuchsia-pkg://fuchsia.com/pkg-resolver/0?hash=26d43a3fc32eaa65e6981791874b6ab80fae31fbfca1ce8c31ab64275fd4e8c0",
        ];
        let packages = json!({
            "version": "1",
            "content": pkg_list,
        })
        .to_string();
        let update_pkg = update_pkg.add_file("packages.json", packages).await;
        assert_eq!(update_pkg.packages().await.unwrap(), pkg_urls(pkg_list));
    }

    // TODO(fxbug.dev/50754) Remove once we remove support for version as an int.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn smoke_test_packages_json_version_int() {
        let update_pkg = TestUpdatePackage::new();
        let pkg_list = vec![
            "fuchsia-pkg://fuchsia.com/ls/0?hash=71bad1a35b87a073f72f582065f6b6efec7b6a4a129868f37f6131f02107f1ea",
            "fuchsia-pkg://fuchsia.com/pkg-resolver/0?hash=26d43a3fc32eaa65e6981791874b6ab80fae31fbfca1ce8c31ab64275fd4e8c0",
        ];
        let packages = json!({
            "version": 1,
            "content": pkg_list,
        })
        .to_string();
        let update_pkg = update_pkg.add_file("packages.json", packages).await;
        assert_eq!(update_pkg.packages().await.unwrap(), pkg_urls(pkg_list));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn expect_failure_json() {
        let update_pkg = TestUpdatePackage::new();
        let packages = "{}";
        let update_pkg = update_pkg.add_file("packages.json", packages).await;
        assert_matches!(update_pkg.packages().await, Err(ParsePackageError::JsonError(_)))
    }

    // TODO(fxbug.dev/50754) Once we remove support for packages.json as an int, this should
    // instead surface as a Json error (rather than a VersionNotSupported error).
    #[fuchsia_async::run_singlethreaded(test)]
    async fn expect_failure_version_not_supported() {
        let update_pkg = TestUpdatePackage::new();
        let pkg_list = vec![
            "fuchsia-pkg://fuchsia.com/ls/0?hash=71bad1a35b87a073f72f582065f6b6efec7b6a4a129868f37f6131f02107f1ea",
        ];
        let packages = json!({
            "version": "2",
            "content": pkg_list,
        })
        .to_string();
        let update_pkg = update_pkg.add_file("packages.json", packages).await;
        assert_matches!(update_pkg.packages().await, Err(ParsePackageError::VersionNotSupported(_)))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn expect_failure_no_files() {
        let update_pkg = TestUpdatePackage::new();
        assert_matches!(update_pkg.packages().await, Err(ParsePackageError::FailedToOpen(_)))
    }
}
