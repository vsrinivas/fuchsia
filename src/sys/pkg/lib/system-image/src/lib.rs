// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

mod cache_packages;
mod errors;
mod non_static_allowlist;
mod path_hash_mapping;
mod system_image;

pub use crate::{
    cache_packages::CachePackages,
    errors::{
        AllowListError, CachePackagesInitError, PathHashMappingError, StaticPackagesInitError,
    },
    non_static_allowlist::NonStaticAllowList,
    path_hash_mapping::{Bootfs, PathHashMapping, StaticPackages},
    system_image::{ExecutabilityRestrictions, SystemImage},
};

static PKGFS_BOOT_ARG_KEY: &str = "zircon.system.pkgfs.cmd";
static PKGFS_BOOT_ARG_VALUE_PREFIX: &str = "bin/pkgsvr+";

pub async fn get_system_image_hash(
    args: &fidl_fuchsia_boot::ArgumentsProxy,
) -> Result<fuchsia_hash::Hash, SystemImageHashError> {
    let hash = args
        .get_string(PKGFS_BOOT_ARG_KEY)
        .await
        .map_err(SystemImageHashError::Fidl)?
        .ok_or(SystemImageHashError::MissingValue)?;
    let hash = hash
        .strip_prefix(PKGFS_BOOT_ARG_VALUE_PREFIX)
        .ok_or_else(|| SystemImageHashError::BadPrefix(hash.clone()))?;
    hash.parse().map_err(SystemImageHashError::BadHash)
}

#[derive(Debug, thiserror::Error)]
pub enum SystemImageHashError {
    #[error("fidl error calling fuchsia.boot/Arguments.GetString")]
    Fidl(#[source] fidl::Error),

    #[error("boot args have no value for key {}", PKGFS_BOOT_ARG_KEY)]
    MissingValue,

    #[error(
        "boot arg for key {} does not start with {}: {0:?}",
        PKGFS_BOOT_ARG_KEY,
        PKGFS_BOOT_ARG_VALUE_PREFIX
    )]
    BadPrefix(String),

    #[error("boot arg for key {} has invalid hash {0:?}", PKGFS_BOOT_ARG_KEY)]
    BadHash(#[source] fuchsia_hash::ParseHashError),
}

#[cfg(test)]
mod test_get_system_image_hash {
    use {
        super::*, assert_matches::assert_matches, fuchsia_async as fasync,
        mock_boot_arguments::MockBootArgumentsService, std::collections::HashMap, std::sync::Arc,
    };

    #[fasync::run_singlethreaded(test)]
    async fn missing_value() {
        let mock =
            MockBootArgumentsService::new(HashMap::from([(PKGFS_BOOT_ARG_KEY.to_string(), None)]));
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_boot::ArgumentsMarker>()
                .unwrap();
        fasync::Task::spawn(Arc::new(mock).handle_request_stream(stream)).detach();

        assert_matches!(
            get_system_image_hash(&proxy).await,
            Err(SystemImageHashError::MissingValue)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn bad_prefix() {
        let mock = MockBootArgumentsService::new(HashMap::from([(
            PKGFS_BOOT_ARG_KEY.to_string(),
            Some("bad-prefix".to_string()),
        )]));
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_boot::ArgumentsMarker>()
                .unwrap();
        fasync::Task::spawn(Arc::new(mock).handle_request_stream(stream)).detach();

        assert_matches!(
            get_system_image_hash(&proxy).await,
            Err(SystemImageHashError::BadPrefix(prefix)) if prefix == "bad-prefix"
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn bad_hash() {
        let mock = MockBootArgumentsService::new(HashMap::from([(
            PKGFS_BOOT_ARG_KEY.to_string(),
            Some("bin/pkgsvr+bad-hash".to_string()),
        )]));
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_boot::ArgumentsMarker>()
                .unwrap();
        fasync::Task::spawn(Arc::new(mock).handle_request_stream(stream)).detach();

        assert_matches!(get_system_image_hash(&proxy).await, Err(SystemImageHashError::BadHash(_)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn success() {
        let mock = MockBootArgumentsService::new(HashMap::from([(
            PKGFS_BOOT_ARG_KEY.to_string(),
            Some(
                "bin/pkgsvr+0000000000000000000000000000000000000000000000000000000000000000"
                    .to_string(),
            ),
        )]));
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_boot::ArgumentsMarker>()
                .unwrap();
        fasync::Task::spawn(Arc::new(mock).handle_request_stream(stream)).detach();

        assert_eq!(get_system_image_hash(&proxy).await.unwrap(), fuchsia_hash::Hash::from([0; 32]));
    }
}
