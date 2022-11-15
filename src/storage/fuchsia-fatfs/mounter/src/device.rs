// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl::endpoints::{ClientEnd, Proxy as _},
    fidl_fuchsia_hardware_block::{BlockMarker, BlockProxy},
    fidl_fuchsia_hardware_block_partition::{Guid, PartitionMarker},
    fidl_fuchsia_io as fio,
    fuchsia_fatfs::FatFs,
    fuchsia_zircon as zx,
    remote_block_device::RemoteBlockClientSync,
    tracing::info,
    vfs::execution_scope::ExecutionScope,
};

const MICROSOFT_BASIC_DATA_GUID: [u8; 16] = [
    0xa2, 0xa0, 0xd0, 0xeb, 0xe5, 0xb9, 0x33, 0x44, 0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7,
];
const BLOCK_DEVICE_DIR: &str = "/dev/class/block";

pub struct FatDevice {
    fs: FatFs,
    pub scope: ExecutionScope,
}

impl FatDevice {
    /// Try and create a new FatDevice, searching for partitions in /dev/class/block.
    pub async fn new() -> Result<Option<Self>, Error> {
        let dir = fuchsia_fs::directory::open_in_namespace(
            BLOCK_DEVICE_DIR,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )?;
        Self::new_at(dir).await
    }

    /// Try and create a new FatDevice, searching for partitions in the given channel.
    async fn new_at(dir: fio::DirectoryProxy) -> Result<Option<Self>, Error> {
        let partition = match Self::find_fat_partition(&dir).await? {
            Some(value) => value,
            None => return Ok(None),
        };

        let proxy =
            fuchsia_component::client::connect_to_named_protocol_at_dir_root::<BlockMarker>(
                &dir, &partition,
            )
            .with_context(|| format!("failed to open {}", partition))?;
        let channel = proxy
            .into_channel()
            .map_err(|_: BlockProxy| anyhow::anyhow!("failed to get block channel"))?;
        let client_end = ClientEnd::<BlockMarker>::new(channel.into());
        let device =
            Box::new(remote_block_device::Cache::new(RemoteBlockClientSync::new(client_end)?)?);
        // TODO(simonshields): if this fails, we could try looking for another partition.
        let fs = FatFs::new(device)?;

        let scope = ExecutionScope::new();

        Ok(Some(FatDevice { fs, scope }))
    }

    pub fn is_present(&self) -> bool {
        self.fs.is_present()
    }

    /// Find a partition with the "Microsoft Basic Data" GUID, which may contain a FAT partition.
    async fn find_fat_partition(dir: &fio::DirectoryProxy) -> Result<Option<String>, Error> {
        let children = fuchsia_fs::directory::readdir(dir).await?;

        for entry in children.iter() {
            let guid = match Self::get_guid_at(dir, &entry.name).await {
                Ok(Some(guid)) => guid,
                Ok(None) => {
                    // If there's no guid, skip the device.
                    continue;
                }
                Err(_) => {
                    // If this happens, it probably just means that the block device wasn't a
                    // partition. Skip it.
                    continue;
                }
            };

            info!(name = ?entry.name, ?guid, "Found block device");
            if guid.value == MICROSOFT_BASIC_DATA_GUID {
                return Ok(Some(entry.name.clone()));
            }
        }
        // TODO(fxbug.dev/58577): should we set up a watcher with a timeout before giving up completely?

        Ok(None)
    }

    /// Get the type GUID for the file with name |name| in |dir|.
    async fn get_guid_at(
        dir: &fio::DirectoryProxy,
        name: &str,
    ) -> Result<Option<Box<Guid>>, Error> {
        let proxy = fuchsia_component::client::connect_to_named_protocol_at_dir_root::<
            PartitionMarker,
        >(&dir, name)
        .with_context(|| format!("failed to open {}", name))?;
        let (status, guid) = proxy.get_type_guid().await?;
        let () = zx::Status::ok(status).context("Getting GUID")?;
        Ok(guid)
    }
}

impl std::ops::Deref for FatDevice {
    type Target = FatFs;

    fn deref(&self) -> &Self::Target {
        &self.fs
    }
}

#[cfg(test)]
pub mod test {
    use {
        super::*,
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_hardware_block::BlockMarker,
        fidl_fuchsia_hardware_block_partition::{Guid, PartitionMarker, PartitionRequest},
        fuchsia_async as fasync,
        fuchsia_component::server::ServiceFs,
        futures::prelude::*,
        ramdevice_client::RamdiskClient,
        std::io::Write,
        vfs::path::Path,
    };

    /// Dictates the FIDL protocol a MockPartition should speak.
    enum MockPartitionMode {
        Partition,
        Block,
    }

    /// Represents a block device, for unit testing.
    struct MockPartition {
        /// GUID to return for GetTypeGuid.
        type_guid: [u8; 16],
        /// FIDL protocol to speak.
        mode: MockPartitionMode,
        /// Channel to respond to requests on.
        channel: zx::Channel,
    }

    /// GUID for an EFI system partition.
    const EFI_SYSTEM_PART_GUID: [u8; 16] = [
        0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11, 0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9,
        0x3b,
    ];

    impl MockPartition {
        /// Make a new FAT partition.
        pub fn fat(channel: zx::Channel) -> Self {
            MockPartition {
                type_guid: MICROSOFT_BASIC_DATA_GUID,
                mode: MockPartitionMode::Partition,
                channel,
            }
        }

        /// Make a new Block protocol device.
        pub fn block(channel: zx::Channel) -> Self {
            MockPartition {
                type_guid: MICROSOFT_BASIC_DATA_GUID,
                mode: MockPartitionMode::Block,
                channel,
            }
        }

        /// Make a new non-FAT partition.
        pub fn not_fat(channel: zx::Channel) -> Self {
            MockPartition {
                type_guid: EFI_SYSTEM_PART_GUID,
                mode: MockPartitionMode::Partition,
                channel,
            }
        }

        /// Handle requests on the supplied channel.
        pub async fn serve(self) {
            match self.mode {
                MockPartitionMode::Partition => self.serve_partition().await,
                MockPartitionMode::Block => self.serve_block().await,
            };
        }

        async fn serve_partition(self) {
            let server_end: ServerEnd<PartitionMarker> =
                ServerEnd::new(fidl::Channel::from(self.channel));
            let mut stream = server_end.into_stream().unwrap();
            while let Some(req) = stream.try_next().await.unwrap() {
                match req {
                    PartitionRequest::GetTypeGuid { responder } => {
                        responder
                            .send(
                                zx::Status::OK.into_raw(),
                                Some(&mut Guid { value: self.type_guid }),
                            )
                            .expect("Send succeeds");
                    }
                    _ => panic!("Unsupported request!"),
                }
            }
        }

        async fn serve_block(self) {
            let server_end: ServerEnd<BlockMarker> =
                ServerEnd::new(fidl::Channel::from(self.channel));
            let mut stream = server_end.into_stream().unwrap();
            while let Ok(Some(_)) = stream.try_next().await {
                panic!("Did not expect to get any requests!");
            }
            // We expect Err(), because the client speaks the wrong protocol. The connection will
            // be dropped, which should be handled gracefully by the client.
        }
    }

    #[fuchsia::test]
    async fn test_get_guid_succeeds() {
        let mut fs = ServiceFs::new();
        fs.dir("dev").add_service_at("000", |chan| Some(MockPartition::fat(chan)));
        let (local, remote) = fidl::endpoints::create_endpoints().expect("create channel OK");

        fs.serve_connection(remote).unwrap();
        let _fs_task = fasync::Task::spawn(fs.for_each(|part| async { part.serve().await }));

        let dir = local.into_proxy().expect("into proxy");

        let dev_dir = fuchsia_fs::directory::open_directory(
            &dir,
            "dev",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .expect("open directory");

        let result = FatDevice::get_guid_at(&dev_dir, "000").await.expect("get guid succeeds");
        assert_eq!(result.unwrap().value, MICROSOFT_BASIC_DATA_GUID);
    }

    #[fuchsia::test]
    async fn test_find_fat_partition_succeeds() {
        let mut fs = ServiceFs::new();
        fs.dir("dev")
            .add_service_at("000", |chan| Some(MockPartition::not_fat(chan)))
            .add_service_at("001", |chan| Some(MockPartition::block(chan)))
            .add_service_at("002", |chan| Some(MockPartition::fat(chan)));
        let (local, remote) = fidl::endpoints::create_endpoints().expect("create channel OK");

        fs.serve_connection(remote).unwrap();
        let _fs_task = fasync::Task::spawn(fs.for_each(|part| async { part.serve().await }));

        let dir = local.into_proxy().expect("into proxy");

        let dev_dir = fuchsia_fs::directory::open_directory(
            &dir,
            "dev",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .expect("open directory");

        let result = FatDevice::find_fat_partition(&dev_dir).await;
        assert_eq!(result.expect("Find partition succeeds"), Some("002".to_owned()));
    }

    #[fuchsia::test]
    async fn test_find_no_fat_partition_succeeds() {
        let mut fs = ServiceFs::new();
        fs.dir("dev")
            .add_service_at("000", |chan| Some(MockPartition::not_fat(chan)))
            .add_service_at("001", |chan| Some(MockPartition::block(chan)))
            .add_service_at("002", |chan| Some(MockPartition::not_fat(chan)));
        let (local, remote) = fidl::endpoints::create_endpoints().expect("create channel OK");

        fs.serve_connection(remote).unwrap();
        let _fs_task = fasync::Task::spawn(fs.for_each(|part| async { part.serve().await }));

        let dir = local.into_proxy().expect("into proxy");

        let dev_dir = fuchsia_fs::directory::open_directory(
            &dir,
            "dev",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .expect("open directory");

        let result = FatDevice::find_fat_partition(&dev_dir).await;
        assert_eq!(result.expect("Find partition succeeds"), None);
    }

    pub fn create_ramdisk() -> RamdiskClient {
        ramdevice_client::wait_for_device(
            "/dev/sys/platform/00:00:2d/ramctl",
            std::time::Duration::from_secs(10),
        )
        .expect("ramctl did not appear");

        ramdevice_client::RamdiskClientBuilder::new(512, 1024 * 1024)
            .guid(MICROSOFT_BASIC_DATA_GUID)
            .build()
            .expect("Create ramdisk client succeeds")
    }

    pub fn format(client_end: ClientEnd<BlockMarker>) {
        // Create a filesystem on the ramdisk.
        let remote_block_client = RemoteBlockClientSync::new(client_end).unwrap();
        let device = Box::new(remote_block_device::Cache::new(remote_block_client).unwrap());
        fatfs::format_volume(device, fatfs::FormatVolumeOptions::new())
            .expect("Format volume succeeds");
    }

    pub fn setup_test_fs(client_end: ClientEnd<BlockMarker>, name: &str) {
        let remote_block_client = RemoteBlockClientSync::new(client_end).unwrap();
        let device = Box::new(remote_block_device::Cache::new(remote_block_client).unwrap());
        let fs = fatfs::FileSystem::new(device, fatfs::FsOptions::new())
            .expect("Create filesystem succeeds");

        {
            let dir = fs.root_dir().create_dir("test").expect("Create dir succeeds");
            let mut file = dir.create_file("blah").expect("Create file succeeds");
            file.write_all("hello, world!".as_bytes()).expect("Write succeds");

            fs.root_dir().create_dir(name).expect("Create dir succeeds");
        }

        fs.unmount().expect("Unmount succeeds");
    }

    fn open_root(dev: &FatDevice) -> fio::DirectoryProxy {
        let (proxy, remote) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let root = dev.get_root().unwrap();
        let () = root.clone().open(
            dev.scope.clone(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            0,
            Path::dot(),
            remote.into_channel().into(),
        );
        let () = root.close().unwrap();
        proxy
    }

    #[fuchsia::test]
    async fn test_mount_device_succeeds() {
        let ramdisk = create_ramdisk();
        let channel = ramdisk.open().expect("Opening ramdisk succeeds");
        format(channel);
        let channel = ramdisk.open().expect("Opening ramdisk succeeds");
        setup_test_fs(channel, "mount_device");

        let dev =
            FatDevice::new().await.expect("Create fat device OK").expect("Found a fat device");

        let proxy = open_root(&dev);

        let mut children: Vec<_> = fuchsia_fs::directory::readdir(&proxy)
            .await
            .expect("Readdir succeeds")
            .into_iter()
            .map(|ent| ent.name)
            .collect();
        children.sort();
        assert_eq!(children, vec!["mount_device", "test"]);
    }

    #[fuchsia::test]
    async fn test_mount_invalid_device_fails() {
        // This ramdisk will have the right GUID, but no FAT partition.
        let _ramdisk = create_ramdisk();

        match FatDevice::new().await {
            Ok(_) => panic!("Expected FatDevice::new() to fail"),
            Err(_) => {}
        }
    }

    #[fuchsia::test]
    async fn test_multiple_devices_opens_first() {
        let ramdisk1 = create_ramdisk();
        let channel = ramdisk1.open().expect("Opening ramdisk succeeds");
        format(channel);
        let channel = ramdisk1.open().expect("Opening ramdisk succeeds");
        setup_test_fs(channel, "ramdisk1");

        let ramdisk2 = create_ramdisk();
        let channel = ramdisk2.open().expect("Opening ramdisk succeeds");
        format(channel);
        let channel = ramdisk2.open().expect("Opening ramdisk succeeds");
        setup_test_fs(channel, "ramdisk2");

        let dev =
            FatDevice::new().await.expect("Create fat device OK").expect("Found a fat device");

        let proxy = open_root(&dev);

        let mut children: Vec<_> = fuchsia_fs::directory::readdir(&proxy)
            .await
            .expect("Readdir succeeds")
            .into_iter()
            .map(|ent| ent.name)
            .collect();
        children.sort();
        assert_eq!(children, vec!["ramdisk1", "test"]);
    }

    #[fuchsia::test]
    async fn test_multiple_devices_opens_first_when_invalid() {
        let _ramdisk1 = create_ramdisk();

        let ramdisk2 = create_ramdisk();
        let channel = ramdisk2.open().expect("Opening ramdisk succeeds");
        format(channel);
        let channel = ramdisk2.open().expect("Opening ramdisk succeeds");
        setup_test_fs(channel, "ramdisk2");

        // Currently, we expect this to fail, because `ramdisk1` has the right GUID but is not
        // formatted correctly.
        match FatDevice::new().await {
            Ok(_) => panic!("Expected FatDevice::new() to fail"),
            Err(_) => {}
        }
    }
}
