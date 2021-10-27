// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl::endpoints::{ProtocolMarker, ServerEnd},
    fidl_fuchsia_hardware_block::BlockMarker,
    fidl_fuchsia_hardware_block_partition::{PartitionMarker, PartitionProxyInterface},
    fidl_fuchsia_io::{
        DirectoryProxy, FileMarker, NodeProxy, MODE_TYPE_SERVICE, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon as zx,
    log::{error, warn},
    thiserror::Error,
};

#[derive(Error, Debug)]
pub enum DiskError {
    #[error("Failed to open: {0}")]
    OpenError(#[from] io_util::node::OpenError),
    #[error("Failed to readdir: {0}")]
    ReaddirError(#[from] files_async::Error),
    #[error("Failed during FIDL call: {0}")]
    FidlError(#[from] fidl::Error),
    #[error("Failed to read first block of partition: {0}")]
    ReadBlockHeaderFailed(zx::Status),
    #[error("Failed to get block info: {0}")]
    GetBlockInfoFailed(zx::Status),
    #[error("Block size too small for zxcrypt header")]
    BlockTooSmallForZxcryptHeader,
}

const OPEN_RW: u32 = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE;

// This is the 16-byte magic byte string found at the start of a valid zxcrypt partition.
// It is also defined in `//src/security/zxcrypt/volume.h` and
// `//src/lib/storage/fs_management/cpp/include/fs-management/format.h`.
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
    dev_root_dir: &DirectoryProxy,
) -> Result<Vec<DevBlockPartition>, DiskError> {
    let block_dir =
        io_util::directory::open_directory(dev_root_dir, "class/block", OPEN_RW).await?;
    let dirents = files_async::readdir(&block_dir).await?;
    let mut partitions = Vec::new();
    for child in dirents {
        match io_util::directory::open_node_no_describe(
            &block_dir,
            &child.name,
            OPEN_RW,
            MODE_TYPE_SERVICE,
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

/// The `DiskManager` trait allows for operating on block devices and partitions.
///
/// This trait exists as a way to abstract disk operations for easy mocking/testing.
/// There is only one production implementation, [`DevDiskManager`].
#[async_trait]
pub trait DiskManager {
    type BlockDevice;
    type Partition: Partition<BlockDevice = Self::BlockDevice>;

    /// Returns a list of all block devices that are valid partitions.
    async fn partitions(&self) -> Result<Vec<Self::Partition>, DiskError>;

    /// Given a block device, query the block size, and return if the contents of the first block
    /// contain the zxcrypt magic bytes
    async fn has_zxcrypt_header(&self, block_dev: &Self::BlockDevice) -> Result<bool, DiskError>;
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

/// The production implementation of [`DiskManager`].
pub struct DevDiskManager {
    /// The /dev directory to use as the root for all device paths.
    dev_root: DirectoryProxy,
}

impl DevDiskManager {
    /// Creates a new [`DevDiskManager`] with `dev_root` as the root for
    /// all device paths. Typically this is the "/dev" directory.
    pub fn new(dev_root: DirectoryProxy) -> Self {
        Self { dev_root }
    }
}

#[async_trait]
impl DiskManager for DevDiskManager {
    type BlockDevice = DevBlockDevice;
    type Partition = DevBlockPartition;

    async fn partitions(&self) -> Result<Vec<Self::Partition>, DiskError> {
        all_partitions(&self.dev_root).await
    }

    async fn has_zxcrypt_header(&self, block_dev: &Self::BlockDevice) -> Result<bool, DiskError> {
        let superblock = block_dev.read_first_block().await?;
        is_zxcrypt_superblock(&superblock)
    }
}

/// A convenience wrapper around a NodeProxy.
struct Node(NodeProxy);

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
        let file_proxy = self.0.clone_as::<FileMarker>()?;
        // Issue a read of block_size bytes, since block devices only like being read along block
        // boundaries.
        let res = file_proxy.read_at(block_size, 0).await?;
        zx::Status::ok(res.0).map_err(DiskError::ReadBlockHeaderFailed)?;
        Ok(res.1)
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

#[cfg(test)]
pub mod test {
    use {
        super::*,
        crate::constants::{ACCOUNT_LABEL, FUCHSIA_DATA_GUID},
        fidl_fuchsia_hardware_block::{BlockInfo, MAX_TRANSFER_UNBOUNDED},
        fidl_fuchsia_hardware_block_partition::Guid,
        fidl_fuchsia_io::{DirectoryMarker, MODE_TYPE_DIRECTORY},
        fidl_test_identity::{
            MockPartitionMarker, MockPartitionRequest, MockPartitionRequestStream,
        },
        futures::{future::BoxFuture, prelude::*},
        matches::assert_matches,
        std::sync::Arc,
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            path::Path as VfsPath, pseudo_directory,
        },
    };

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
    }

    impl MockPartition {
        /// Handles the requests for a given RequestStream. In order to simulate the devhost
        /// block device's ability to multiplex fuchsia.io protocols with block protocols,
        /// we use a custom FIDL protocol that composes all the relevant protocols.
        pub fn handle_requests_for_stream(
            self: Arc<Self>,
            scope: ExecutionScope,
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

                        // fuchsia.io.File methods
                        MockPartitionRequest::ReadAt { count, offset, responder } => {
                            // All reads should be of block size.
                            assert_eq!(
                                count as usize, BLOCK_SIZE,
                                "all reads must be of block size"
                            );

                            // Only the first
                            assert_eq!(offset, 0, "only the first block should be read");

                            match &self.first_block {
                                Ok(data) => {
                                    assert_eq!(
                                        data.len(),
                                        BLOCK_SIZE,
                                        "mock block data must be of size BLOCK_SIZE"
                                    );
                                    responder.send(0, data)
                                }
                                Err(s) => responder.send(*s, &[0; 0]),
                            }
                            .expect("failed to send File.ReadAt response");
                        }

                        // fuchsia.io.Node methods
                        MockPartitionRequest::Clone { flags, object, control_handle: _ } => {
                            assert_eq!(flags, OPEN_RW);
                            let stream =
                                ServerEnd::<MockPartitionMarker>::new(object.into_channel())
                                    .into_stream()
                                    .unwrap();
                            scope.spawn(
                                Arc::clone(&self).handle_requests_for_stream(scope.clone(), stream),
                            );
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

    fn host_mock_partition(
        scope: &ExecutionScope,
        mock: MockPartition,
    ) -> Arc<vfs::service::Service> {
        let scope = scope.clone();
        let mock = Arc::new(mock);
        vfs::service::host(move |stream| {
            mock.clone().handle_requests_for_stream(scope.clone(), stream)
        })
    }

    /// Serves the pseudo-directory `mock_devfs` asynchronously and returns a proxy to it.
    fn serve_mock_devfs(
        scope: &ExecutionScope,
        mock_devfs: Arc<dyn DirectoryEntry>,
    ) -> DirectoryProxy {
        let (dev_root, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        mock_devfs.open(
            scope.clone(),
            OPEN_RW,
            MODE_TYPE_DIRECTORY,
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
                    "000" => host_mock_partition(&scope, MockPartition {
                            guid: Ok(BLOB_GUID),
                            label: Ok("other".to_string()),
                            first_block: Ok(make_zxcrypt_superblock(BLOCK_SIZE)),
                    }),
                    "001" => host_mock_partition(&scope, MockPartition {
                            guid: Ok(DATA_GUID),
                            label: Ok(ACCOUNT_LABEL.to_string()),
                            first_block: Ok(make_zxcrypt_superblock(BLOCK_SIZE)),
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
                    "000" => host_mock_partition(&scope, MockPartition {
                        guid: Ok(BLOB_GUID),
                        label: Ok("other".to_string()),
                        first_block: Ok([0].repeat(BLOCK_SIZE)),
                    }),
                    "001" => host_mock_partition(&scope, MockPartition {
                        guid: Ok(DATA_GUID),
                        label: Ok(ACCOUNT_LABEL.to_string()),
                        first_block: Ok(make_zxcrypt_superblock(BLOCK_SIZE)),
                    }),
                    "002" => host_mock_partition(&scope, MockPartition {
                        guid: Ok(DATA_GUID),
                        label: Ok(ACCOUNT_LABEL.to_string()),
                        first_block: Err(zx::Status::NOT_FOUND.into_raw()),
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
}
