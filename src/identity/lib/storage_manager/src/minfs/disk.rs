// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl::endpoints::{ProtocolMarker, ServerEnd},
    fidl_fuchsia_device::ControllerMarker,
    fidl_fuchsia_hardware_block::BlockMarker,
    fidl_fuchsia_hardware_block_encrypted::{DeviceManagerMarker, DeviceManagerProxy},
    fidl_fuchsia_hardware_block_partition::{PartitionMarker, PartitionProxyInterface},
    fidl_fuchsia_identity_account as faccount, fidl_fuchsia_io as fio,
    fs_management::{
        self as fs,
        filesystem::{Filesystem, ServingSingleVolumeFilesystem},
    },
    fuchsia_vfs_watcher::{WatchEvent, WatchMessage, Watcher},
    fuchsia_zircon as zx,
    futures::prelude::*,
    std::path::Path,
    thiserror::Error,
    tracing::{error, warn},
};

/// The size, in bytes of a key.
pub const KEY_LEN: usize = 32;

/// A 256-bit key.
pub type Key = [u8; KEY_LEN];

#[derive(Error, Debug)]
pub enum DiskError {
    #[error("Failed to open: {0}")]
    OpenError(#[from] fuchsia_fs::node::OpenError),
    #[error("Failed to readdir: {0}")]
    ReaddirError(#[from] fuchsia_fs::directory::Error),
    #[error("Failed during FIDL call: {0}")]
    FidlError(#[from] fidl::Error),
    #[error("Failed to read first block of partition: {0}")]
    ReadBlockHeaderFailed(#[source] zx::Status),
    #[error("Failed to get block info: {0}")]
    GetBlockInfoFailed(#[source] zx::Status),
    #[error("Block size too small for zxcrypt header")]
    BlockTooSmallForZxcryptHeader,
    #[error("Creating Watcher failed: {0}")]
    WatcherError(#[source] anyhow::Error),
    #[error("Reading from Watcher stream failed: {0}")]
    WatcherStreamError(#[source] std::io::Error),
    #[error("Failed to bind zxcrypt driver to block device: {0}")]
    BindZxcryptDriverFailed(#[source] zx::Status),
    #[error("Failed to get topological path of device: {0}")]
    GetTopologicalPathFailed(#[source] zx::Status),
    #[error("Failed to format block device with zxcrypt: {0}")]
    FailedToFormatZxcrypt(#[source] zx::Status),
    #[error("Failed to unseal zxcrypt block device: {0}")]
    FailedToUnsealZxcrypt(#[source] zx::Status),
    #[error("Failed to seal zxcrypt block device: {0}")]
    FailedToSealZxcrypt(#[source] zx::Status),
    #[error("Failed to shred zxcrypt block device: {0}")]
    FailedToShredZxcrypt(#[source] zx::Status),
    #[error("Failed to format minfs: {0}")]
    MinfsFormatError(#[source] anyhow::Error),
    #[error("Failed to serve minfs: {0}")]
    MinfsServeError(#[source] anyhow::Error),
    #[error("Failed to kill the minfs process: {0}")]
    MinfsKillError(#[from] fs::KillError),
}

impl From<DiskError> for faccount::Error {
    fn from(_: DiskError) -> Self {
        faccount::Error::Resource
    }
}

const OPEN_RW: fio::OpenFlags = fio::OpenFlags::empty()
    .union(fio::OpenFlags::RIGHT_READABLE)
    .union(fio::OpenFlags::RIGHT_WRITABLE);

// This is the 16-byte magic byte string found at the start of a valid zxcrypt partition.
// It is also defined in `//src/security/zxcrypt/volume.h` and
// `//src/lib/storage/fs_management/cpp/format.h`.
const ZXCRYPT_MAGIC: [u8; 16] = [
    0x5f, 0xe8, 0xf8, 0x00, 0xb3, 0x6d, 0x11, 0xe7, 0x80, 0x7a, 0x78, 0x63, 0x72, 0x79, 0x70, 0x74,
];

/// Given a slice representing the first block of a device, return true if this block has the
/// zxcrypt_magic as the first 16 bytes.
fn is_zxcrypt_superblock(block: &[u8]) -> Result<bool, DiskError> {
    if block.len() < 16 {
        return Err(DiskError::BlockTooSmallForZxcryptHeader);
    }
    Ok(block[0..16] == ZXCRYPT_MAGIC)
}

/// Given a partition, return true if the partition has the desired GUID
async fn partition_has_guid<T>(partition: &T, desired_guid: [u8; 16]) -> bool
where
    T: PartitionProxyInterface,
{
    match partition.get_type_guid().await {
        Err(_) => false,
        Ok((_, None)) => false,
        Ok((status, Some(guid))) => {
            if zx::Status::from_raw(status) != zx::Status::OK {
                false
            } else {
                guid.value == desired_guid
            }
        }
    }
}

/// Given a partition, return true if the partition has the desired label
async fn partition_has_label<T>(partition: &T, desired_label: &str) -> bool
where
    T: PartitionProxyInterface,
{
    match partition.get_name().await {
        Err(_) => false,
        Ok((_, None)) => false,
        Ok((status, Some(name))) => {
            if zx::Status::from_raw(status) != zx::Status::OK {
                false
            } else {
                name == desired_label
            }
        }
    }
}

/// Given a directory handle representing the root of a device tree (i.e. open handle to "/dev"),
/// open all block devices in `/dev/class/block/*` and return them as Partition instances.
async fn all_partitions(
    dev_root_dir: &fio::DirectoryProxy,
) -> Result<Vec<DevBlockPartition>, DiskError> {
    let block_dir =
        fuchsia_fs::directory::open_directory(dev_root_dir, "class/block", OPEN_RW).await?;
    let dirents = fuchsia_fs::directory::readdir(&block_dir).await?;
    let mut partitions = Vec::new();
    for child in dirents {
        match fuchsia_fs::directory::open_node_no_describe(
            &block_dir,
            &child.name,
            OPEN_RW,
            fio::MODE_TYPE_SERVICE,
        ) {
            Ok(node_proxy) => partitions.push(DevBlockPartition(Node(node_proxy))),
            Err(err) => {
                // Ignore failures to open any particular block device and just omit it from the
                // listing.
                warn!("{}", err);
            }
        }
    }
    Ok(partitions)
}

/// Blocks until an entry with the name `filename` is present in `directory_proxy`.
/// If a timeout is desired, use the [`fuchsia_async::TimeoutExt::on_timeout()`] method.
async fn wait_for_node(
    directory_proxy: &fio::DirectoryProxy,
    filename: &str,
) -> Result<(), DiskError> {
    let needle = Path::new(filename);
    let mut watcher =
        Watcher::new(Clone::clone(directory_proxy)).await.map_err(DiskError::WatcherError)?;
    while let Some(WatchMessage { event, filename }) =
        watcher.try_next().await.map_err(DiskError::WatcherStreamError)?
    {
        match event {
            WatchEvent::ADD_FILE | WatchEvent::EXISTING if filename == needle => return Ok(()),
            _ => {}
        }
    }
    unreachable!("Watcher never ends")
}

/// The `DiskManager` trait allows for operating on block devices and partitions.
///
/// This trait exists as a way to abstract disk operations for easy mocking/testing.
/// There is only one production implementation, [`DevDiskManager`].
#[async_trait]
pub trait DiskManager {
    type BlockDevice;
    type Partition: Partition<BlockDevice = Self::BlockDevice>;
    type EncryptedBlockDevice: EncryptedBlockDevice<BlockDevice = Self::BlockDevice>;
    type Minfs: Minfs;

    /// Returns a list of all block devices that are valid partitions.
    async fn partitions(&self) -> Result<Vec<Self::Partition>, DiskError>;

    /// Given a block device, query the block size, and return if the contents of the first block
    /// contain the zxcrypt magic bytes
    async fn has_zxcrypt_header(&self, block_dev: &Self::BlockDevice) -> Result<bool, DiskError>;

    /// Bind the zxcrypt driver to the given block device, returning an encrypted block device.
    async fn bind_to_encrypted_block(
        &self,
        block_dev: Self::BlockDevice,
    ) -> Result<Self::EncryptedBlockDevice, DiskError>;

    /// Format the minfs filesystem onto a block device.
    async fn format_minfs(&self, block_dev: &Self::BlockDevice) -> Result<(), DiskError>;

    /// Serves the minfs filesystem on the given block device.
    async fn serve_minfs(&self, block_dev: Self::BlockDevice) -> Result<Self::Minfs, DiskError>;
}

/// The `Partition` trait provides a narrow interface for
/// [`Partition`][fidl_fuchsia_hardware_block_partition::PartitionProxy] operations.
#[async_trait]
pub trait Partition {
    type BlockDevice;

    /// Checks if the partition has the desired GUID.
    async fn has_guid(&self, desired_guid: [u8; 16]) -> Result<bool, DiskError>;

    /// Checks if the partition has the desired label.
    async fn has_label(&self, desired_label: &str) -> Result<bool, DiskError>;

    /// Consumes the `Partition` and returns the underlying block device.
    fn into_block_device(self) -> Self::BlockDevice;
}

/// The `EncryptedBlockDevice` trait provides a narrow interface for
/// [`DeviceManager`][fidl_fuchsia_hardware_block_encrypted::DeviceManagerProxy].
#[async_trait]
pub trait EncryptedBlockDevice: Send + 'static {
    type BlockDevice;

    /// Unseals the block device using the given key. The key must be 256 bits long.
    /// Returns a decrypted block device on success.
    async fn unseal(&self, key: &Key) -> Result<Self::BlockDevice, DiskError>;

    /// Seals the block device.
    async fn seal(&self) -> Result<(), DiskError>;

    /// Re-encrypts the block device using the given key, wiping out any previous zxcrypt volumes.
    /// The key must be 256 bits long.
    async fn format(&self, key: &Key) -> Result<(), DiskError>;

    /// Destroy the contents of the block device by destroying wrapped keys stored in all copies of
    /// the superblock. After calling `shred()`, subsequent `unseal()` calls will fail until the
    /// device is `format()`ed again.
    async fn shred(&self) -> Result<(), DiskError>;
}

#[async_trait]
pub trait Minfs: Send + 'static {
    /// Returns the root directory of the minfs instance.
    fn root_dir(&self) -> &fio::DirectoryProxy;

    /// Shutdown the serving minfs instance.
    async fn shutdown(self);
}

/// The production implementation of [`DiskManager`].
pub struct DevDiskManager {
    /// The /dev directory to use as the root for all device paths.
    dev_root: fio::DirectoryProxy,
}

impl DevDiskManager {
    /// Creates a new [`DevDiskManager`] with `dev_root` as the root for
    /// all device paths. Typically this is the "/dev" directory.
    pub fn new(dev_root: fio::DirectoryProxy) -> Self {
        Self { dev_root }
    }
}

#[async_trait]
impl DiskManager for DevDiskManager {
    type BlockDevice = DevBlockDevice;
    type Partition = DevBlockPartition;
    type EncryptedBlockDevice = EncryptedDevBlockDevice;
    type Minfs = DevMinfs;

    async fn partitions(&self) -> Result<Vec<Self::Partition>, DiskError> {
        all_partitions(&self.dev_root).await
    }

    async fn has_zxcrypt_header(&self, block_dev: &Self::BlockDevice) -> Result<bool, DiskError> {
        let superblock = block_dev.read_first_block().await?;
        is_zxcrypt_superblock(&superblock)
    }

    async fn bind_to_encrypted_block(
        &self,
        block_dev: Self::BlockDevice,
    ) -> Result<Self::EncryptedBlockDevice, DiskError> {
        let node = block_dev.0;
        let block_path = {
            let controller = node.clone_as::<ControllerMarker>()?;
            // Bind the zxcrypt driver to the block device, which will result in
            // a zxcrypt subdirectory appearing under the block.
            match controller.bind("zxcrypt.so").await?.map_err(zx::Status::from_raw) {
                Ok(()) | Err(zx::Status::ALREADY_BOUND) => {}
                Err(s) => return Err(DiskError::BindZxcryptDriverFailed(s)),
            }
            // The block device appears in /dev/class/block as a convenience. Find the actual
            // location of the device in the device topology.
            let full_path = controller
                .get_topological_path()
                .await?
                .map_err(|s| DiskError::GetTopologicalPathFailed(zx::Status::from_raw(s)))?;
            // Strip the '/dev/' prefix, as the disk_manager always opens paths relative to a
            // `dev_root` (/dev on production, a fake VFS in test).
            full_path
                .strip_prefix("/dev/")
                .expect("block topological path has /dev/ prefix")
                .to_string()
        };

        // Open the block device at its topological path. We always open rw because services are
        // opened with rw.
        let block_dir = fuchsia_fs::directory::open_directory_no_describe(
            &self.dev_root,
            &block_path,
            OPEN_RW,
        )?;

        // Wait for the zxcrypt subdirectory to appear, meaning the zxcrypt driver is loaded.
        wait_for_node(&block_dir, "zxcrypt").await?;

        // In order to open the zxcrypt directory as either a directory or a service,
        // the EncryptedDevBlockDevice needs to operate on its parent directory (block_dir).
        Ok(EncryptedDevBlockDevice(block_dir))
    }

    async fn format_minfs(&self, block_dev: &Self::BlockDevice) -> Result<(), DiskError> {
        let node = block_dev.0.clone_as::<fio::NodeMarker>()?;
        let mut minfs = Filesystem::from_node(node, fs::Minfs::default());
        minfs.format().await.map_err(DiskError::MinfsFormatError)?;
        Ok(())
    }

    async fn serve_minfs(&self, block_dev: Self::BlockDevice) -> Result<Self::Minfs, DiskError> {
        let mut minfs = Filesystem::from_node(block_dev.0 .0, fs::Minfs::default());
        let serving_fs = minfs.serve().await.map_err(DiskError::MinfsServeError)?;
        Ok(DevMinfs { serving_fs })
    }
}

/// A convenience wrapper around a NodeProxy.
struct Node(fio::NodeProxy);

impl Node {
    /// Clones the connection to the node and casts it as the protocol `T`.
    pub fn clone_as<T: ProtocolMarker>(&self) -> Result<T::Proxy, DiskError> {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<T>()?;
        self.0.clone(OPEN_RW, ServerEnd::new(server_end.into_channel()))?;
        Ok(proxy)
    }
}

/// A production device block.
pub struct DevBlockDevice(Node);

impl DevBlockDevice {
    async fn read_first_block(&self) -> Result<Vec<u8>, DiskError> {
        let block_size = self.block_size().await?;
        let file_proxy = self.0.clone_as::<fio::FileMarker>()?;
        // Issue a read of block_size bytes, since block devices only like being read along block
        // boundaries.
        file_proxy
            .read_at(block_size, 0)
            .await?
            .map_err(zx::Status::from_raw)
            .map_err(DiskError::ReadBlockHeaderFailed)
    }

    async fn block_size(&self) -> Result<u64, DiskError> {
        let block_proxy = self.0.clone_as::<BlockMarker>()?;
        let resp = block_proxy.get_info().await?;
        zx::Status::ok(resp.0).map_err(DiskError::GetBlockInfoFailed)?;
        let block_size =
            resp.1.ok_or_else(|| DiskError::GetBlockInfoFailed(zx::Status::NOT_FOUND))?.block_size
                as u64;
        Ok(block_size)
    }
}

/// The production implementation of [`Partition`].
pub struct DevBlockPartition(Node);

#[async_trait]
impl Partition for DevBlockPartition {
    type BlockDevice = DevBlockDevice;

    async fn has_guid(&self, desired_guid: [u8; 16]) -> Result<bool, DiskError> {
        let partition_proxy = self.0.clone_as::<PartitionMarker>()?;
        Ok(partition_has_guid(&partition_proxy, desired_guid).await)
    }

    async fn has_label(&self, desired_label: &str) -> Result<bool, DiskError> {
        let partition_proxy = self.0.clone_as::<PartitionMarker>()?;
        Ok(partition_has_label(&partition_proxy, desired_label).await)
    }

    fn into_block_device(self) -> Self::BlockDevice {
        DevBlockDevice(self.0)
    }
}

/// The production implementation of [`EncryptedBlockDevice`].
pub struct EncryptedDevBlockDevice(fio::DirectoryProxy);

fn get_device_manager_proxy(
    dev_block_device: &EncryptedDevBlockDevice,
) -> Result<DeviceManagerProxy, DiskError> {
    let (device_manager_proxy, server_end) =
        fidl::endpoints::create_proxy::<DeviceManagerMarker>()?;
    dev_block_device.0.open(
        OPEN_RW,
        fio::MODE_TYPE_SERVICE,
        "zxcrypt",
        ServerEnd::new(server_end.into_channel()),
    )?;
    Ok(device_manager_proxy)
}

#[async_trait]
impl EncryptedBlockDevice for EncryptedDevBlockDevice {
    type BlockDevice = DevBlockDevice;

    async fn unseal(&self, key: &Key) -> Result<Self::BlockDevice, DiskError> {
        let device_manager_proxy = get_device_manager_proxy(self)?;
        zx::Status::ok(device_manager_proxy.unseal(key, 0).await?)
            .map_err(DiskError::FailedToUnsealZxcrypt)?;

        let zxcrypt_dir =
            fuchsia_fs::directory::open_directory(&self.0, "zxcrypt", OPEN_RW).await?;

        wait_for_node(&zxcrypt_dir, "unsealed").await?;
        let unsealed_dir =
            fuchsia_fs::directory::open_directory(&zxcrypt_dir, "unsealed", OPEN_RW).await?;

        wait_for_node(&unsealed_dir, "block").await?;
        let unsealed_block_node = fuchsia_fs::directory::open_node_no_describe(
            &unsealed_dir,
            "block",
            OPEN_RW,
            fio::MODE_TYPE_SERVICE,
        )?;
        Ok(DevBlockDevice(Node(unsealed_block_node)))
    }

    async fn seal(&self) -> Result<(), DiskError> {
        let device_manager_proxy = get_device_manager_proxy(self)?;
        zx::Status::ok(device_manager_proxy.seal().await?)
            .map_err(DiskError::FailedToSealZxcrypt)?;
        Ok(())
    }

    async fn format(&self, key: &Key) -> Result<(), DiskError> {
        let device_manager_proxy = get_device_manager_proxy(self)?;
        zx::Status::ok(device_manager_proxy.format(key, 0).await?)
            .map_err(DiskError::FailedToFormatZxcrypt)?;
        Ok(())
    }

    async fn shred(&self) -> Result<(), DiskError> {
        let device_manager_proxy = get_device_manager_proxy(self)?;
        zx::Status::ok(device_manager_proxy.shred().await?)
            .map_err(DiskError::FailedToShredZxcrypt)?;
        Ok(())
    }
}

/// Production implementation of Minfs.
pub struct DevMinfs {
    serving_fs: ServingSingleVolumeFilesystem,
}

#[async_trait]
impl Minfs for DevMinfs {
    fn root_dir(&self) -> &fio::DirectoryProxy {
        self.serving_fs.root()
    }

    async fn shutdown(self) {
        if let Err(err) = self.serving_fs.shutdown().await {
            error!("failed to shutdown minfs: {}", err);
        }
    }
}

pub mod testing {
    use crate::minfs::disk::Minfs;
    use async_trait::async_trait;
    use fidl::endpoints::ServerEnd;
    use fidl_fuchsia_io as fio;
    use vfs::{directory::entry::DirectoryEntry, execution_scope::ExecutionScope};

    #[derive(Debug, Clone)]
    pub struct MockMinfs(fio::DirectoryProxy);

    impl MockMinfs {
        pub fn simple(scope: ExecutionScope) -> Self {
            let (proxy, server_end) =
                fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
            vfs::directory::mutable::simple().open(
                scope,
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                fio::MODE_TYPE_DIRECTORY,
                vfs::path::Path::dot(),
                ServerEnd::new(server_end.into_channel()),
            );
            MockMinfs(proxy)
        }
    }

    #[async_trait]
    impl Minfs for MockMinfs {
        fn root_dir(&self) -> &fio::DirectoryProxy {
            &self.0
        }

        async fn shutdown(self) {}
    }

    impl Default for MockMinfs {
        fn default() -> Self {
            let scope = ExecutionScope::build()
                .entry_constructor(vfs::directory::mutable::simple::tree_constructor(
                    |_parent, _name| {
                        Ok(vfs::file::vmo::read_write(
                            vfs::file::vmo::simple_init_vmo_with_capacity(&[], 100),
                        ))
                    },
                ))
                .new();
            Self::simple(scope)
        }
    }
}

#[cfg(test)]
pub mod test {
    use {
        super::*,
        crate::minfs::constants::{ACCOUNT_LABEL, FUCHSIA_DATA_GUID},
        assert_matches::assert_matches,
        fidl_fuchsia_hardware_block::{BlockInfo, MAX_TRANSFER_UNBOUNDED},
        fidl_fuchsia_hardware_block_encrypted::{DeviceManagerRequest, DeviceManagerRequestStream},
        fidl_fuchsia_hardware_block_partition::Guid,
        fidl_test_identity::{
            MockPartitionMarker, MockPartitionRequest, MockPartitionRequestStream,
        },
        futures::{future::BoxFuture, lock::Mutex},
        std::cell::Cell,
        std::sync::Arc,
        vfs::{
            directory::{
                entry::{DirectoryEntry, EntryInfo},
                helper::DirectlyMutable,
                immutable::simple,
            },
            execution_scope::ExecutionScope,
            path::Path as VfsPath,
            pseudo_directory,
        },
    };

    // We have precomputed the key produced by the fixed salt and params (see
    // src/identity/bin/password_authenticator/src/scrypt.rs) so that each test
    // that wants to use one doesn't need to perform an additional key
    // derivation every single time.
    pub const TEST_SCRYPT_KEY: [u8; KEY_LEN] = [
        88, 91, 129, 123, 173, 34, 21, 1, 23, 147, 87, 189, 56, 149, 89, 132, 210, 235, 150, 102,
        129, 93, 202, 53, 115, 170, 162, 217, 254, 115, 216, 181,
    ];

    const BLOCK_SIZE: usize = 4096;
    const DATA_GUID: Guid = Guid { value: FUCHSIA_DATA_GUID };
    const BLOB_GUID: Guid = Guid {
        value: [
            0x0e, 0x38, 0x67, 0x29, 0x4c, 0x13, 0xbb, 0x4c, 0xb6, 0xda, 0x17, 0xe7, 0xce, 0x1c,
            0xa4, 0x5d,
        ],
    };

    // Creates a Vec<u8> starting with zxcrypt magic and filled up to block_size with zeroes.
    // This doesn't include any keyslots or version information, just the magic bytes.
    pub fn make_zxcrypt_superblock(block_size: usize) -> Vec<u8> {
        [ZXCRYPT_MAGIC.to_vec(), [0].repeat(block_size - ZXCRYPT_MAGIC.len())].concat()
    }

    /// A mock [`Partition`] that can control the result of certain operations.
    pub struct MockPartition {
        /// Controls whether the `get_type_guid` call succeeds.
        guid: Result<Guid, i32>,
        /// Controls whether the `get_name` call succeeds.
        label: Result<String, i32>,
        /// Controls whether reading the first block of data succeeds.
        first_block: Result<Vec<u8>, i32>,
        /// A reference to the block device directory hosted at the
        /// topological path of the block device. This is used to append
        /// the "zxcrypt" directory to simulate the zxcrypt driver being
        /// bound.
        block_dir: Arc<simple::Simple>,
    }

    impl MockPartition {
        /// Handles the requests for a given RequestStream. In order to simulate the devhost
        /// block device's ability to multiplex fuchsia.io protocols with block protocols,
        /// we use a custom FIDL protocol that composes all the relevant protocols.
        pub fn handle_requests_for_stream(
            self: Arc<Self>,
            scope: ExecutionScope,
            id: u64,
            mut stream: MockPartitionRequestStream,
        ) -> BoxFuture<'static, ()> {
            Box::pin(async move {
                while let Some(request) = stream.try_next().await.expect("failed to read request") {
                    match request {
                        // fuchsia.hardware.block.partition.Partition methods
                        MockPartitionRequest::GetTypeGuid { responder } => {
                            match &self.guid {
                                Ok(guid) => responder.send(0, Some(&mut guid.clone())),
                                Err(raw_status) => responder.send(*raw_status, None),
                            }
                            .expect("failed to send Partition.GetTypeGuid response");
                        }
                        MockPartitionRequest::GetName { responder } => {
                            match &self.label {
                                Ok(label) => responder.send(0, Some(label)),
                                Err(raw_status) => responder.send(*raw_status, None),
                            }
                            .expect("failed to send Partition.GetName response");
                        }

                        // fuchsia.hardware.block.Block methods
                        MockPartitionRequest::GetInfo { responder } => {
                            responder
                                .send(
                                    0,
                                    Some(&mut BlockInfo {
                                        block_count: 1,
                                        block_size: BLOCK_SIZE as u32,
                                        max_transfer_size: MAX_TRANSFER_UNBOUNDED,
                                        flags: 0,
                                        reserved: 0,
                                    }),
                                )
                                .expect("failed to send Block.GetInfo response");
                        }

                        // fuchsia.device.Controller methods
                        MockPartitionRequest::GetTopologicalPath { responder } => {
                            responder
                                .send(&mut Ok(format!("/dev/mocks/{}", id)))
                                .expect("failed to send Controller.GetTopologicalPath response");
                        }

                        MockPartitionRequest::Bind { driver, responder } => {
                            assert_eq!(driver, "zxcrypt.so");
                            let zxcrypt_dir = simple::simple();
                            let mut resp = self
                                .block_dir
                                .add_entry(
                                    "zxcrypt",
                                    DirectoryOrService::new(
                                        zxcrypt_dir.clone(),
                                        vfs::service::host(move |stream| {
                                            let zxcrypt_dir = zxcrypt_dir.clone();
                                            async move {
                                                Arc::new(MockZxcryptBlock::new(zxcrypt_dir))
                                                    .handle_requests_for_stream(stream)
                                                    .await
                                            }
                                        }),
                                    ),
                                )
                                .map_err(|_| zx::Status::ALREADY_BOUND.into_raw());

                            responder
                                .send(&mut resp)
                                .expect("failed to send Controller.Bind response");
                        }

                        // fuchsia.io.File methods
                        MockPartitionRequest::ReadAt { count, offset, responder } => {
                            // All reads should be of block size.
                            assert_eq!(
                                count as usize, BLOCK_SIZE,
                                "all reads must be of block size"
                            );

                            // Only the first
                            assert_eq!(offset, 0, "only the first block should be read");

                            if let Ok(data) = &self.first_block {
                                assert_eq!(
                                    data.len(),
                                    BLOCK_SIZE,
                                    "mock block data must be of size BLOCK_SIZE"
                                );
                            }
                            // Silly clone required because FIDL bindings want mutability.
                            responder
                                .send(&mut self.first_block.clone())
                                .expect("failed to send File.ReadAt response");
                        }

                        // fuchsia.io.Node methods
                        MockPartitionRequest::Clone { flags, object, control_handle: _ } => {
                            assert_eq!(flags, OPEN_RW);
                            let stream =
                                ServerEnd::<MockPartitionMarker>::new(object.into_channel())
                                    .into_stream()
                                    .unwrap();
                            scope.spawn(Arc::clone(&self).handle_requests_for_stream(
                                scope.clone(),
                                id,
                                stream,
                            ));
                        }
                        req => {
                            error!("{:?} is not implemented for this mock", req);
                            unimplemented!(
                                "MockPartition request is not implemented for this mock"
                            );
                        }
                    }
                }
            })
        }
    }

    #[derive(Copy, Clone, Debug)]
    enum MockZxcryptBlockState {
        Unformatted,
        Sealed,
        Unsealed,
        UnsealedShredded,
        Shredded,
    }

    /// A mock zxcrypt block device, serving a stream of [`DeviceManagerRequest`].
    /// The block device takes a pseudo-directory in which it populates the "unsealed" entry
    /// when the device is unsealed.
    struct MockZxcryptBlock {
        unsealed_contents: Arc<simple::Simple>,
        state: Arc<Mutex<Cell<MockZxcryptBlockState>>>,
    }

    impl MockZxcryptBlock {
        fn new(unsealed_contents: Arc<simple::Simple>) -> MockZxcryptBlock {
            MockZxcryptBlock {
                unsealed_contents,
                state: Arc::new(Mutex::new(Cell::new(MockZxcryptBlockState::Unformatted))),
            }
        }

        async fn handle_requests_for_stream(
            self: Arc<Self>,
            mut stream: DeviceManagerRequestStream,
        ) {
            while let Some(request) =
                stream.try_next().await.expect("failed to read DeviceManager request")
            {
                match request {
                    DeviceManagerRequest::Format { key: _, slot, responder } => {
                        assert_eq!(slot, 0, "key slot must be 0");
                        let s = self.state.lock().await;
                        assert!(
                            matches!(
                                s.get(),
                                MockZxcryptBlockState::Unformatted
                                    | MockZxcryptBlockState::Shredded
                                    | MockZxcryptBlockState::Sealed
                            ),
                            "{:?}",
                            s.get()
                        );
                        s.replace(MockZxcryptBlockState::Sealed);
                        responder
                            .send(zx::Status::OK.into_raw())
                            .expect("failed to send DeviceManager.Format response");
                    }

                    DeviceManagerRequest::Unseal { key: _, slot, responder } => {
                        assert_eq!(slot, 0, "key slot must be 0");

                        let s = self.state.lock().await;
                        assert!(matches!(s.get(), MockZxcryptBlockState::Sealed), "{:?}", s.get());

                        let unsealed_dir = pseudo_directory! {
                            "block" => pseudo_directory! {},
                        };
                        self.unsealed_contents
                            .add_entry("unsealed", unsealed_dir)
                            .expect("failed to add unsealed dir");
                        s.replace(MockZxcryptBlockState::Unsealed);
                        responder
                            .send(zx::Status::OK.into_raw())
                            .expect("failed to send DeviceManager.Unseal response");
                    }

                    DeviceManagerRequest::Seal { responder } => {
                        let s = self.state.lock().await;
                        assert!(
                            matches!(
                                s.get(),
                                MockZxcryptBlockState::Unsealed
                                    | MockZxcryptBlockState::UnsealedShredded
                            ),
                            "{:?}",
                            s.get()
                        );

                        self.unsealed_contents
                            .remove_entry("unsealed", true)
                            .expect("failed to remove unsealed dir");
                        let next_state = match s.get() {
                            MockZxcryptBlockState::Unsealed => MockZxcryptBlockState::Sealed,
                            MockZxcryptBlockState::UnsealedShredded => {
                                MockZxcryptBlockState::Shredded
                            }
                            _ => unreachable!(),
                        };
                        s.replace(next_state);
                        responder
                            .send(zx::Status::OK.into_raw())
                            .expect("failed to send DeviceManager.Seal response");
                    }

                    DeviceManagerRequest::Shred { responder } => {
                        let s = self.state.lock().await;
                        let next_state = match s.get() {
                            MockZxcryptBlockState::Unformatted
                            | MockZxcryptBlockState::Sealed
                            | MockZxcryptBlockState::Shredded => MockZxcryptBlockState::Shredded,
                            MockZxcryptBlockState::Unsealed
                            | MockZxcryptBlockState::UnsealedShredded => {
                                MockZxcryptBlockState::UnsealedShredded
                            }
                        };
                        s.replace(next_state);
                        responder
                            .send(zx::Status::OK.into_raw())
                            .expect("failed to send DeviceManager.Shred response");
                    }
                }
            }
        }

        async fn state(&self) -> MockZxcryptBlockState {
            self.state.lock().await.get()
        }
    }

    /// A [`DirectoryEntry`] implementation that serves `directory` when opened with
    /// `MODE_TYPE_DIRECTORY`, and `service` otherwise. This is useful for mocking the behavior of
    /// block devices in devhost.
    struct DirectoryOrService {
        directory: Arc<dyn DirectoryEntry>,
        service: Arc<dyn DirectoryEntry>,
    }

    impl DirectoryOrService {
        fn new(directory: Arc<dyn DirectoryEntry>, service: Arc<dyn DirectoryEntry>) -> Arc<Self> {
            Arc::new(Self { directory, service })
        }
    }

    impl DirectoryEntry for DirectoryOrService {
        fn open(
            self: Arc<Self>,
            scope: ExecutionScope,
            flags: fio::OpenFlags,
            mode: u32,
            path: VfsPath,
            server_end: ServerEnd<fio::NodeMarker>,
        ) {
            if mode & fio::MODE_TYPE_DIRECTORY != 0 {
                self.directory.clone().open(scope, flags, mode, path, server_end);
            } else {
                self.service.clone().open(scope, flags, mode, path, server_end);
            }
        }

        fn entry_info(&self) -> EntryInfo {
            EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)
        }
    }

    fn host_mock_partition(
        scope: &ExecutionScope,
        id: u64,
        mock: MockPartition,
    ) -> Arc<vfs::service::Service> {
        let scope = scope.clone();
        let mock = Arc::new(mock);
        vfs::service::host(move |stream| {
            mock.clone().handle_requests_for_stream(scope.clone(), id, stream)
        })
    }

    /// Serves the pseudo-directory `mock_devfs` asynchronously and returns a proxy to it.
    fn serve_mock_devfs(
        scope: &ExecutionScope,
        mock_devfs: Arc<dyn DirectoryEntry>,
    ) -> fio::DirectoryProxy {
        let (dev_root, server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        mock_devfs.open(
            scope.clone(),
            OPEN_RW,
            fio::MODE_TYPE_DIRECTORY,
            VfsPath::dot(),
            ServerEnd::new(server_end.into_channel()),
        );
        dev_root
    }

    #[fuchsia::test]
    async fn lists_partitions() {
        let scope = ExecutionScope::new();
        let mock_devfs = pseudo_directory! {
            "class" => pseudo_directory! {
                "block" => pseudo_directory! {
                    "000" => host_mock_partition(&scope, 0, MockPartition {
                            guid: Ok(BLOB_GUID),
                            label: Ok("other".to_string()),
                            first_block: Ok(make_zxcrypt_superblock(BLOCK_SIZE)),
                            block_dir: simple::simple(),
                    }),
                    "001" => host_mock_partition(&scope, 1, MockPartition {
                            guid: Ok(DATA_GUID),
                            label: Ok(ACCOUNT_LABEL.to_string()),
                            first_block: Ok(make_zxcrypt_superblock(BLOCK_SIZE)),
                            block_dir: simple::simple(),
                    }),
                }
            }
        };
        let disk_manager = DevDiskManager::new(serve_mock_devfs(&scope, mock_devfs));
        let partitions = disk_manager.partitions().await.expect("list partitions");

        assert_eq!(partitions.len(), 2);
        assert!(partitions[0].has_guid(BLOB_GUID.value).await.expect("has_guid"));
        assert!(partitions[0].has_label("other").await.expect("has_label"));

        assert!(partitions[1].has_guid(DATA_GUID.value).await.expect("has_guid"));
        assert!(partitions[1].has_label(ACCOUNT_LABEL).await.expect("has_label"));

        scope.shutdown();
        scope.wait().await;
    }

    #[fuchsia::test]
    async fn lists_partitions_empty() {
        let scope = ExecutionScope::new();
        let mock_devfs = pseudo_directory! {
            "class" => pseudo_directory! {
                "block" => pseudo_directory! {},
            }
        };
        let disk_manager = DevDiskManager::new(serve_mock_devfs(&scope, mock_devfs));
        let partitions = disk_manager.partitions().await.expect("list partitions");
        assert_eq!(partitions.len(), 0);

        scope.shutdown();
        scope.wait().await;
    }

    #[fuchsia::test]
    async fn has_zxcrypt_header() {
        let scope = ExecutionScope::new();
        let mock_devfs = pseudo_directory! {
            "class" => pseudo_directory! {
                "block" => pseudo_directory! {
                    "000" => host_mock_partition(&scope, 0, MockPartition {
                        guid: Ok(BLOB_GUID),
                        label: Ok("other".to_string()),
                        first_block: Ok([0].repeat(BLOCK_SIZE)),
                        block_dir: simple::simple(),
                    }),
                    "001" => host_mock_partition(&scope, 1, MockPartition {
                        guid: Ok(DATA_GUID),
                        label: Ok(ACCOUNT_LABEL.to_string()),
                        first_block: Ok(make_zxcrypt_superblock(BLOCK_SIZE)),
                        block_dir: simple::simple(),
                    }),
                    "002" => host_mock_partition(&scope, 2, MockPartition {
                        guid: Ok(DATA_GUID),
                        label: Ok(ACCOUNT_LABEL.to_string()),
                        first_block: Err(zx::Status::NOT_FOUND.into_raw()),
                        block_dir: simple::simple(),
                    }),
                }
            }
        };
        let disk_manager = DevDiskManager::new(serve_mock_devfs(&scope, mock_devfs));
        let partitions = disk_manager.partitions().await.expect("list partitions");
        let mut partition_iter = partitions.into_iter();

        let non_zxcrypt_block =
            partition_iter.next().expect("expected first partition").into_block_device();
        assert_matches!(disk_manager.has_zxcrypt_header(&non_zxcrypt_block).await, Ok(false));

        let zxcrypt_block =
            partition_iter.next().expect("expected second partition").into_block_device();
        assert_matches!(disk_manager.has_zxcrypt_header(&zxcrypt_block).await, Ok(true));

        let bad_block =
            partition_iter.next().expect("expected third partition").into_block_device();
        assert_matches!(disk_manager.has_zxcrypt_header(&bad_block).await, Err(_));

        scope.shutdown();
        scope.wait().await;
    }

    #[fuchsia::test]
    async fn binds_zxcrypt_to_block_device() {
        let scope = ExecutionScope::new();
        let block_dir = simple::simple();
        let mock_devfs = pseudo_directory! {
            "class" => pseudo_directory! {
                "block" => pseudo_directory! {
                    "000" => host_mock_partition(&scope, 0, MockPartition {
                        guid: Ok(DATA_GUID),
                        label: Ok(ACCOUNT_LABEL.to_string()),
                        first_block: Ok(make_zxcrypt_superblock(BLOCK_SIZE)),
                        block_dir: block_dir.clone(),
                    }),
                }
            },
            "mocks" => pseudo_directory! {
                "0" => block_dir,
            }
        };
        let disk_manager = DevDiskManager::new(serve_mock_devfs(&scope, mock_devfs));
        let mut partitions = disk_manager.partitions().await.expect("list partitions");
        let block_device = partitions.remove(0).into_block_device();
        let _ = disk_manager
            .bind_to_encrypted_block(block_device)
            .await
            .expect("bind_to_encrypted_block");

        scope.shutdown();
        scope.wait().await;
    }

    #[fuchsia::test]
    async fn binds_zxcrypt_to_block_device_twice() {
        let scope = ExecutionScope::new();
        let block_dir = simple::simple();
        let mock_devfs = pseudo_directory! {
            "class" => pseudo_directory! {
                "block" => pseudo_directory! {
                    "000" => host_mock_partition(&scope, 0, MockPartition {
                        guid: Ok(DATA_GUID),
                        label: Ok(ACCOUNT_LABEL.to_string()),
                        first_block: Ok(make_zxcrypt_superblock(BLOCK_SIZE)),
                        block_dir: block_dir.clone(),
                    }),
                }
            },
            "mocks" => pseudo_directory! {
                "0" => block_dir,
            }
        };
        let disk_manager = DevDiskManager::new(serve_mock_devfs(&scope, mock_devfs));
        let mut partitions = disk_manager.partitions().await.expect("list partitions");
        let block_device = partitions.remove(0).into_block_device();
        let cloned_block =
            DevBlockDevice(Node(block_device.0.clone_as::<fio::NodeMarker>().expect("clone")));
        let _ = disk_manager
            .bind_to_encrypted_block(block_device)
            .await
            .expect("bind_to_encrypted_block");

        let _ = disk_manager
            .bind_to_encrypted_block(cloned_block)
            .await
            .expect("bind_to_encrypted_block");

        scope.shutdown();
        scope.wait().await;
    }

    #[fuchsia::test]
    async fn format_zxcrypt_device() {
        let scope = ExecutionScope::new();
        let zxcrypt_dir = simple::simple();
        let arc_mock_zxcrypt_block = Arc::new(MockZxcryptBlock::new(zxcrypt_dir.clone()));
        let arc_mock_zxcrypt_block_clone = arc_mock_zxcrypt_block.clone();

        // We only need to serve the relevant zxcrypt directories for this test.
        let mock_encrypted_block_dir = pseudo_directory! {
            "zxcrypt" => DirectoryOrService::new(
                zxcrypt_dir.clone(),
                vfs::service::host(move |stream| {
                    let arc_mock_zxcrypt_block_clone_inner = arc_mock_zxcrypt_block.clone();
                    async move {
                        arc_mock_zxcrypt_block_clone_inner
                            .handle_requests_for_stream(stream).await
                    }
                })
            ),
        };

        // Build a zxcrypt block device that points to our mock zxcrypt driver node, emulating
        // bind_to_encrypted_block.
        let key = Box::new(TEST_SCRYPT_KEY.clone());
        let encrypted_block_device =
            EncryptedDevBlockDevice(serve_mock_devfs(&scope, mock_encrypted_block_dir));
        encrypted_block_device.format(&key).await.expect("format");

        let state = arc_mock_zxcrypt_block_clone.state().await;
        assert!(matches!(state, MockZxcryptBlockState::Sealed), "{:?}", state);

        let _ = encrypted_block_device.unseal(&key).await.expect("unseal");

        let state = arc_mock_zxcrypt_block_clone.state().await;
        assert!(matches!(state, MockZxcryptBlockState::Unsealed), "{:?}", state);

        scope.shutdown();
        scope.wait().await;
    }

    #[fuchsia::test]
    async fn shred_zxcrypt_device() {
        let scope = ExecutionScope::new();
        let zxcrypt_dir = simple::simple();

        let arc_mock_zxcrypt_block = Arc::new(MockZxcryptBlock::new(zxcrypt_dir.clone()));
        let arc_mock_zxcrypt_block_clone = arc_mock_zxcrypt_block.clone();

        let mock_encrypted_block_dir = pseudo_directory! {
            "zxcrypt" => DirectoryOrService::new(
                zxcrypt_dir.clone(),
                vfs::service::host(move |stream| {
                    let arc_mock_zxcrypt_block_clone_inner = arc_mock_zxcrypt_block.clone();
                    async move {
                        arc_mock_zxcrypt_block_clone_inner
                            .handle_requests_for_stream(stream).await
                    }
                })
            ),
        };

        // Build a zxcrypt block device that points to our mock zxcrypt driver node, emulating
        // bind_to_encrypted_block.
        let key = Box::new(TEST_SCRYPT_KEY.clone());
        let encrypted_block_device =
            EncryptedDevBlockDevice(serve_mock_devfs(&scope, mock_encrypted_block_dir));

        // Format and unseal the device.
        encrypted_block_device.format(&key).await.expect("format");
        let state = arc_mock_zxcrypt_block_clone.state().await;
        assert!(matches!(state, MockZxcryptBlockState::Sealed), "{:?}", state);

        let _ = encrypted_block_device.unseal(&key).await.expect("unseal");
        let state = arc_mock_zxcrypt_block_clone.state().await;
        assert!(matches!(state, MockZxcryptBlockState::Unsealed), "{:?}", state);

        // Verify that we can also shred the device
        let _ = encrypted_block_device.shred().await.expect("shred");
        let state = arc_mock_zxcrypt_block_clone.state().await;
        assert!(matches!(state, MockZxcryptBlockState::UnsealedShredded), "{:?}", state);

        // And then seal it again
        let _ = encrypted_block_device.seal().await.expect("seal");
        let state = arc_mock_zxcrypt_block_clone.state().await;
        assert!(matches!(state, MockZxcryptBlockState::Shredded), "{:?}", state);

        // And format it again
        encrypted_block_device.format(&key).await.expect("format");
        let state = arc_mock_zxcrypt_block_clone.state().await;
        assert!(matches!(state, MockZxcryptBlockState::Sealed), "{:?}", state);

        // And shred it from Sealed
        let _ = encrypted_block_device.shred().await.expect("shred");
        let state = arc_mock_zxcrypt_block_clone.state().await;
        assert!(matches!(state, MockZxcryptBlockState::Shredded), "{:?}", state);

        scope.shutdown();
        scope.wait().await;
    }
}
