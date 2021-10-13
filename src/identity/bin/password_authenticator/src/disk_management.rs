// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_hardware_block::BlockMarker,
    fidl_fuchsia_hardware_block_partition::{PartitionMarker, PartitionProxyInterface},
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, FileMarker, NodeMarker, NodeProxy, MODE_TYPE_DIRECTORY,
        MODE_TYPE_SERVICE, OPEN_FLAG_DIRECTORY, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon as zx,
    log::warn,
    std::marker::PhantomData,
};

/// A trait for interacting with the universe of partitions available
#[async_trait]
pub trait PartitionManager<T>
where
    T: BlockDevice + Partition,
{
    /// Opens each block device known to this partition manager.
    async fn partitions(&self) -> Result<Vec<T>, Error>;
}

/// A PartitionManager implementation backed by a device_manager tree rooted at `dev_root_dir`.
pub struct DevPartitionManager<T> {
    /// An open fuchsia.io.Directory client for the root of the device manager tree "/dev"
    dev_root_dir: DirectoryProxy,

    /// Additional type information to allow the compiler to better handle trait implementations
    block_device_type: PhantomData<T>,
}

/// A trait abstracting over interactions with a single block device
#[async_trait]
pub trait BlockDevice {
    /// Read the first `block_size` bytes from the block device
    async fn read_first_block(&self, block_size: u64) -> Result<Vec<u8>, Error>;

    /// Query the block device for its block size, since block reads must be block-aligned
    async fn block_size(&self) -> Result<u64, Error>;
}

/// A trait abstracting over interactions with a partition.
#[async_trait]
pub trait Partition {
    /// Returns true if the partition has type GUID `desired_guid`
    async fn has_guid(&self, desired_guid: [u8; 16]) -> Result<bool, Error>;

    /// Returns true if the partition has label `desired_label`
    async fn has_label(&self, desired_label: &str) -> Result<bool, Error>;
}

/// A concrete implementation of both BlockDevice and Partition, backed by an open channel to that
/// block device from the device tree.
pub struct DevBlockDevice {
    /// A fuchsia.io.Node client backed by an open channel to the specific block device e.g.
    /// "/dev/class/block/001"
    node: NodeProxy,
}

const OPEN_RW: u32 = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE;

// This is the 16-byte magic byte string found at the start of a valid zxcrypt partition.
// It is also defined in `//src/security/zxcrypt/volume.h` and
// `//src/lib/storage/fs_management/cpp/include/fs-management/format.h`.
const ZXCRYPT_MAGIC: [u8; 16] = [
    0x5f, 0xe8, 0xf8, 0x00, 0xb3, 0x6d, 0x11, 0xe7, 0x80, 0x7a, 0x78, 0x63, 0x72, 0x79, 0x70, 0x74,
];

#[async_trait]
impl BlockDevice for DevBlockDevice {
    async fn read_first_block(&self, block_size: u64) -> Result<Vec<u8>, Error> {
        let (file_proxy, file_proxy_server) =
            fidl::endpoints::create_proxy::<FileMarker>().context("Create file client proxy")?;
        self.node
            .clone(OPEN_RIGHT_READABLE, ServerEnd::new(file_proxy_server.into_channel()))
            .context("open cloned file client channel")?;

        // Issue a read of block_size bytes, since block devices only like being read along block
        // boundaries.
        let res = file_proxy.read_at(block_size, 0).await.context("send read")?;
        zx::Status::ok(res.0).context("read header")?;
        Ok(res.1)
    }

    async fn block_size(&self) -> Result<u64, Error> {
        let (block_proxy, block_proxy_server) =
            fidl::endpoints::create_proxy::<BlockMarker>().context("Create block client proxy")?;
        self.node
            .clone(OPEN_RIGHT_READABLE, ServerEnd::new(block_proxy_server.into_channel()))
            .context("open cloned block client channel")?;
        let resp = block_proxy.get_info().await?;
        zx::Status::ok(resp.0).context("get block info")?;
        let block_size = resp.1.context("block info")?.block_size as u64;
        Ok(block_size)
    }
}

#[async_trait]
impl Partition for DevBlockDevice {
    async fn has_guid(&self, desired_guid: [u8; 16]) -> Result<bool, Error> {
        let (partition_proxy, partition_proxy_server) =
            fidl::endpoints::create_proxy::<PartitionMarker>()
                .context("Create partition client proxy")?;
        self.node
            .clone(OPEN_RIGHT_READABLE, ServerEnd::new(partition_proxy_server.into_channel()))
            .context("open cloned partition client channel")?;

        Ok(partition_has_guid(&partition_proxy, desired_guid).await)
    }

    async fn has_label(&self, desired_label: &str) -> Result<bool, Error> {
        let (partition_proxy, partition_proxy_server) =
            fidl::endpoints::create_proxy::<PartitionMarker>()
                .context("Create partition client proxy")?;
        self.node
            .clone(OPEN_RIGHT_READABLE, ServerEnd::new(partition_proxy_server.into_channel()))
            .context("open cloned partition client channel")?;

        Ok(partition_has_label(&partition_proxy, desired_label).await)
    }
}

/// Given a slice representing the first block of a device, return true if this block has the
/// zxcrypt_magic as the first 16 bytes.
fn is_zxcrypt_superblock(block: &[u8]) -> Result<bool, Error> {
    if block.len() < 16 {
        return Err(anyhow!("block too small to contain superblock"));
    }
    Ok(block[0..16] == ZXCRYPT_MAGIC)
}

/// Given a block device, query the block size, and return if the contents of the first block
/// contain the zxcrypt magic bytes
pub async fn has_zxcrypt_header<T>(block_device: &T) -> Result<bool, Error>
where
    T: BlockDevice,
{
    let block_size = block_device.block_size().await?;
    let superblock = block_device.read_first_block(block_size).await?;
    is_zxcrypt_superblock(&superblock)
}

impl<T> DevPartitionManager<T> {
    pub fn new_from_namespace() -> Result<Self, anyhow::Error> {
        let dev_root_dir = io_util::open_directory_in_namespace("/dev", OPEN_RW)?;
        Ok(Self::new(dev_root_dir))
    }

    pub fn new(dev_root_dir: DirectoryProxy) -> Self {
        DevPartitionManager { dev_root_dir, block_device_type: PhantomData }
    }
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
/// open all block devices in `/dev/class/block/*` and return them as DevBlockDevice instances.
async fn all_block_devices(dev_root_dir: &DirectoryProxy) -> Result<Vec<DevBlockDevice>, Error> {
    let (block_dir_client, block_dir_server) =
        fidl::endpoints::create_proxy::<DirectoryMarker>().context("create channel pair")?;

    dev_root_dir
        .open(
            OPEN_FLAG_DIRECTORY | OPEN_RW, // flags
            MODE_TYPE_DIRECTORY,           // mode
            "class/block",                 // path
            ServerEnd::new(block_dir_server.into_channel()),
        )
        .context("open() error")?;

    let dirents = files_async::readdir(&block_dir_client)
        .await
        .context("list children of block device dir")?;

    let mut block_devs = Vec::new();
    for child in dirents {
        let (block_client, block_server) =
            fidl::endpoints::create_proxy::<NodeMarker>().context("create Node channel pair")?;
        let open_result = block_dir_client
            .open(
                OPEN_RW,
                MODE_TYPE_SERVICE,
                &child.name,
                ServerEnd::new(block_server.into_channel()),
            )
            .with_context(|| format!("Couldn't open block device {}", &child.name));
        if let Err(err) = open_result {
            // Ignore failures to open any particular block device and just omit it from the
            // listing.
            warn!("{}", err);
            continue;
        }

        block_devs.push(DevBlockDevice { node: block_client });
    }
    Ok(block_devs)
}

#[async_trait]
impl PartitionManager<DevBlockDevice> for DevPartitionManager<DevBlockDevice> {
    async fn partitions(&self) -> Result<Vec<DevBlockDevice>, Error> {
        all_block_devices(&self.dev_root_dir).await
    }
}

#[cfg(test)]
pub mod test {
    use {
        super::*,
        crate::constants::{ACCOUNT_LABEL, FUCHSIA_DATA_GUID},
        fidl_fuchsia_hardware_block_partition::Guid,
        fuchsia_async as fasync,
        futures::future,
        matches::assert_matches,
        std::convert::TryInto,
    };

    #[derive(Clone, Debug)]
    pub struct MockPartition {
        // The guid or zx_status error to return to get_type_guid requests
        pub guid: Result<Guid, i32>,

        // The label or zx_status error to return to get_name requests
        pub label: Result<String, i32>,

        // If present, the first block of the partition.  If absent, attempts
        // to read or request the block size will return an error.
        pub first_block: Option<Vec<u8>>,
    }

    #[async_trait]
    impl BlockDevice for MockPartition {
        async fn read_first_block(&self, block_size: u64) -> Result<Vec<u8>, Error> {
            match &self.first_block {
                Some(block) => {
                    if block.len() == block_size as usize {
                        Ok(block.to_vec())
                    } else {
                        Err(anyhow!("wrong block size"))
                    }
                }
                None => Err(anyhow!("no block")),
            }
        }

        async fn block_size(&self) -> Result<u64, Error> {
            match &self.first_block {
                Some(block) => Ok(block.len().try_into()?),
                None => Err(anyhow!("no block")),
            }
        }
    }

    #[async_trait]
    impl Partition for MockPartition {
        async fn has_guid(&self, desired_guid: [u8; 16]) -> Result<bool, Error> {
            Ok(partition_has_guid(self, desired_guid).await)
        }

        async fn has_label(&self, desired_label: &str) -> Result<bool, Error> {
            Ok(partition_has_label(self, desired_label).await)
        }
    }

    impl PartitionProxyInterface for MockPartition {
        type GetTypeGuidResponseFut = future::Ready<Result<(i32, Option<Box<Guid>>), fidl::Error>>;
        fn get_type_guid(&self) -> Self::GetTypeGuidResponseFut {
            match self.guid {
                Ok(guid) => future::ok((0, Some(Box::new(guid)))),
                Err(status) => future::ok((status, None)),
            }
        }

        type GetNameResponseFut = future::Ready<Result<(i32, Option<String>), fidl::Error>>;
        fn get_name(&self) -> Self::GetNameResponseFut {
            match &self.label {
                Ok(label) => future::ok((0, Some(label.clone()))),
                Err(status) => future::ok((*status, None)),
            }
        }

        // The rest of these methods aren't called
        type GetInfoResponseFut = future::Ready<
            Result<(i32, Option<Box<fidl_fuchsia_hardware_block::BlockInfo>>), fidl::Error>,
        >;
        fn get_info(&self) -> Self::GetInfoResponseFut {
            unimplemented!()
        }

        type GetStatsResponseFut = future::Ready<
            Result<(i32, Option<Box<fidl_fuchsia_hardware_block::BlockStats>>), fidl::Error>,
        >;
        fn get_stats(&self, _clear: bool) -> Self::GetStatsResponseFut {
            unimplemented!()
        }

        type GetFifoResponseFut = future::Ready<Result<(i32, Option<fidl::Fifo>), fidl::Error>>;
        fn get_fifo(&self) -> Self::GetFifoResponseFut {
            unimplemented!()
        }

        type AttachVmoResponseFut = future::Ready<
            Result<(i32, Option<Box<fidl_fuchsia_hardware_block::VmoId>>), fidl::Error>,
        >;
        fn attach_vmo(&self, _vmo: fidl::Vmo) -> Self::AttachVmoResponseFut {
            unimplemented!()
        }

        type CloseFifoResponseFut = future::Ready<Result<i32, fidl::Error>>;
        fn close_fifo(&self) -> Self::CloseFifoResponseFut {
            unimplemented!()
        }

        type RebindDeviceResponseFut = future::Ready<Result<i32, fidl::Error>>;
        fn rebind_device(&self) -> Self::RebindDeviceResponseFut {
            unimplemented!()
        }

        type GetInstanceGuidResponseFut =
            future::Ready<Result<(i32, Option<Box<Guid>>), fidl::Error>>;
        fn get_instance_guid(&self) -> Self::GetInstanceGuidResponseFut {
            unimplemented!()
        }
    }

    pub const DATA_GUID: Guid = Guid { value: FUCHSIA_DATA_GUID };
    pub const BLOB_GUID: Guid = Guid {
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

    // A partition manager implementation backed by an optional fixed static list of partitions.
    // If no partition list is given, partitions() (from the PartitionManager trait) will return
    // an error.
    pub struct MockPartitionManager {
        // The partitions backing the mock partition manager
        pub maybe_partitions: Option<Vec<MockPartition>>,
    }

    #[async_trait]
    impl PartitionManager<MockPartition> for MockPartitionManager {
        async fn partitions(&self) -> Result<Vec<MockPartition>, Error> {
            match &self.maybe_partitions {
                Some(partitions) => Ok(partitions.to_vec()),
                None => Err(anyhow!("listing partitions failed")),
            }
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_partition_methods() {
        let p1 = MockPartition {
            guid: Ok(DATA_GUID),
            label: Ok(ACCOUNT_LABEL.to_string()),
            first_block: Some(make_zxcrypt_superblock(4096)),
        };
        let p2 = MockPartition {
            guid: Ok(BLOB_GUID),
            label: Ok("fuchsia-blob".to_string()),
            first_block: None,
        };

        let desired_guid = FUCHSIA_DATA_GUID;
        let desired_label = "account";

        assert!(partition_has_guid(&p1, desired_guid).await);
        assert!(partition_has_label(&p1, desired_label).await);

        assert!(!partition_has_guid(&p2, desired_guid).await);
        assert!(!partition_has_label(&p2, desired_label).await);

        assert_matches!(p1.has_guid(desired_guid).await, Ok(true));
        assert_matches!(p2.has_guid(desired_guid).await, Ok(false));

        assert_matches!(p1.has_label(desired_label).await, Ok(true));
        assert_matches!(p2.has_label(desired_label).await, Ok(false));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_has_zxcrypt_header() {
        let zxcrypt_partition = MockPartition {
            guid: Ok(DATA_GUID),
            label: Ok(ACCOUNT_LABEL.to_string()),
            first_block: Some(make_zxcrypt_superblock(4096)),
        };
        assert_matches!(has_zxcrypt_header(&zxcrypt_partition).await, Ok(true));

        let not_zxcrypt_partition = MockPartition {
            guid: Ok(DATA_GUID),
            label: Ok(ACCOUNT_LABEL.to_string()),
            first_block: Some([0u8; 4096].to_vec()),
        };
        assert_matches!(has_zxcrypt_header(&not_zxcrypt_partition).await, Ok(false));

        let unreadable_partition = MockPartition {
            guid: Ok(DATA_GUID),
            label: Ok(ACCOUNT_LABEL.to_string()),
            first_block: None,
        };
        assert_matches!(has_zxcrypt_header(&unreadable_partition).await, Err(_));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_partition_manager() {
        let inner_partitions = vec![
            MockPartition {
                guid: Ok(BLOB_GUID),
                label: Ok("fuchsia-blob".to_string()),
                first_block: None,
            },
            MockPartition {
                guid: Ok(DATA_GUID),
                label: Ok(ACCOUNT_LABEL.to_string()),
                first_block: Some(make_zxcrypt_superblock(4096)),
            },
        ];

        let pm_ok = MockPartitionManager { maybe_partitions: Some(inner_partitions) };
        let partitions_ok = pm_ok.partitions().await;
        assert_matches!(partitions_ok, Ok(_));
        assert_eq!(partitions_ok.unwrap().len(), 2);

        let pm_fail = MockPartitionManager { maybe_partitions: None };
        let partitions_fail = pm_fail.partitions().await;
        assert_matches!(partitions_fail, Err(_));
    }
}
