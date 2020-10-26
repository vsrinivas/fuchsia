// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_hardware_block_partition::Guid,
    fidl_fuchsia_hardware_block_volume::{VolumeManagerProxy, VolumeMarker, VolumeProxy},
    fuchsia_component::client::{connect_channel_to_service_at_path, connect_to_service_at_path},
    fuchsia_zircon::{Channel, Status},
    log::debug,
    rand::{rngs::SmallRng, Rng},
    remote_block_device::{BufferSlice, MutableBufferSlice, RemoteBlockDevice},
    std::path::PathBuf,
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
    // Path to the all block devices
    block_path: PathBuf,

    // Instance GUID of this volume
    instance_guid: Guid,

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

async fn does_guid_match(volume_proxy: &VolumeProxy, expected_instance_guid: &Guid) -> bool {
    // The GUIDs must match
    let (status, actual_guid_instance) = volume_proxy.get_instance_guid().await.unwrap();

    // The ramdisk is also a block device, but does not support the Volume protocol
    if let Err(Status::NOT_SUPPORTED) = Status::ok(status) {
        return false;
    }

    let actual_guid_instance = actual_guid_instance.unwrap();
    *actual_guid_instance == *expected_instance_guid
}

async fn connect(block_path: &PathBuf, instance_guid: &Guid) -> (VolumeProxy, RemoteBlockDevice) {
    debug!("Connecting to {:?}", instance_guid);

    loop {
        // TODO(xbhatnag): Find a better way to wait for the volume to appear
        let entries = std::fs::read_dir(block_path).unwrap();
        for entry in entries {
            let entry = entry.unwrap();
            let volume_path = block_path.join(entry.file_name());
            let volume_path = volume_path.to_str().unwrap();

            // Connect to the Volume FIDL protocol
            let volume_proxy = connect_to_service_at_path::<VolumeMarker>(volume_path).unwrap();
            if does_guid_match(&volume_proxy, instance_guid).await {
                // Connect to the Block FIDL protocol
                let (client_end, server_end) = Channel::create().unwrap();
                connect_channel_to_service_at_path(server_end, volume_path).unwrap();
                let block_device = RemoteBlockDevice::new(client_end).unwrap();

                return (volume_proxy, block_device);
            }
        }
    }
}

// returns |true| if the |result| is a connection error.
// returns |false| if the |result| is ok.
// panics on other errors.
pub fn need_reconnect(result: Result<(), Error>) -> bool {
    match result {
        Ok(()) => false,
        Err(e) => match e.downcast::<Status>() {
            Ok(Status::CANCELED) => true,
            other_error => panic!("Unexpected error: {:?}", other_error),
        },
    }
}

impl Volume {
    pub async fn new(block_path: PathBuf, instance_guid: Guid) -> Self {
        let (volume_proxy, block_device) = connect(&block_path, &instance_guid).await;

        // Get slice information from volume
        let (status, volume_info) = volume_proxy.query().await.unwrap();
        Status::ok(status).unwrap();
        let volume_info = volume_info.unwrap();
        let slice_size = volume_info.slice_size;
        let max_vslice_count = volume_info.vslice_count;

        Self { block_path, instance_guid, volume_proxy, block_device, slice_size, max_vslice_count }
    }

    pub fn slice_size(&self) -> u64 {
        self.slice_size
    }

    pub fn max_vslice_count(&self) -> u64 {
        self.max_vslice_count
    }

    pub async fn reconnect(&mut self) {
        // Connection lost. Reconnect!
        let (volume_proxy, block_device) = connect(&self.block_path, &self.instance_guid).await;
        self.volume_proxy = volume_proxy;
        self.block_device = block_device;
    }

    pub async fn write_slice_at(&mut self, data: &[u8], slice_offset: u64) {
        let offset = slice_offset * self.slice_size;
        assert_eq!(data.len() as u64, self.slice_size);

        loop {
            let buffer_slice = BufferSlice::from(data);
            let result = self.block_device.write_at(buffer_slice, offset).await;
            if !need_reconnect(result) {
                break;
            }
            self.reconnect().await;
        }
    }

    pub async fn read_slice_at(&mut self, slice_offset: u64) -> Vec<u8> {
        let mut data: Vec<u8> = Vec::with_capacity(self.slice_size as usize);
        data.resize(self.slice_size as usize, 0);

        let offset = slice_offset * self.slice_size;
        assert_eq!(data.len() as u64, self.slice_size);

        loop {
            let buffer_slice = MutableBufferSlice::from(data.as_mut_slice());
            let result = self.block_device.read_at(buffer_slice, offset).await;
            if !need_reconnect(result) {
                break data;
            }
            self.reconnect().await;
        }
    }

    pub async fn extend(&mut self, start_slice: u64, slice_count: u64) -> Result<(), Status> {
        loop {
            let result = self.volume_proxy.extend(start_slice, slice_count).await;
            match result {
                Ok(status) => break Status::ok(status),
                Err(fidl::Error::ClientChannelClosed { .. }) => self.reconnect().await,
                Err(e) => panic!("Unexpected error: {}", e),
            }
        }
    }

    pub async fn shrink(&mut self, start_slice: u64, slice_count: u64) {
        loop {
            let result = self.volume_proxy.shrink(start_slice, slice_count).await;
            match result {
                Ok(status) => break Status::ok(status).unwrap(),
                Err(fidl::Error::ClientChannelClosed { .. }) => self.reconnect().await,
                Err(e) => panic!("Unexpected error: {}", e),
            }
        }
    }

    pub async fn destroy(mut self) {
        loop {
            let result = self.volume_proxy.destroy().await;
            match result {
                Ok(status) => break Status::ok(status).unwrap(),
                Err(fidl::Error::ClientChannelClosed { .. }) => self.reconnect().await,
                Err(e) => panic!("Unexpected error: {}", e),
            }
        }
    }
}

pub struct VolumeManager {
    proxy: VolumeManagerProxy,
    block_path: PathBuf,
}

impl VolumeManager {
    pub fn new(proxy: VolumeManagerProxy, block_path: PathBuf) -> Self {
        Self { proxy, block_path }
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

        Volume::new(self.block_path.clone(), guid_instance).await
    }
}
