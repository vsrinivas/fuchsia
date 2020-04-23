// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, AsHandleRef, Vmo};
use std::fs;
use std::os::unix::io::AsRawFd;

#[allow(bad_style)]
#[cfg_attr(test, allow(dead_code))]
mod sys;
/// The list of sysconfig sub partitions.
pub use sys::SysconfigPartition;
#[cfg(not(test))]
use sys::*;

#[cfg(test)]
mod sys_mock;
#[cfg(test)]
use sys_mock::*;

pub mod channel;

struct SysconfigSyncClient(*mut sysconfig_sync_client_t);

impl SysconfigSyncClient {
    fn new(fd: std::os::unix::io::RawFd) -> Result<Self, zx::Status> {
        let mut sync_client: *mut sysconfig_sync_client_t = std::ptr::null_mut();
        // Caller is responsible for closing the fd.
        // On success, `sync_client` will be pointing to a `sysconfig_sync_client` struct, and must
        // be freed with `sysconfig_sync_client_free`.
        zx::ok(unsafe {
            sysconfig_sync_client_create(fd, &mut sync_client as *mut *mut sysconfig_sync_client_t)
        })?;
        Ok(SysconfigSyncClient(sync_client))
    }

    fn read_partition(&self, partition: SysconfigPartition) -> Result<Vec<u8>, zx::Status> {
        let size = self.get_partition_size(partition)?;
        let vmo = Vmo::create(size as u64)?;
        assert!(!self.0.is_null());
        // Requires that `self.0` is pointing to a valid `sysconfig_sync_client_t`.
        // Does not take ownership of the vmo handle.
        zx::ok(unsafe { sysconfig_read_partition(self.0, partition, vmo.raw_handle(), 0) })?;

        let mut result = vec![0u8; size];
        vmo.read(result.as_mut_slice(), 0)?;
        Ok(result)
    }

    fn write_partition(
        &self,
        partition: SysconfigPartition,
        data: &[u8],
    ) -> Result<(), zx::Status> {
        let size = self.get_partition_size(partition)?;
        if data.len() > size {
            return Err(zx::Status::OUT_OF_RANGE);
        }
        let vmo = Vmo::create(size as u64)?;
        vmo.write(data, 0)?;
        assert!(!self.0.is_null());
        // Requires that `self.0` is pointing to a valid `sysconfig_sync_client_t`.
        // Does not take ownership of the vmo handle.
        zx::ok(unsafe { sysconfig_write_partition(self.0, partition, vmo.raw_handle(), 0) })?;
        Ok(())
    }

    fn get_partition_size(&self, partition: SysconfigPartition) -> Result<usize, zx::Status> {
        assert!(!self.0.is_null());
        let mut out: usize = 0;
        // Requires that `self.0` is pointing to a valid `sysconfig_sync_client_t`.
        zx::ok(unsafe { sysconfig_get_partition_size(self.0, partition, &mut out) })?;
        Ok(out)
    }
}

impl Drop for SysconfigSyncClient {
    fn drop(&mut self) {
        assert!(!self.0.is_null());
        // Requires that `self.0` is pointing to a valid `sysconfig_sync_client_t`.
        unsafe {
            sysconfig_sync_client_free(self.0);
        }
    }
}

/// Read an entire sysconfig sub partition into a Vec<u8>.
pub fn read_partition(partition: SysconfigPartition) -> Result<Vec<u8>, std::io::Error> {
    let dev = fs::File::open("/dev")?;
    let client = SysconfigSyncClient::new(dev.as_raw_fd())?;

    Ok(client.read_partition(partition)?)
}

/// Write `data` to a sysconfig sub partition, if the length of `data` is smaller than the
/// partition size, the rest of the partition is filled with 0s.
pub fn write_partition(partition: SysconfigPartition, data: &[u8]) -> Result<(), std::io::Error> {
    let dev = fs::OpenOptions::new().read(true).write(true).open("/dev")?;
    let client = SysconfigSyncClient::new(dev.as_raw_fd())?;

    client.write_partition(partition, data)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_get_partition_size() {
        let fd = 42;
        let client = SysconfigSyncClient::new(fd).unwrap();
        unsafe {
            assert_eq!((*client.0).devfs_root, fd);
            let part_size = client.get_partition_size(SysconfigPartition::VerifiedBootMetadataA);
            assert_eq!((*client.0).partition_size, part_size.unwrap());
            assert_eq!((*client.0).partition, SysconfigPartition::VerifiedBootMetadataA);
        }
    }

    #[test]
    fn test_read_partition() {
        let fd = 42;
        let client = SysconfigSyncClient::new(fd).unwrap();
        unsafe {
            assert_eq!((*client.0).devfs_root, fd);
            let data = vec![7; 4096];
            sys_mock::set_data(data.clone());
            assert_eq!(
                data,
                client
                    .read_partition(SysconfigPartition::VerifiedBootMetadataB,)
                    .expect("read_partition")
            );
            assert_eq!((*client.0).partition, SysconfigPartition::VerifiedBootMetadataB);
            assert_eq!((*client.0).vmo_offset, 0);
        }
    }

    #[test]
    fn test_write_partition() {
        let fd = 42;
        let client = SysconfigSyncClient::new(fd).unwrap();
        unsafe {
            assert_eq!((*client.0).devfs_root, fd);
            let mut data = vec![1, 2, 3, 4];
            client.write_partition(SysconfigPartition::Config, &data).expect("write_partition");
            assert_eq!((*client.0).partition, SysconfigPartition::Config);
            assert_eq!((*client.0).vmo_offset, 0);

            data.resize((*client.0).partition_size as usize, 0);
            assert_eq!(sys_mock::get_data(), data);
        }
    }
}
