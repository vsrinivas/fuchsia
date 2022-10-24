// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_boot::{ArgumentsMarker, BoolPair},
    fs_management::{BlobCompression, BlobEvictionPolicy},
    fuchsia_component::client::connect_to_protocol,
};

#[derive(Default)]
pub struct BootArgs {
    netsvc_netboot: bool,
    zircon_system_filesystem_check: bool,
    zircon_system_disable_automount: bool,
    blobfs_write_compression_algorithm: Option<String>,
    blobfs_eviction_policy: Option<String>,
}

impl BootArgs {
    pub async fn new() -> Result<Self, Error> {
        let arguments_proxy = connect_to_protocol::<ArgumentsMarker>()
            .context("Failed to connect to Arguments protocol")?;
        
        let mut defaults = vec![
            BoolPair { key: "netsvc.netboot".to_string(), defaultval: false },
            BoolPair { key: "zircon.system.filesystem-check".to_string(), defaultval: false },
            BoolPair { key: "zircon.system.disable-automount".to_string(), defaultval: false },
        ];
        let ret = arguments_proxy
            .get_bools(&mut defaults.iter_mut())
            .await
            .context("get_bools failed")?;
        let netsvc_netboot = ret[0];
        let zircon_system_filesystem_check = ret[1];
        let zircon_system_disable_automount = ret[2];

        let blobfs_write_compression_algorithm = arguments_proxy
            .get_string("blobfs.write-compression-algorithm")
            .await
            .context("Failed to get blobfs write compression algorithm")?;

        let blobfs_eviction_policy = arguments_proxy
            .get_string("blobfs.cache-eviction-policy")
            .await
            .context("Failed to get blobfs cache eviction policy")?;

        Ok(BootArgs {
            netsvc_netboot,
            zircon_system_filesystem_check,
            zircon_system_disable_automount,
            blobfs_write_compression_algorithm,
            blobfs_eviction_policy,
        })
    }

    pub fn netboot(&self) -> bool {
        self.netsvc_netboot || self.zircon_system_disable_automount
    }

    pub fn check_filesystems(&self) -> bool {
        self.zircon_system_filesystem_check
    }

    pub fn blobfs_write_compression_algorithm(&self) -> Option<BlobCompression> {
        match self.blobfs_write_compression_algorithm.as_deref() {
            Some("ZSTD_CHUNKED") => Some(BlobCompression::ZSTDChunked),
            Some("UNCOMPRESSED") => Some(BlobCompression::Uncompressed),
            _ => None,
        }
    }

    pub fn blobfs_eviction_policy(&self) -> Option<BlobEvictionPolicy> {
        match self.blobfs_eviction_policy.as_deref() {
            Some("NEVER_EVICT") => Some(BlobEvictionPolicy::NeverEvict),
            Some("EVICT_IMMEDIATELY") => Some(BlobEvictionPolicy::EvictImmediately),
            _ => None,
        }
    }
}
