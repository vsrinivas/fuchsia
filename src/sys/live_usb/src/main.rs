// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod block_wrapper;
mod fvm;
mod gpt;
mod pkg;

use {
    crate::fvm::{get_partition_size, FvmRamdisk},
    anyhow::{Context, Error},
    fidl_fuchsia_boot::{ArgumentsMarker, BoolPair},
    fidl_fuchsia_hardware_block_partition::PartitionProxy,
    fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_watch::PathEvent,
    fuchsia_zircon as zx,
    futures::prelude::*,
    std::path::PathBuf,
};

/// This GUID is used by the installer to identify partitions that contain
/// data that will be installed to disk. The `fx mkinstaller` tool generates
/// images containing partitions with this GUID.
static WORKSTATION_INSTALLER_GPT: [u8; 16] = [
    0xce, 0x98, 0xce, 0x4d, 0x7e, 0xe7, 0xc1, 0x45, 0xa8, 0x63, 0xca, 0xf9, 0x2f, 0x13, 0x30, 0xc1,
];

async fn is_live_usb_enabled() -> Result<bool, Error> {
    let proxy = fuchsia_component::client::connect_to_protocol::<ArgumentsMarker>()
        .context("Connecting to service")?;

    let mut bools: [BoolPair; 2] = [
        BoolPair { key: "boot.usb".to_owned(), defaultval: false },
        BoolPair { key: "live_usb.is_system".to_owned(), defaultval: false },
    ];
    let result: Vec<bool> =
        proxy.get_bools(&mut bools.iter_mut()).await.context("Getting boot.usb bool value")?;

    Ok(result.iter().all(|f| *f))
}

/// This function is intended for use as an argument to skip_while().
async fn is_not_sparse_fvm(devfs_root: &zx::Channel, path: &PathBuf) -> Result<bool, Error> {
    let (local, remote) = zx::Channel::create()?;
    fdio::service_connect_at(&devfs_root, path.file_name().unwrap().to_str().unwrap(), remote)?;

    let proxy = PartitionProxy::new(fidl::AsyncChannel::from_channel(local)?);
    let (status, guid) = proxy.get_type_guid().await?;
    zx::Status::ok(status).context("getting partition type")?;
    let guid = guid.ok_or(anyhow::anyhow!("no guid!"))?;

    if guid.value == WORKSTATION_INSTALLER_GPT {
        Ok(false)
    } else {
        Ok(true)
    }
}

/// Waits for a sparse FVM to appear.
/// Returns the path to the partition with the sparse FVM once one is found.
async fn wait_for_sparse_fvm() -> Result<String, Error> {
    let stream =
        fuchsia_watch::watch("/dev/class/block").await.context("Starting block watcher")?;
    let (root, remote) = zx::Channel::create()?;
    fdio::service_connect("/dev/class/block", remote)?;
    let root_ref = &root;
    let event = Box::pin(stream.skip_while(|e| {
        // "name" is a full path, like /dev/class/block/000
        let path_to_check = match e {
            PathEvent::Added(name, _) => Some(name),
            PathEvent::Existing(name, _) => Some(name),
            _ => None,
        }
        .map(|v| v.to_path_buf());
        async move {
            match path_to_check {
                Some(path_to_check) => {
                    is_not_sparse_fvm(root_ref, &path_to_check).await.unwrap_or(true)
                }
                None => true,
            }
        }
    }))
    .next()
    .await
    .ok_or(anyhow::anyhow!("didn't get an event while watching for block device"))?;
    let name = match event {
        PathEvent::Added(name, _) => name,
        PathEvent::Existing(name, _) => name,
        // skip_while() above should only return for PathEvent::Added or PathEvent::Existing.
        _ => unreachable!(),
    };

    Ok(name.to_str().ok_or(anyhow::anyhow!("Invalid unicode in path"))?.to_owned())
}

async fn inner_main() -> Result<(), Error> {
    // Find the sparse FVM partition.
    let sparse_fvm_partition = wait_for_sparse_fvm().await?;
    fx_log_info!("using {} as sparse partition", sparse_fvm_partition);

    // Cap the ramdisk at 1/4 of system ram, unless that's not big enough for the FVM, a misc
    // partition and FVM_MINIMUM_PADDING bytes of extra room.
    let physmem = zx::system_get_physmem();
    let ramdisk_size = std::cmp::max(
        physmem / 4,
        get_partition_size(&sparse_fvm_partition).await.context("getting FVM size")? as u64
            + gpt::MISC_SIZE
            + gpt::FVM_MINIMUM_PADDING,
    );

    fx_log_info!("using {} bytes for ramdisk", ramdisk_size);
    let ramdisk = FvmRamdisk::new(ramdisk_size, sparse_fvm_partition)
        .await
        .context("creating FVM ramdisk")?;
    // Write FVM to the ramdisk.
    ramdisk.pave_fvm().await.context("paving FVM")?;

    pkg::disable_updates().await.context("disabling updates")?;

    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init_with_tags(&["live_usb.cm"]).expect("logging init");

    let enable_live_usb = match is_live_usb_enabled().await {
        Ok(val) => val,
        Err(e) => {
            fx_log_err!("Failed to check boot arguments: {:?}", e);
            false
        }
    };

    if !enable_live_usb {
        fx_log_info!("Not booting from a USB!");
        return;
    }

    fx_log_info!("Doing live USB boot!");
    let error = inner_main().await;
    if let Err(e) = error {
        fx_log_err!("Failed to do USB boot: {:?}", e);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::block_wrapper::WrappedBlockDevice,
        ::gpt::{
            disk::LogicalBlockSize,
            partition_types::{OperatingSystem, Type},
            GptConfig,
        },
        fidl_fuchsia_device::ControllerProxy,
        fidl_fuchsia_hardware_block_partition::PartitionProxy,
        fuchsia_async as fasync,
        ramdevice_client::{RamdiskClient, RamdiskClientBuilder},
        std::{collections::BTreeMap, fs::File},
    };

    async fn create_ramdisk_with_partitions(uuids: Vec<&'static str>) -> RamdiskClient {
        // 16MB
        let ramdisk_size: u64 = 16 * 1024 * 1024;

        let mut builder = RamdiskClientBuilder::new(512, ramdisk_size / 512);
        isolated_driver_manager::launch_isolated_driver_manager()
            .expect("launching isolated driver manager succeeds");
        ramdevice_client::wait_for_device(
            "/dev/sys/platform/00:00:2d/ramctl",
            std::time::Duration::from_secs(10),
        )
        .expect("ramctl appears");
        let ramdisk = builder.build().expect("creating ramdisk succeeds");
        let channel = ramdisk.open().expect("opening ramdisk succeeds");

        let file: File = fdio::create_fd(channel.into()).expect("creating file OK");
        let wrapper = Box::new(WrappedBlockDevice::new(file, 512));
        let mut disk = GptConfig::new()
            .writable(true)
            .initialized(false)
            .logical_block_size(LogicalBlockSize::Lb512)
            .create_from_device(wrapper, None)
            .expect("create gpt succeeds");

        disk.update_partitions(BTreeMap::new()).expect("init disk ok");

        for (i, uuid) in uuids.into_iter().enumerate() {
            let partition_type = Type { guid: uuid, os: OperatingSystem::None };

            disk.add_partition(&format!("part{}", i), 1024, partition_type, 0)
                .expect("adding partition succeeds");
        }

        disk.write().expect("writing GPT succeeds");

        let channel = ramdisk.open().expect("opening ramdisk OK");
        let controller = ControllerProxy::new(fidl::AsyncChannel::from_channel(channel).unwrap());
        controller
            .rebind("gpt.so")
            .await
            .expect("rebind request OK")
            .map_err(zx::Status::from_raw)
            .expect("rebind OK");
        ramdisk
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_wait_for_sparse_fvm() {
        let uuids = vec![
            "1d75395d-f2c6-476b-a8b7-45cc1c97b476", // MISC - 001
            "41d0e340-57e3-954e-8c1e-17ecac44cff5", // Real FVM - 002
            "4dce98ce-e77e-45c1-a863-caf92f1330c1", // sparse FVM - 003
            "00000000-0000-0000-0000-000000000000", // empty guid - 004
        ];

        let _disk = create_ramdisk_with_partitions(uuids).await;
        let path = wait_for_sparse_fvm().await.expect("found FVM");

        let (local, remote) = zx::Channel::create().unwrap();
        // Don't just assert on the path, as other tests might use the devmgr and cause race
        // conditions.
        fdio::service_connect(&path, remote).expect("connecting to partition OK");

        let proxy = PartitionProxy::new(fidl::AsyncChannel::from_channel(local).unwrap());
        let (status, guid) = proxy.get_type_guid().await.expect("send get type guid");
        zx::Status::ok(status).expect("get_type_guid ok");

        assert_eq!(guid.unwrap().value, WORKSTATION_INSTALLER_GPT);
    }
}
