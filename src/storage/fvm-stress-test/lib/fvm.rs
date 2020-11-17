// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_hardware_block_partition::Guid,
    fidl_fuchsia_hardware_block_volume::{VolumeMarker, VolumeProxy},
    fuchsia_component::client::{connect_channel_to_service_at_path, connect_to_service_at_path},
    fuchsia_zircon::{Channel, Status},
    futures::channel::mpsc,
    futures::StreamExt,
    log::debug,
    remote_block_device::{BufferSlice, MutableBufferSlice, RemoteBlockDevice},
    std::path::PathBuf,
    stress_test_utils::get_volume_path,
};

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
    pub async fn new(block_path: PathBuf, instance_guid: &Guid, slice_size: u64) -> Self {
        let volume_path = get_volume_path(block_path, instance_guid).await;
        let volume_path = volume_path.to_str().unwrap();

        let volume_proxy = connect_to_service_at_path::<VolumeMarker>(volume_path).unwrap();

        // Connect to the Block FIDL protocol
        let (client_end, server_end) = Channel::create().unwrap();
        connect_channel_to_service_at_path(server_end, volume_path).unwrap();
        let block_device = RemoteBlockDevice::new(client_end).unwrap();

        return Self { volume_proxy, block_device, slice_size };
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
            debug!("Receiving next path");
            let block_path = self.path_receiver.next().await.unwrap();
            self.connection =
                Some(VolumeConnection::new(block_path, &self.instance_guid, self.slice_size).await);
        }
        &self.connection.as_ref().unwrap()
    }

    pub async fn write_slice_at(&mut self, data: &[u8], slice_offset: u64) -> Result<(), Status> {
        loop {
            let connection = self.reconnect_if_needed().await;
            if let Some(result) = connection.write_slice_at(data, slice_offset).await {
                break result;
            } else {
                self.connection = None;
            }
        }
    }

    pub async fn read_slice_at(&mut self, slice_offset: u64) -> Result<Vec<u8>, Status> {
        loop {
            let connection = self.reconnect_if_needed().await;
            if let Some(result) = connection.read_slice_at(slice_offset).await {
                break result;
            } else {
                self.connection = None;
            }
        }
    }

    pub async fn extend(&mut self, start_slice: u64, slice_count: u64) -> Result<(), Status> {
        loop {
            let connection = self.reconnect_if_needed().await;
            if let Some(result) = connection.extend(start_slice, slice_count).await {
                break result;
            } else {
                self.connection = None;
            }
        }
    }

    pub async fn shrink(&mut self, start_slice: u64, slice_count: u64) -> Result<(), Status> {
        loop {
            let connection = self.reconnect_if_needed().await;
            if let Some(result) = connection.shrink(start_slice, slice_count).await {
                break result;
            } else {
                self.connection = None;
            }
        }
    }

    pub async fn destroy(mut self) -> Result<(), Status> {
        loop {
            let connection = self.reconnect_if_needed().await;
            if let Some(result) = connection.destroy().await {
                break result;
            } else {
                self.connection = None;
            }
        }
    }
}
