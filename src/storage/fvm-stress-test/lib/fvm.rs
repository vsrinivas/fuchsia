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

pub struct Volume {
    // Instance GUID of this volume
    instance_guid: Guid,

    // Receives a path to the new dev/class/block directory.
    // When the volume loses its connection to the block device,
    // it will expect to receive a path to the new block directory
    // from this receiver.
    path_receiver: mpsc::UnboundedReceiver<PathBuf>,

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

pub fn as_status_error(result: Result<(), anyhow::Error>) -> Result<(), Status> {
    match result {
        Ok(()) => Ok(()),
        Err(e) => match e.downcast::<Status>() {
            Ok(s) => Err(s),
            Err(e) => panic!("Unrecoverable connection error: {:?}", e),
        },
    }
}

impl Volume {
    pub async fn new(
        block_path: PathBuf,
        instance_guid: Guid,
    ) -> (Self, mpsc::UnboundedSender<PathBuf>) {
        let (sender, path_receiver) = mpsc::unbounded::<PathBuf>();
        let (volume_proxy, block_device) = connect(&block_path, &instance_guid).await;

        // Get slice information from volume
        let (status, volume_info) = volume_proxy.query().await.unwrap();
        Status::ok(status).unwrap();
        let volume_info = volume_info.unwrap();
        let slice_size = volume_info.slice_size;
        let max_vslice_count = volume_info.vslice_count;

        let volume = Self {
            instance_guid,
            path_receiver,
            volume_proxy,
            block_device,
            slice_size,
            max_vslice_count,
        };

        (volume, sender)
    }

    pub fn slice_size(&self) -> u64 {
        self.slice_size
    }

    pub fn max_vslice_count(&self) -> u64 {
        self.max_vslice_count
    }

    pub async fn status_code_or_reconnect(
        &mut self,
        result: Result<i32, fidl::Error>,
    ) -> Option<i32> {
        match result {
            Ok(code) => Some(code),
            Err(e) => {
                if e.is_closed() {
                    self.reconnect().await;
                    None
                } else {
                    panic!("Unrecoverable connection error: {}", e);
                }
            }
        }
    }

    pub async fn reconnect(&mut self) {
        debug!("Reconnecting...");
        let new_block_path = self.path_receiver.next().await.unwrap();
        let (volume_proxy, block_device) = connect(&new_block_path, &self.instance_guid).await;
        self.volume_proxy = volume_proxy;
        self.block_device = block_device;
    }

    pub async fn write_slice_at(&mut self, data: &[u8], slice_offset: u64) -> Result<(), Status> {
        let offset = slice_offset * self.slice_size;
        assert_eq!(data.len() as u64, self.slice_size);

        loop {
            let buffer_slice = BufferSlice::from(data);
            let result = self.block_device.write_at(buffer_slice, offset).await;
            let result = as_status_error(result);

            if let Err(Status::CANCELED) = result {
                self.reconnect().await;
            } else {
                break result;
            }
        }
    }

    pub async fn read_slice_at(&mut self, slice_offset: u64) -> Result<Vec<u8>, Status> {
        let mut data: Vec<u8> = Vec::with_capacity(self.slice_size as usize);
        data.resize(self.slice_size as usize, 0);

        let offset = slice_offset * self.slice_size;
        assert_eq!(data.len() as u64, self.slice_size);

        loop {
            let buffer_slice = MutableBufferSlice::from(data.as_mut_slice());
            let result = self.block_device.read_at(buffer_slice, offset).await;
            let result = as_status_error(result);

            if let Err(Status::CANCELED) = result {
                self.reconnect().await;
            } else {
                break result.map(|_| data);
            }
        }
    }

    pub async fn extend(&mut self, start_slice: u64, slice_count: u64) -> Result<(), Status> {
        let status_code = loop {
            let result = self.volume_proxy.extend(start_slice, slice_count).await;
            if let Some(code) = self.status_code_or_reconnect(result).await {
                break code;
            }
        };
        Status::ok(status_code)
    }

    pub async fn shrink(&mut self, start_slice: u64, slice_count: u64) -> Result<(), Status> {
        let status_code = loop {
            let result = self.volume_proxy.shrink(start_slice, slice_count).await;
            if let Some(code) = self.status_code_or_reconnect(result).await {
                break code;
            }
        };
        Status::ok(status_code)
    }

    pub async fn destroy(mut self) -> Result<(), Status> {
        let status_code = loop {
            let result = self.volume_proxy.destroy().await;
            if let Some(code) = self.status_code_or_reconnect(result).await {
                break code;
            }
        };
        Status::ok(status_code)
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
        mut instance_guid: Guid,
        name: &str,
        flags: u32,
    ) {
        let status = self
            .proxy
            .allocate_partition(slice_count, &mut type_guid, &mut instance_guid, name, flags)
            .await
            .unwrap();
        Status::ok(status).unwrap();
    }
}
