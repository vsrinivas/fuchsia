// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_developer_bridge::VersionInfo;

#[cfg(not(test))]
const VERSION_INFO: &str = std::include_str!(std::env!("FFX_VERSION_INFO"));
#[cfg(not(test))]
const BUILD_VERSION: &str = std::include_str!(std::env!("BUILD_VERSION"));

#[cfg(not(test))]
pub fn build_info() -> VersionInfo {
    build_info_impl(VERSION_INFO.to_string(), BUILD_VERSION.to_string())
}

#[cfg(test)]
pub fn build_info() -> VersionInfo {
    panic!("build_info should not be called from a test environment");
}

fn build_info_impl(raw_version_info: String, raw_build_version: String) -> VersionInfo {
    let split: Vec<&str> = raw_version_info.trim().split("-").collect();
    if split.len() != 2 {
        return VersionInfo {
            build_version: Some(raw_build_version.trim().to_string()),
            ..VersionInfo::EMPTY
        };
    }

    let raw_hash = split.get(0).unwrap().to_string();
    let hash_opt = if raw_hash.is_empty() { None } else { Some(raw_hash) };
    let timestamp_str = split.get(1).unwrap();
    let timestamp = timestamp_str.parse::<u64>().ok();

    return VersionInfo {
        commit_hash: hash_opt,
        commit_timestamp: timestamp,
        build_version: Some(raw_build_version.trim().to_string()),
        ..VersionInfo::EMPTY
    };
}

#[cfg(test)]
mod test {
    use super::*;

    const HASH: &str = "hashyhashhash";
    const TIMESTAMP: u64 = 12345689;
    const FAKE_BUILD_VERSION: &str = "20201118";

    #[test]
    fn test_valid_string_dirty() {
        let s = format!("{}-{}", HASH, TIMESTAMP);
        let result = build_info_impl(s, FAKE_BUILD_VERSION.to_string());

        assert_eq!(
            result,
            VersionInfo {
                commit_hash: Some(HASH.to_string()),
                commit_timestamp: Some(TIMESTAMP),
                build_version: Some(FAKE_BUILD_VERSION.to_string()),
                ..VersionInfo::EMPTY
            }
        );
    }

    #[test]
    fn test_valid_string_clean() {
        let s = format!("{}-{}", HASH, TIMESTAMP);
        let result = build_info_impl(s, FAKE_BUILD_VERSION.to_string());

        assert_eq!(
            result,
            VersionInfo {
                commit_hash: Some(HASH.to_string()),
                commit_timestamp: Some(TIMESTAMP),
                build_version: Some(FAKE_BUILD_VERSION.to_string()),
                ..VersionInfo::EMPTY
            }
        );
    }

    #[test]
    fn test_invalid_string_empty() {
        let result = build_info_impl(String::default(), FAKE_BUILD_VERSION.to_string());

        assert_eq!(
            result,
            VersionInfo {
                commit_hash: None,
                commit_timestamp: None,
                build_version: Some(FAKE_BUILD_VERSION.to_string()),
                ..VersionInfo::EMPTY
            }
        );
    }

    #[test]
    fn test_invalid_string_empty_with_hyphens() {
        let result = build_info_impl("--".to_string(), FAKE_BUILD_VERSION.to_string());

        assert_eq!(
            result,
            VersionInfo {
                commit_hash: None,
                commit_timestamp: None,
                build_version: Some(FAKE_BUILD_VERSION.to_string()),
                ..VersionInfo::EMPTY
            }
        );
    }

    #[test]
    fn test_invalid_string_clean_missing_hash() {
        let result = build_info_impl(format!("-{}", TIMESTAMP), FAKE_BUILD_VERSION.to_string());

        assert_eq!(
            result,
            VersionInfo {
                commit_hash: None,
                commit_timestamp: Some(TIMESTAMP),
                build_version: Some(FAKE_BUILD_VERSION.to_string()),
                ..VersionInfo::EMPTY
            }
        );
    }

    #[test]
    fn test_invalid_string_clean_missing_hash_and_timestamp() {
        let result = build_info_impl("--".to_string(), FAKE_BUILD_VERSION.to_string());

        assert_eq!(
            result,
            VersionInfo {
                commit_hash: None,
                commit_timestamp: None,
                build_version: Some(FAKE_BUILD_VERSION.to_string()),
                ..VersionInfo::EMPTY
            }
        );
    }
}
