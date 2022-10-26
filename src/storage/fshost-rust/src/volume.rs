// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_hardware_block_volume::VolumeAndNodeProxy,
    fuchsia_zircon as zx,
    std::cmp,
};

// Number of bits required for the VSlice address space.
const SLICE_ENTRY_VSLICE_BITS: u64 = 32;
// Maximum number of VSlices that can be addressed.
const MAX_VSLICES: u64 = 1 << (SLICE_ENTRY_VSLICE_BITS - 1);
const DEFAULT_VOLUME_PERCENTAGE: u64 = 10;
const DEFAULT_VOLUME_SIZE: u64 = 24 * 1024 * 1024;

pub async fn resize_volume(
    volume_proxy: &VolumeAndNodeProxy,
    target_bytes: u64,
    inside_zxcrypt: bool,
) -> Result<u64, Error> {
    // Free all the existing slices.
    let mut slice = 1;
    // The -1 here is because of zxcrypt; zxcrypt will offset all slices by 1 to account for its
    // header.  zxcrypt isn't present in all cases, but that won't matter since minfs shouldn't be
    // using a slice so high.
    while slice < (MAX_VSLICES - 1) {
        let (status, vslices, response_count) =
            volume_proxy.query_slices(&[slice]).await.context("Transport error on query_slices")?;
        zx::Status::ok(status).context("query_slices failed")?;
        if response_count == 0 {
            break;
        }
        for i in 0..response_count {
            if vslices[i as usize].allocated {
                let status = volume_proxy
                    .shrink(slice, vslices[i as usize].count)
                    .await
                    .context("Transport error on shrink")?;
                zx::Status::ok(status)?;
            }
            slice += vslices[i as usize].count;
        }
    }
    let (status, volume_manager_info, _volume_info) =
        volume_proxy.get_volume_info().await.context("Transport error on get_volume_info")?;
    zx::Status::ok(status).context("get_volume_info failed")?;
    let manager = volume_manager_info.ok_or(anyhow!("Expected volume manager info"))?;
    let slice_size = manager.slice_size;

    // Count the first slice (which is already allocated to the volume) as available.
    let slices_available = 1 + manager.slice_count - manager.assigned_slice_count;
    let mut slice_count = target_bytes / slice_size;
    if slice_count == 0 {
        // If a size is not specified, limit the size of the data partition so as not to use up all
        // FVM's space (thus limiting blobfs growth).  10% or 24MiB (whichever is larger) should be
        // enough.
        let default_slices = cmp::max(
            manager.slice_count * DEFAULT_VOLUME_PERCENTAGE / 100,
            DEFAULT_VOLUME_SIZE / slice_size,
        );
        tracing::info!("Using default size of {:?}", default_slices * slice_size);
        slice_count = cmp::min(slices_available, default_slices);
    }
    if slices_available < slice_count {
        tracing::info!(
            "Only {:?} slices available. Some functionality
                may be missing",
            slices_available
        );
        slice_count = slices_available;
    }
    assert!(slice_count > 0);
    if inside_zxcrypt {
        // zxcrypt occupies an additional slice for its own metadata.
        slice_count -= 1;
    }
    if slice_count > 1 {
        // -1 for slice_count because we get the first slice for free.
        let status = volume_proxy
            .extend(1, slice_count - 1)
            .await
            .context("Transport error on extend call")?;
        zx::Status::ok(status)
            .context(format!("Failed to extend partition (slice count: {:?})", slice_count))?;
    }
    return Ok(slice_count * slice_size);
}

#[cfg(test)]
mod tests {
    use {
        crate::volume::{resize_volume, MAX_VSLICES},
        anyhow::Error,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_hardware_block_volume::{
            VolumeAndNodeMarker, VolumeAndNodeRequest, VolumeManagerInfo, VsliceRange,
        },
        fuchsia_zircon as zx,
        futures::{pin_mut, select, FutureExt, StreamExt},
    };

    const SLICE_SIZE: u64 = 16384;
    const SLICE_COUNT: u64 = 5000;
    const MAXIMUM_SLICE_COUNT: u64 = 5500;
    const RANGE_ALLOCATED: u64 = 1234;

    async fn check_resize_volume(
        target_bytes: u64,
        inside_zxcrypt: bool,
        assigned_slice_count: u64,
        expected_extend_slice_count: u64,
    ) -> Result<u64, Error> {
        let (proxy, mut stream) = create_proxy_and_stream::<VolumeAndNodeMarker>().unwrap();
        let mock_device = async {
            while let Some(request) = stream.next().await {
                match request {
                    Ok(VolumeAndNodeRequest::QuerySlices { responder, start_slices }) => {
                        let mut slices = vec![VsliceRange { allocated: false, count: 0 }; 16];
                        slices[0] = VsliceRange { allocated: true, count: RANGE_ALLOCATED };
                        let mut arr = slices.iter_mut().collect::<Vec<_>>().try_into().unwrap();
                        let count = if start_slices[0] == 1 { 1 } else { 0 };
                        responder.send(zx::sys::ZX_OK, &mut arr, count).unwrap();
                    }
                    Ok(VolumeAndNodeRequest::Shrink { responder, start_slice, slice_count }) => {
                        assert_eq!(start_slice, 1);
                        assert_eq!(slice_count, RANGE_ALLOCATED);
                        responder.send(zx::sys::ZX_OK).unwrap();
                    }
                    Ok(VolumeAndNodeRequest::GetVolumeInfo { responder }) => {
                        responder
                            .send(
                                zx::sys::ZX_OK,
                                Some(&mut VolumeManagerInfo {
                                    slice_size: SLICE_SIZE,
                                    slice_count: SLICE_COUNT,
                                    assigned_slice_count: assigned_slice_count,
                                    maximum_slice_count: MAXIMUM_SLICE_COUNT,
                                    max_virtual_slice: MAX_VSLICES,
                                }),
                                None,
                            )
                            .unwrap();
                    }
                    Ok(VolumeAndNodeRequest::Extend { responder, start_slice, slice_count }) => {
                        assert_eq!(start_slice, 1);
                        assert_eq!(slice_count, expected_extend_slice_count);
                        responder.send(zx::sys::ZX_OK).unwrap();
                    }
                    _ => {
                        println!("Unexpected request: {:?}", request);
                        unreachable!()
                    }
                }
            }
        }
        .fuse();

        pin_mut!(mock_device);

        select! {
            _ = mock_device => unreachable!(),
            matches = resize_volume(&proxy, target_bytes, inside_zxcrypt).fuse() => matches,
        }
    }

    #[fuchsia::test]
    async fn test_target_bytes_zero_slice_count_equals_default_slices() {
        let target_bytes = 0;
        let inside_zxcrypt = false;
        let assigned_slice_count = 3000;
        let expected_extend_slice_count = 1535;
        assert_eq!(
            check_resize_volume(
                target_bytes,
                inside_zxcrypt,
                assigned_slice_count,
                expected_extend_slice_count
            )
            .await
            .unwrap(),
            // Add one because extend ignores free allocated slice per volume
            (expected_extend_slice_count + 1) * SLICE_SIZE
        );
    }

    #[fuchsia::test]
    async fn test_target_bytes_zero_slice_count_equals_slices_available() {
        let target_bytes = 0;
        let inside_zxcrypt = false;
        let assigned_slice_count = 4000;
        let expected_extend_slice_count = 1000;
        assert_eq!(
            check_resize_volume(
                target_bytes,
                inside_zxcrypt,
                assigned_slice_count,
                expected_extend_slice_count
            )
            .await
            .unwrap(),
            // Add one because extend ignores free allocated slice per volume
            (expected_extend_slice_count + 1) * SLICE_SIZE
        );
    }

    #[fuchsia::test]
    async fn test_slice_count_less_than_slice_available() {
        let target_bytes = SLICE_SIZE * 2500;
        let inside_zxcrypt = false;
        let assigned_slice_count = 3000;
        let expected_extend_slice_count = 2000;
        assert_eq!(
            check_resize_volume(
                target_bytes,
                inside_zxcrypt,
                assigned_slice_count,
                expected_extend_slice_count
            )
            .await
            .unwrap(),
            // Add one because extend ignores free allocated slice per volume
            (expected_extend_slice_count + 1) * SLICE_SIZE
        );
    }

    #[fuchsia::test]
    async fn test_inside_zxcrypt_no_extend_one_available_slice() {
        let target_bytes = SLICE_SIZE * 2000;
        let inside_zxcrypt = true;
        let assigned_slice_count = SLICE_COUNT;
        let expected_extend_slice_count = 0;
        assert_eq!(
            check_resize_volume(
                target_bytes,
                inside_zxcrypt,
                assigned_slice_count,
                expected_extend_slice_count
            )
            .await
            .unwrap(),
            0
        );
    }

    #[fuchsia::test]
    async fn test_inside_zxcrypt_no_extend_two_available_slices() {
        let target_bytes = SLICE_SIZE * 2000;
        let inside_zxcrypt = true;
        let assigned_slice_count = SLICE_COUNT - 1;
        let expected_extend_slice_count = 0;
        assert_eq!(
            check_resize_volume(
                target_bytes,
                inside_zxcrypt,
                assigned_slice_count,
                expected_extend_slice_count
            )
            .await
            .unwrap(),
            SLICE_SIZE
        );
    }

    #[fuchsia::test]
    async fn test_inside_zxcrypt_no_extend_one_slice_requested() {
        let target_bytes = SLICE_SIZE;
        let inside_zxcrypt = true;
        let assigned_slice_count = 4000;
        let expected_extend_slice_count = 0;
        assert_eq!(
            check_resize_volume(
                target_bytes,
                inside_zxcrypt,
                assigned_slice_count,
                expected_extend_slice_count
            )
            .await
            .unwrap(),
            0
        );
    }
}
