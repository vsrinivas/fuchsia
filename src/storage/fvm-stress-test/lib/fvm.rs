// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_hardware_block_partition::Guid,
    fidl_fuchsia_hardware_block_volume::{VolumeManagerProxy, VolumeMarker, VolumeProxy},
    fuchsia_component::client::{connect_channel_to_service_at_path, connect_to_service_at_path},
    fuchsia_zircon::{Channel, Status},
    rand::{rngs::SmallRng, Rng},
    remote_block_device::{BufferSlice, MutableBufferSlice, RemoteBlockDevice},
    std::{path::PathBuf, time::Duration},
};

// All partitions in this test have their type set to this arbitrary GUID.
pub const GUID_TYPE: Guid = Guid {
    value: [0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf],
};

// Partitions get their instance GUID from this function
pub fn random_guid(rng: &mut SmallRng) -> Guid {
    Guid { value: rng.gen() }
}

pub struct Volume {
    // Client end of Volume FIDL protocol
    volume_proxy: VolumeProxy,

    // Wrapper over Block FIDL protocol
    block_device: RemoteBlockDevice,

    // Size (in bytes) of a slice, as defined by Volume protocol
    slice_size: u64,

    // The maximum number of virtual slices that can be
    // allocated for this volume, according to volume info.
    max_vslice_count: u64,
}

impl Volume {
    pub async fn new(volume_path: &str, expected_instance_guid: Guid) -> Self {
        let volume_proxy = connect_to_service_at_path::<VolumeMarker>(volume_path).unwrap();

        // The GUIDs must match
        let (status, actual_guid_instance) = volume_proxy.get_instance_guid().await.unwrap();
        Status::ok(status).unwrap();
        let actual_guid_instance = actual_guid_instance.unwrap();
        assert!(*actual_guid_instance == expected_instance_guid);

        // Get slice information from volume
        let (status, volume_info) = volume_proxy.query().await.unwrap();
        Status::ok(status).unwrap();
        let volume_info = volume_info.unwrap();
        let slice_size = volume_info.slice_size;
        let max_vslice_count = volume_info.vslice_count;

        // Setup a remote block device
        let (client_end, server_end) = Channel::create().unwrap();
        connect_channel_to_service_at_path(server_end, volume_path).unwrap();
        let block_device = RemoteBlockDevice::new(client_end).unwrap();

        Self { volume_proxy, block_device, slice_size, max_vslice_count }
    }

    pub fn slice_size(&self) -> u64 {
        self.slice_size
    }

    pub fn max_vslice_count(&self) -> u64 {
        self.max_vslice_count
    }

    pub async fn write_slice_at(&mut self, data: &[u8], slice_offset: u64) {
        let offset = slice_offset * self.slice_size;
        assert_eq!(data.len() as u64, self.slice_size);
        let buffer_slice = BufferSlice::from(data);
        self.block_device.write_at(buffer_slice, offset).await.unwrap();
    }

    pub async fn read_slice_at(&mut self, slice_offset: u64) -> Vec<u8> {
        let mut data: Vec<u8> = Vec::with_capacity(self.slice_size as usize);
        data.resize(self.slice_size as usize, 0);

        let offset = slice_offset * self.slice_size;
        assert_eq!(data.len() as u64, self.slice_size);

        let buffer_slice = MutableBufferSlice::from(data.as_mut_slice());
        self.block_device.read_at(buffer_slice, offset).await.unwrap();

        data
    }

    pub async fn extend(&mut self, start_slice: u64, slice_count: u64) -> Result<(), Status> {
        let status = self.volume_proxy.extend(start_slice, slice_count).await.unwrap();
        Status::ok(status)
    }

    pub async fn shrink(&mut self, start_slice: u64, slice_count: u64) {
        let status = self.volume_proxy.shrink(start_slice, slice_count).await.unwrap();
        Status::ok(status).unwrap();
    }

    pub async fn destroy(self) {
        let status = self.volume_proxy.destroy().await.unwrap();
        Status::ok(status).unwrap()
    }
}

pub struct VolumeManager {
    proxy: VolumeManagerProxy,
    dev_path: PathBuf,
    num_vols: u64,
}

impl VolumeManager {
    pub fn new(proxy: VolumeManagerProxy, dev_path: PathBuf) -> Self {
        Self { proxy, dev_path, num_vols: 0 }
    }

    pub async fn new_volume(
        &mut self,
        slice_count: u64,
        mut guid_type: Guid,
        mut guid_instance: Guid,
        name: &str,
        flags: u32,
    ) -> Volume {
        let status = self
            .proxy
            .allocate_partition(slice_count, &mut guid_type, &mut guid_instance, name, flags)
            .await
            .unwrap();
        Status::ok(status).unwrap();

        // TODO(xbhatnag): Do not guess the block number
        self.num_vols += 1;
        let volume_path = self.dev_path.join(format!("class/block/{:03}", self.num_vols));
        let volume_path = volume_path.to_str().unwrap();

        // Wait for the volume to appear
        ramdevice_client::wait_for_device(volume_path, Duration::from_secs(5)).unwrap();

        Volume::new(volume_path, guid_instance).await
    }
}
