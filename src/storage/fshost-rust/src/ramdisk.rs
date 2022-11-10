// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Functionality for extracting a ramdisk image from the zircon boot items. This ramdisk contains
//! an fvm with blobfs and data volumes, and is intended to be used in conjunction with the
//! fvm_ramdisk option, in run modes where we need to operate on the real disk and can't run
//! filesystems off it, such as recovery.

use {
    anyhow::{ensure, Context, Error},
    device_watcher::recursive_wait_and_open_node,
    fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
    futures::TryFutureExt,
    zerocopy::FromBytes,
};

/// The following types and constants are defined in zircon/system/public/zircon/boot/image.h.
const ZBI_TYPE_STORAGE_RAMDISK: u32 = 0x4b534452;
const ZBI_FLAGS_VERSION: u32 = 0x00010000;
const ZBI_ITEM_MAGIC: u32 = 0xb5781729;
const ZBI_FLAGS_STORAGE_COMPRESSED: u32 = 0x00000001;

#[repr(C)]
#[derive(FromBytes)]
struct ZbiHeader {
    type_: u32,
    length: u32,
    extra: u32,
    flags: u32,
    _reserved0: u32,
    _reserved1: u32,
    magic: u32,
    _crc32: u32,
}

async fn create_ramdisk(zbi_vmo: zx::Vmo) -> Result<(), Error> {
    let mut header_buf = [0u8; std::mem::size_of::<ZbiHeader>()];
    zbi_vmo.read(&mut header_buf, 0).context("reading zbi item header")?;
    // Expect is fine here - we made the buffer ourselves to the exact size of the header so
    // something is very wrong if we trip this.
    let header = ZbiHeader::read_from(header_buf.as_slice()).expect("buffer was the wrong size");

    ensure!(
        header.flags & ZBI_FLAGS_VERSION != 0,
        "invalid ZBI_TYPE_STORAGE_RAMDISK item header: flags"
    );
    ensure!(header.magic == ZBI_ITEM_MAGIC, "invalid ZBI_TYPE_STORAGE_RAMDISK item header: magic");
    ensure!(
        header.type_ == ZBI_TYPE_STORAGE_RAMDISK,
        "invalid ZBI_TYPE_STORAGE_RAMDISK item header: type"
    );

    // TODO(fxbug.dev/34597): The old code ignored uncompressed items too, and silently.  Really
    // the protocol should be cleaned up so the VMO arrives without the header in it and then it
    // could just be used here directly if uncompressed (or maybe bootsvc deals with decompression
    // in the first place so the uncompressed VMO is always what we get).
    ensure!(
        header.flags & ZBI_FLAGS_STORAGE_COMPRESSED != 0,
        "ignoring uncompressed RAMDISK item in ZBI"
    );

    let ramdisk_vmo = zx::Vmo::create(header.extra as u64).context("making output vmo")?;
    let mut compressed_buf = vec![0u8; header.length as usize];
    zbi_vmo
        .read(&mut compressed_buf, std::mem::size_of::<ZbiHeader>() as u64)
        .context("reading compressed ramdisk")?;
    let decompressed_buf =
        zstd::decode_all(compressed_buf.as_slice()).context("zstd decompression failed")?;
    ramdisk_vmo.write(&decompressed_buf, 0).context("writing decompressed contents to vmo")?;

    let dev = fuchsia_fs::directory::open_in_namespace("/dev", fio::OpenFlags::RIGHT_READABLE)
        .context("opening /dev")?;
    recursive_wait_and_open_node(&dev, "sys/platform/00:00:2d/ramctl")
        .await
        .context("waiting for ramctl")?;

    let ramdisk = ramdevice_client::VmoRamdiskClientBuilder::new(ramdisk_vmo)
        .build()
        .context("building ramdisk from vmo")?;
    // We want the ramdisk to continue to exist for the lifetime of the system, so we just leak the
    // pointer instead of running the Drop implementation, which attempts to destroy the ramdisk.
    std::mem::forget(ramdisk);

    Ok(())
}

pub async fn set_up_ramdisk() -> Result<(), Error> {
    let proxy = connect_to_protocol::<fidl_fuchsia_boot::ItemsMarker>()?;
    let (maybe_vmo, _length) = proxy
        .get(ZBI_TYPE_STORAGE_RAMDISK, 0)
        .await
        .context("boot items get failed (fidl failure)")?;
    if let Some(vmo) = maybe_vmo {
        fasync::Task::spawn(create_ramdisk(vmo).unwrap_or_else(|e| {
            tracing::error!(?e, "failed to create ramdisk filesystems");
        }))
        .detach();
    }
    Ok(())
}
