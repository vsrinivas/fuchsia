// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A safe rust wrapper for creating and using ramdisks.
//!
//! Ramdisks are, by default, placed in `/dev`. If this ramdisk is being used in a sandbox, it has
//! to have access to dev. This can be done by adding the following to the relevant .cmx file -
//!
//! ```
//! "sandbox": {
//!     "dev": [ "misc/ramctl" ]
//! }
//! ```

#![deny(missing_docs)]
#![feature()]

#[allow(bad_style)]
mod ramdevice_sys;

use {
    fdio, fuchsia_zircon as zx,
    std::{ffi, ptr},
};

/// A client for managing a ramdisk. This can be created with the [`RamdiskClient::create`] function.
pub struct RamdiskClient {
    // we own this pointer - nothing in the ramdisk library keeps it, and we don't pass it anywhere,
    // and the only valid way to get one is to have been the thing that made the ramdisk in the
    // first place.
    ramdisk: *mut ramdevice_sys::ramdisk_client_t,
}

impl RamdiskClient {
    /// Create a new ramdisk.
    pub fn create(block_size: u64, block_count: u64) -> Result<Self, zx::Status> {
        let mut ramdisk: *mut ramdevice_sys::ramdisk_client_t = ptr::null_mut();
        let status =
            unsafe { ramdevice_sys::ramdisk_create(block_size, block_count, &mut ramdisk) };
        zx::Status::ok(status)?;
        Ok(RamdiskClient { ramdisk })
    }

    /// Get the device path of the associated ramdisk.
    pub fn get_path(&self) -> &str {
        unsafe {
            let raw_path = ramdevice_sys::ramdisk_get_path(self.ramdisk);
            // We can trust this path to be valid UTF-8
            ffi::CStr::from_ptr(raw_path).to_str().expect("ramdisk path was not utf8?")
        }
    }

    /// Get an open channel to the underlying ramdevice.
    pub fn open(&self) -> Result<zx::Channel, zx::Status> {
        let (client_chan, server_chan) = zx::Channel::create()?;
        fdio::service_connect(self.get_path(), server_chan)?;
        Ok(client_chan)
    }

    /// Remove the underlying ramdisk. This deallocates all resources for this ramdisk, which will
    /// remove all data written to the associated ramdisk.
    pub fn destroy(self) -> Result<(), zx::Status> {
        // we are doing the same thing as the `Drop` impl, so tell rust not to drop it
        let status = unsafe { ramdevice_sys::ramdisk_destroy(self.ramdisk) };
        std::mem::forget(self);
        zx::Status::ok(status)
    }
}

impl Drop for RamdiskClient {
    fn drop(&mut self) {
        let _ = unsafe { ramdevice_sys::ramdisk_destroy(self.ramdisk) };
    }
}

#[cfg(test)]
mod tests {
    use {
        super::RamdiskClient,
        fidl_fuchsia_io::{NodeInfo, NodeProxy},
        fuchsia_async as fasync,
    };

    // #[test]
    #[allow(dead_code)] // TODO(FLK-375): this test is flaking. re-enable once we figure out why.
    fn create_get_path_destroy() {
        // just make sure all the functions are hooked up properly.
        let ramdisk = RamdiskClient::create(512, 2048).expect("failed to create ramdisk");
        let _path = ramdisk.get_path();
        assert_eq!(ramdisk.destroy(), Ok(()));
    }

    // #[test]
    #[allow(dead_code)] // TODO(FLK-375): this test is flaking. re-enable once we figure out why.
    fn create_write_destroy() {
        let ramdisk = RamdiskClient::create(512, 2048).expect("failed to create ramdisk");

        let device = ramdisk.open().expect("failed to open channel to ramdisk");
        // ask it to describe itself using the Node interface
        let mut executor = fasync::Executor::new().expect("failed to create executor");
        let fasync_channel =
            fasync::Channel::from_channel(device).expect("failed to convert to fasync channel");
        let proxy = NodeProxy::new(fasync_channel);
        executor.run_singlethreaded(async move {
            let info = proxy.describe().await.expect("failed to get node info");
            if let NodeInfo::Device(_) = info {
                println!("device!");
            } else {
                panic!("not a device?");
            }
        });

        assert_eq!(ramdisk.destroy(), Ok(()));
    }
}
