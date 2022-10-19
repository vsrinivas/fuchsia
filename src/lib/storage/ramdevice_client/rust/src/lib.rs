// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A safe rust wrapper for creating and using ramdisks.
//!
//! Ramdisks are, by default, placed in `/dev`. If this ramdisk is being used in a sandbox, it has
//! to have access to `/dev`. This can be done by adding the following to the relevant .cmx file -
//!
//! ```
//! "sandbox": {
//!     "dev": [ "sys/platform/00:00:2d/ramctl" ]
//! }
//! ```
//!
//! Alternatively, an isolated instance of devmgr can be used to isolate the ramdisks from the
//! system device manager. Tests can provide their own devmgr through
//! [`RamdiskClientBuilder::dev_root`] or use the pre-defined ramdisk-only isolated devmgr through
//! [`RamdiskClientBuilder::isolated_dev_root`] by depending on
//! `//src/lib/storage/ramdevice_client:ramdisk-isolated-devmgr` and adding the following to the
//! relevant test .cmx file -
//!
//! ```
//! "facets": {
//!     "fuchsia.test": {
//!         "injected-services": {
//!             "fuchsia.test.IsolatedDevmgr": "fuchsia-pkg://fuchsia.com/ramdevice-client-tests#meta/ramdisk-isolated-devmgr.cmx"
//!         }
//!     }
//! },
//! "sandbox": {
//!     "services": [
//!         "fuchsia.test.IsolatedDevmgr"
//!     ]
//! }
//! ```

#![deny(missing_docs)]

#[allow(bad_style)]
mod ramdevice_sys;

use {
    anyhow::Error,
    fdio, fuchsia_zircon as zx,
    std::{
        ffi, fs,
        os::unix::io::{AsRawFd, RawFd},
        ptr,
    },
    zx::HandleBased,
};
enum DevRoot {
    Provided(fs::File),
}

/// A type to help construct a [`RamdeviceClient`] from an existing VMO.
pub struct VmoRamdiskClientBuilder {
    vmo: zx::Vmo,
    block_size: Option<u64>,
    dev_root: Option<DevRoot>,
}

impl VmoRamdiskClientBuilder {
    /// Create a new ramdisk builder with the given VMO handle.
    pub fn new(vmo: zx::Vmo) -> Self {
        Self { vmo, block_size: None, dev_root: None }
    }

    /// Set the size of a single block (in bytes)
    pub fn block_size(mut self, block_size: u64) -> Self {
        self.block_size = Some(block_size);
        self
    }

    /// Use the given directory as "/dev" instead of opening "/dev" from the environment.
    pub fn dev_root(mut self, dev_root: fs::File) -> Self {
        self.dev_root = Some(DevRoot::Provided(dev_root));
        self
    }

    /// Create the ramdisk.
    pub fn build(self) -> Result<RamdiskClient, zx::Status> {
        let vmo_handle = self.vmo.into_raw();

        let mut ramdisk: *mut ramdevice_sys::ramdisk_client_t = ptr::null_mut();
        let status = match (&self.dev_root, &self.block_size) {
            (Some(dev_root), Some(block_size)) => {
                // If this statement needs to open the dev_root itself, hold onto the File to
                // ensure dev_root_fd is valid for this block.
                let dev_root_fd = match &dev_root {
                    DevRoot::Provided(f) => f.as_raw_fd(),
                };

                // Safe because ramdisk_create_at_from_vmo_with_block_size creates a duplicate fd
                // of the provided dev_root_fd. The returned ramdisk is valid iff the FFI method
                // returns ZX_OK.
                unsafe {
                    ramdevice_sys::ramdisk_create_at_from_vmo_with_block_size(
                        dev_root_fd,
                        vmo_handle,
                        *block_size,
                        &mut ramdisk,
                    )
                }
            }
            (Some(dev_root), None) => {
                // If this statement needs to open the dev_root itself, hold onto the File to
                // ensure dev_root_fd is valid for this block.
                let dev_root_fd = match &dev_root {
                    DevRoot::Provided(f) => f.as_raw_fd(),
                };
                // Safe because ramdisk_create_at_from_vmo creates a duplicate fd of the provided
                // dev_root_fd. The returned ramdisk is valid iff the FFI method returns ZX_OK.
                unsafe {
                    ramdevice_sys::ramdisk_create_at_from_vmo(dev_root_fd, vmo_handle, &mut ramdisk)
                }
            }
            (None, Some(block_size)) => {
                // The returned ramdisk is valid iff the FFI method returns ZX_OK.
                unsafe {
                    ramdevice_sys::ramdisk_create_from_vmo_with_block_size(
                        vmo_handle,
                        *block_size,
                        &mut ramdisk,
                    )
                }
            }
            (None, None) => {
                // The returned ramdisk is valid iff the FFI method returns ZX_OK.
                unsafe { ramdevice_sys::ramdisk_create_from_vmo(vmo_handle, &mut ramdisk) }
            }
        };
        zx::Status::ok(status)?;

        Ok(RamdiskClient { ramdisk })
    }
}

/// A type to help construct a [`RamdeviceClient`].
pub struct RamdiskClientBuilder {
    block_size: u64,
    block_count: u64,
    dev_root: Option<DevRoot>,
    guid: Option<[u8; 16]>,
}

impl RamdiskClientBuilder {
    /// Create a new ramdisk builder with the given block_size and block_count.
    pub fn new(block_size: u64, block_count: u64) -> Self {
        Self { block_size, block_count, dev_root: None, guid: None }
    }

    /// Use the given directory as "/dev" instead of opening "/dev" from the environment.
    pub fn dev_root(&mut self, dev_root: fs::File) -> &mut Self {
        self.dev_root = Some(DevRoot::Provided(dev_root));
        self
    }

    /// Initialize the ramdisk with the given GUID, which can be queried from the ramdisk instance.
    pub fn guid(&mut self, guid: [u8; 16]) -> &mut Self {
        self.guid = Some(guid);
        self
    }

    /// Create the ramdisk.
    pub fn build(&mut self) -> Result<RamdiskClient, zx::Status> {
        let block_size = self.block_size;
        let block_count = self.block_count;

        let mut ramdisk: *mut ramdevice_sys::ramdisk_client_t = ptr::null_mut();
        let status = match (&self.dev_root, &self.guid) {
            (Some(dev_root), Some(guid)) => {
                // If this statement needs to open the dev_root itself, hold onto the File to
                // ensure dev_root_fd is valid for this block.
                let dev_root_fd = match &dev_root {
                    DevRoot::Provided(f) => f.as_raw_fd(),
                };

                // Safe because ramdisk_create_at creates a duplicate fd of the provided dev_root_fd.
                // The returned ramdisk is valid iff the FFI method returns ZX_OK.
                unsafe {
                    ramdevice_sys::ramdisk_create_at_with_guid(
                        dev_root_fd,
                        block_size,
                        block_count,
                        guid.as_ptr(),
                        16,
                        &mut ramdisk,
                    )
                }
            }
            (Some(dev_root), None) => {
                // If this statement needs to open the dev_root itself, hold onto the File to
                // ensure dev_root_fd is valid for this block.
                let dev_root_fd = match &dev_root {
                    DevRoot::Provided(f) => f.as_raw_fd(),
                };
                // Safe because ramdisk_create_at creates a duplicate fd of the provided dev_root_fd.
                // The returned ramdisk is valid iff the FFI method returns ZX_OK.
                unsafe {
                    ramdevice_sys::ramdisk_create_at(
                        dev_root_fd,
                        block_size,
                        block_count,
                        &mut ramdisk,
                    )
                }
            }
            (None, Some(guid)) => {
                // The returned ramdisk is valid iff the FFI method returns ZX_OK.
                unsafe {
                    ramdevice_sys::ramdisk_create_with_guid(
                        block_size,
                        block_count,
                        guid.as_ptr(),
                        16,
                        &mut ramdisk,
                    )
                }
            }
            (None, None) => {
                // The returned ramdisk is valid iff the FFI method returns ZX_OK.
                unsafe { ramdevice_sys::ramdisk_create(block_size, block_count, &mut ramdisk) }
            }
        };
        zx::Status::ok(status)?;

        Ok(RamdiskClient { ramdisk })
    }
}

/// A client for managing a ramdisk. This can be created with the [`RamdiskClient::create`]
/// function or through the type returned by [`RamdiskClient::builder`] to specify additional
/// options.
pub struct RamdiskClient {
    // we own this pointer - nothing in the ramdisk library keeps it, and we don't pass it anywhere,
    // and the only valid way to get one is to have been the thing that made the ramdisk in the
    // first place.
    ramdisk: *mut ramdevice_sys::ramdisk_client_t,
}

impl RamdiskClient {
    /// Create a new ramdisk builder with the given block_size and block_count.
    pub fn builder(block_size: u64, block_count: u64) -> RamdiskClientBuilder {
        RamdiskClientBuilder::new(block_size, block_count)
    }

    /// Create a new ramdisk.
    pub fn create(block_size: u64, block_count: u64) -> Result<Self, zx::Status> {
        Self::builder(block_size, block_count).build()
    }

    /// Get the device path of the associated ramdisk. Note that if this ramdisk was created with a
    /// custom dev_root, the returned path will be relative to that handle.
    pub fn get_path(&self) -> &str {
        unsafe {
            let raw_path = ramdevice_sys::ramdisk_get_path(self.ramdisk);
            // We can trust this path to be valid UTF-8
            ffi::CStr::from_ptr(raw_path).to_str().expect("ramdisk path was not utf8?")
        }
    }

    /// Get an open channel to the underlying ramdevice.
    pub fn open(&self) -> Result<zx::Channel, zx::Status> {
        struct UnownedFd(RawFd);
        impl AsRawFd for UnownedFd {
            fn as_raw_fd(&self) -> RawFd {
                self.0
            }
        }

        // Safe because self.ramdisk is valid and the borrowed fd is not borrowed beyond this
        // method call.
        let fd = unsafe { ramdevice_sys::ramdisk_get_block_fd(self.ramdisk) };
        let client_chan = fdio::clone_channel(&UnownedFd(fd))?;
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

/// This struct has exclusive ownership of the ramdisk pointer.
/// It is safe to move this struct between threads.
unsafe impl Send for RamdiskClient {}

/// The only methods that can be invoked from a reference of RamdiskClient
/// are open() and get_path(). Both these functions are non-destructive and
/// can be called from multiple threads.
/// This implies that it is safe to share a reference to RamdiskClient between threads.
unsafe impl Sync for RamdiskClient {}

impl Drop for RamdiskClient {
    fn drop(&mut self) {
        let _ = unsafe { ramdevice_sys::ramdisk_destroy(self.ramdisk) };
    }
}

/// Wait for no longer than |duration| for the device at |path| to appear.
pub fn wait_for_device(path: &str, duration: std::time::Duration) -> Result<(), Error> {
    let c_path = ffi::CString::new(path)?;
    Ok(zx::Status::ok(unsafe {
        ramdevice_sys::wait_for_device(c_path.as_ptr(), duration.as_nanos() as u64)
    })?)
}

/// Wait for no longer than |duration| for the device at |path| relative to |dirfd| to appear.
pub fn wait_for_device_at(
    dirfd: &fs::File,
    path: &str,
    duration: std::time::Duration,
) -> Result<(), Error> {
    let c_path = ffi::CString::new(path)?;
    Ok(zx::Status::ok(unsafe {
        ramdevice_sys::wait_for_device_at(
            dirfd.as_raw_fd(),
            c_path.as_ptr(),
            duration.as_nanos() as u64,
        )
    })?)
}

#[cfg(test)]
mod tests {
    use {
        super::*, assert_matches::assert_matches, fidl_fuchsia_io as fio, fuchsia_async as fasync,
    };

    // Note that if these tests flake, all downstream tests that depend on this crate may too.

    const TEST_GUID: [u8; 16] = [
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
        0x10,
    ];

    const WAIT_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(10);

    #[fasync::run_singlethreaded(test)]
    async fn create_get_path_destroy() {
        wait_for_device("/dev/sys/platform/00:00:2d/ramctl", WAIT_TIMEOUT)
            .expect("ramctl did not appear");
        // just make sure all the functions are hooked up properly.
        let ramdisk = RamdiskClient::builder(512, 2048).build().expect("failed to create ramdisk");
        let _path = ramdisk.get_path();
        assert_eq!(ramdisk.destroy(), Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_with_dev_root_and_guid_get_path_destroy() {
        wait_for_device("/dev/sys/platform/00:00:2d/ramctl", WAIT_TIMEOUT)
            .expect("ramctl did not appear");
        let devroot = std::fs::File::open("/dev").unwrap();
        let ramdisk = RamdiskClient::builder(512, 2048)
            .dev_root(devroot)
            .guid(TEST_GUID)
            .build()
            .expect("failed to create ramdisk");
        let _path = ramdisk.get_path();
        assert_eq!(ramdisk.destroy(), Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_with_guid_get_path_destroy() {
        wait_for_device("/dev/sys/platform/00:00:2d/ramctl", WAIT_TIMEOUT)
            .expect("ramctl did not appear");
        let ramdisk = RamdiskClient::builder(512, 2048)
            .guid(TEST_GUID)
            .build()
            .expect("failed to create ramdisk");
        let _path = ramdisk.get_path();
        assert_eq!(ramdisk.destroy(), Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_open_destroy() {
        wait_for_device("/dev/sys/platform/00:00:2d/ramctl", WAIT_TIMEOUT)
            .expect("ramctl did not appear");
        let ramdisk = RamdiskClient::create(512, 2048).unwrap();
        assert_matches!(ramdisk.open(), Ok(_));
        assert_eq!(ramdisk.destroy(), Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn create_describe_destroy() {
        wait_for_device("/dev/sys/platform/00:00:2d/ramctl", WAIT_TIMEOUT)
            .expect("ramctl did not appear");
        let ramdisk = RamdiskClient::create(512, 2048).unwrap();
        let device = ramdisk.open().unwrap();

        // ask it to describe itself using the Node interface
        let fasync_channel =
            fasync::Channel::from_channel(device).expect("failed to convert to fasync channel");
        let proxy = fio::NodeProxy::new(fasync_channel);
        let protocol = proxy.query().await.expect("failed to get node info");
        assert_eq!(protocol, fio::FILE_PROTOCOL_NAME.as_bytes());

        assert_eq!(ramdisk.destroy(), Ok(()));
    }
}
