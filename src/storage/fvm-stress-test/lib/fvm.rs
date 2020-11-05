// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_hardware_block_partition::Guid,
    fidl_fuchsia_hardware_block_volume::{VolumeManagerProxy, VolumeMarker, VolumeProxy},
    fuchsia_component::client::{connect_channel_to_service_at_path, connect_to_service_at_path},
    fuchsia_zircon::{Channel, Status},
    futures::channel::mpsc,
    futures::StreamExt,
    log::debug,
    rand::{rngs::SmallRng, Rng},
    remote_block_device::{BufferSlice, MutableBufferSlice, RemoteBlockDevice},
    std::path::PathBuf,
};

// All partitions in this test have their type set to this arbitrary GUID.
pub const TYPE_GUID: Guid = Guid {
    value: [0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf],
};

// Partitions get their instance GUID from this function
pub fn random_guid(rng: &mut SmallRng) -> Guid {
    Guid { value: rng.gen() }
}

struct VolumeConnection {
    volume_proxy: VolumeProxy,
    block_device: RemoteBlockDevice,
    slice_size: u64,
}

fn status_code(result: Result<i32, fidl::Error>) -> Option<i32> {
    match result {
        Ok(code) => Some(code),
        Err(e) => {
            if e.is_closed() {
                None
            } else {
                panic!("Unrecoverable connection error: {}", e);
            }
        }
    }
}

impl VolumeConnection {
    pub async fn new(block_path: &PathBuf, instance_guid: &Guid, slice_size: u64) -> Self {
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

                    return Self { volume_proxy, block_device, slice_size };
                }
            }
        }
    }

    // Writes a slice worth of data at the given offset.
    // If the volume is disconnected, None is returned.
    pub async fn write_slice_at(
        &self,
        data: &[u8],
        slice_offset: u64,
    ) -> Option<Result<(), Status>> {
        let offset = slice_offset * self.slice_size;
        assert_eq!(data.len() as u64, self.slice_size);

        let buffer_slice = BufferSlice::from(data);
        let result = self.block_device.write_at(buffer_slice, offset).await;
        let result = as_status_error(result);

        if let Err(Status::CANCELED) = result {
            return None;
        }

        return Some(result);
    }

    // Reads a slice worth of data from the given offset.
    // If the volume is disconnected, None is returned.
    pub async fn read_slice_at(&self, slice_offset: u64) -> Option<Result<Vec<u8>, Status>> {
        let mut data: Vec<u8> = Vec::with_capacity(self.slice_size as usize);
        data.resize(self.slice_size as usize, 0);

        let offset = slice_offset * self.slice_size;
        assert_eq!(data.len() as u64, self.slice_size);

        let buffer_slice = MutableBufferSlice::from(data.as_mut_slice());
        let result = self.block_device.read_at(buffer_slice, offset).await;
        let result = as_status_error(result);
        let result = result.map(|_| data);

        if let Err(Status::CANCELED) = result {
            return None;
        }

        return Some(result);
    }

    // Adds slices to the volume at a given offset.
    // If the volume is disconnected, None is returned.
    pub async fn extend(&self, start_slice: u64, slice_count: u64) -> Option<Result<(), Status>> {
        let result = self.volume_proxy.extend(start_slice, slice_count).await;
        if let Some(code) = status_code(result) {
            Some(Status::ok(code))
        } else {
            None
        }
    }

    // Removes slices from the volume at a given offset.
    // If the volume is disconnected, None is returned.
    pub async fn shrink(&self, start_slice: u64, slice_count: u64) -> Option<Result<(), Status>> {
        let result = self.volume_proxy.shrink(start_slice, slice_count).await;
        if let Some(code) = status_code(result) {
            Some(Status::ok(code))
        } else {
            None
        }
    }

    // Destroys the volume, returning all slices to the volume manager.
    // If the volume is disconnected, None is returned.
    pub async fn destroy(&self) -> Option<Result<(), Status>> {
        let result = self.volume_proxy.destroy().await;
        if let Some(code) = status_code(result) {
            Some(Status::ok(code))
        } else {
            None
        }
    }
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

pub fn as_status_error(result: Result<(), anyhow::Error>) -> Result<(), Status> {
    match result {
        Ok(()) => Ok(()),
        Err(e) => match e.downcast::<Status>() {
            Ok(s) => Err(s),
            Err(e) => panic!("Unrecoverable connection error: {:?}", e),
        },
    }
}

pub struct Volume {
    // Instance GUID of this volume
    instance_guid: Guid,

    // Receives a path to the new dev/class/block directory.
    // When the volume loses its connection to the block device,
    // it will expect to receive a path to the new block directory
    // from this receiver.
    path_receiver: mpsc::UnboundedReceiver<PathBuf>,

    // Active connection to the block device
    connection: Option<VolumeConnection>,

    // Size (in bytes) of a slice, as defined by Volume protocol
    slice_size: u64,
}

impl Volume {
    pub async fn new(
        instance_guid: Guid,
        slice_size: u64,
    ) -> (Self, mpsc::UnboundedSender<PathBuf>) {
        let (sender, path_receiver) = mpsc::unbounded::<PathBuf>();

        let volume = Self { instance_guid, path_receiver, connection: None, slice_size };

        (volume, sender)
    }

    pub fn slice_size(&self) -> u64 {
        self.slice_size
    }

    async fn reconnect_if_needed(&mut self) -> &VolumeConnection {
        if self.connection.is_none() {
            let block_path = self.path_receiver.next().await.unwrap();
            self.connection = Some(
                VolumeConnection::new(&block_path, &self.instance_guid, self.slice_size).await,
            );
        }
        &self.connection.as_ref().unwrap()
    }

    pub async fn write_slice_at(&mut self, data: &[u8], slice_offset: u64) -> Result<(), Status> {
        loop {
            let connection = self.reconnect_if_needed().await;
            if let Some(result) = connection.write_slice_at(data, slice_offset).await {
                break result;
            }
        }
    }

    pub async fn read_slice_at(&mut self, slice_offset: u64) -> Result<Vec<u8>, Status> {
        loop {
            let connection = self.reconnect_if_needed().await;
            if let Some(result) = connection.read_slice_at(slice_offset).await {
                break result;
            }
        }
    }

    pub async fn extend(&mut self, start_slice: u64, slice_count: u64) -> Result<(), Status> {
        loop {
            let connection = self.reconnect_if_needed().await;
            if let Some(result) = connection.extend(start_slice, slice_count).await {
                break result;
            }
        }
    }

    pub async fn shrink(&mut self, start_slice: u64, slice_count: u64) -> Result<(), Status> {
        loop {
            let connection = self.reconnect_if_needed().await;
            if let Some(result) = connection.shrink(start_slice, slice_count).await {
                break result;
            }
        }
    }

    pub async fn destroy(mut self) -> Result<(), Status> {
        loop {
            let connection = self.reconnect_if_needed().await;
            if let Some(result) = connection.destroy().await {
                break result;
            }
        }
    }
}

pub struct VolumeManager {
    proxy: VolumeManagerProxy,
}

impl VolumeManager {
    pub fn new(proxy: VolumeManagerProxy) -> Self {
        Self { proxy }
    }

    pub async fn new_volume(
        &self,
        slice_count: u64,
        mut type_guid: Guid,
        instance_guid: &mut Guid,
        name: &str,
        flags: u32,
    ) {
        let status = self
            .proxy
            .allocate_partition(slice_count, &mut type_guid, instance_guid, name, flags)
            .await
            .unwrap();
        Status::ok(status).unwrap();
    }
}
