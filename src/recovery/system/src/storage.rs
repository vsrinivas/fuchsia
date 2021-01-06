// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context, Error};
use fdio::{create_fd, device_get_topo_path};
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_io::{DirectoryMarker, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE};
use fidl_fuchsia_paver::{PaverMarker, PaverProxy};
use fs_management as fs;
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;

/// Calls the paver service to format the system volume, and returns the path
/// to the newly created blobfs and minfs partitions.
async fn format_disk(paver: PaverProxy) -> Result<(String, String), Error> {
    let (data_sink, data_sink_server_end) = fidl::endpoints::create_proxy()?;
    paver.find_data_sink(data_sink_server_end)?;

    let server_end = match data_sink.wipe_volume().await? {
        Ok(server) => server,
        Err(err) => return Err(format_err!("failed to wipe volume: {}", err)),
    };

    let file = create_fd(server_end.into_channel().into())?;

    let base_path = device_get_topo_path(&file)?;

    Ok((format!("{}/blobfs-p-1/block", base_path), format!("{}/minfs-p-2/block", base_path)))
}

/// Required functionality from an fs::Filesystem.
/// See fs_management for the documentation.
trait Filesystem {
    fn format(&mut self) -> Result<(), Error>;
    fn mount(&mut self, mount_point: &str) -> Result<(), Error>;
}

/// Forwards calls to the fs_management implementation.
impl Filesystem for fs::Filesystem<fs::Blobfs> {
    fn format(&mut self) -> Result<(), Error> {
        self.format()
    }

    fn mount(&mut self, mount_point: &str) -> Result<(), Error> {
        self.mount(mount_point)
    }
}

/// The object that controls the lifetime of the newly minted blobfs.
/// The filesystem is accessible through the "/b" path on the current namespace,
/// as long as this object is alive.
pub struct Storage {
    _blobfs: fs::Filesystem<fs::Blobfs>,
    minfs: fs::Filesystem<fs::Minfs>,
}

impl Storage {
    /// Re-initializes the storage stack on this device, returning an object that
    /// encapsulates the freshly minted blobfs.
    pub async fn new() -> Result<Storage, Error> {
        println!("About to Initialize storage");
        let paver = connect_to_service::<PaverMarker>()?;
        let (blobfs_path, minfs_path) = format_disk(paver.clone()).await?;

        let blobfs = create_blobfs(blobfs_path)?;
        let minfs = create_minfs(minfs_path)?;

        println!("Storage initialized");
        Ok(Storage { _blobfs: blobfs, minfs })
    }

    /// Mounts the encapsulated minfs at "/m".
    pub fn mount_minfs(&mut self) -> Result<(), Error> {
        self.minfs.mount("/m")
    }

    pub fn get_blobfs(&self) -> Result<ClientEnd<DirectoryMarker>, Error> {
        let (blobfs_root, remote) = fidl::endpoints::create_endpoints::<DirectoryMarker>()?;
        fdio::open("/b", OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, remote.into_channel())?;
        Ok(blobfs_root)
    }
}

/// Creates and mounts the provided filesystem at "/b".
fn mount_filesystem(blobfs: &mut dyn Filesystem) -> Result<(), Error> {
    blobfs.format()?;
    blobfs.mount("/b")?;
    Ok(())
}

/// Uses the provide block device path to format a new blobfs partition.
fn create_blobfs(block_device_path: String) -> Result<fs::Filesystem<fs::Blobfs>, Error> {
    let mut blobfs = fs::Blobfs::new(block_device_path.as_str())?;

    mount_filesystem(&mut blobfs)?;
    Ok(blobfs)
}

/// Uses the provide block device path to format a new minfs partition.
/// Does not mount the partition, but leaves it blank.
fn create_minfs(block_device_path: String) -> Result<fs::Filesystem<fs::Minfs>, Error> {
    let (block_device, server_chan) = zx::Channel::create()?;
    fdio::service_connect(&block_device_path, server_chan).context("connecting to block device")?;
    create_minfs_from_channel(block_device)
}

fn create_minfs_from_channel(
    block_device_channel: zx::Channel,
) -> Result<fs::Filesystem<fs::Minfs>, Error> {
    let mut minfs = fs::Minfs::from_channel(block_device_channel)?;
    minfs.format()?;
    Ok(minfs)
}

#[cfg(test)]
mod tests {
    use super::{create_minfs_from_channel, format_disk, mount_filesystem, Filesystem};
    use anyhow::Error;
    use fidl::endpoints::ClientEnd;
    use fidl_fuchsia_hardware_block_volume::VolumeManagerMarker;
    use fidl_fuchsia_paver::{
        DataSinkRequest, DataSinkRequestStream, PaverMarker, PaverRequest, PaverRequestStream,
    };
    use fuchsia_component::server::{NestedEnvironment, ServiceFs};
    use fuchsia_zircon as zx;
    use fuchsia_zircon::Status;
    use futures::prelude::*;
    use ramdevice_client::RamdiskClientBuilder;
    use std::sync::Arc;

    // Mock for a Filesystem.
    struct FilesystemMock {
        format_called: bool,
        mount_called: bool,
    }

    impl FilesystemMock {
        fn new() -> FilesystemMock {
            FilesystemMock { format_called: false, mount_called: false }
        }
    }

    impl Filesystem for FilesystemMock {
        fn format(&mut self) -> Result<(), Error> {
            assert!(!self.format_called);
            self.format_called = true;
            Ok(())
        }

        fn mount(&mut self, mount_point: &str) -> Result<(), Error> {
            assert!(self.format_called);
            assert_eq!(mount_point, "/b");
            self.mount_called = true;
            Ok(())
        }
    }

    /// Tests that mount_filesystem calls the expected Filesystem functions.
    #[test]
    fn test_mount() {
        let mut blobfs: FilesystemMock = FilesystemMock::new();
        let result = match mount_filesystem(&mut blobfs) {
            Ok(()) => true,
            _ => false,
        };
        assert!(result);
        assert!(blobfs.mount_called);
    }

    /// Mock for the paver service.
    struct MockPaver {
        /// Desired response.
        response: Status,
    }

    impl MockPaver {
        fn new(response: Status) -> Self {
            Self { response }
        }

        /// FindDataSink implementation.
        async fn run_service(self: Arc<Self>, mut stream: PaverRequestStream) -> Result<(), Error> {
            while let Some(req) = stream.try_next().await? {
                match req {
                    PaverRequest::FindDataSink { data_sink, .. } => {
                        let mock_paver_clone = self.clone();
                        fuchsia_async::Task::spawn(
                            mock_paver_clone
                                .run_data_sink_service(data_sink.into_stream()?)
                                .unwrap_or_else(|e| panic!("error running paver service: {:?}", e)),
                        )
                        .detach();
                    }
                    req => println!("mock Paver ignoring request: {:?}", req),
                }
            }
            Ok(())
        }

        /// WipeVolume implementation.
        async fn run_data_sink_service(
            self: Arc<Self>,
            mut stream: DataSinkRequestStream,
        ) -> Result<(), Error> {
            while let Some(req) = stream.try_next().await? {
                match req {
                    DataSinkRequest::WipeVolume { responder } => {
                        if self.response == Status::OK {
                            responder.send(&mut Ok(grab_volume())).expect("send ok");
                        } else {
                            responder.send(&mut Err(self.response.into_raw())).expect("send ok");
                        }
                    }
                    req => println!("mock paver ignoring request: {:?}", req),
                }
            }
            Ok(())
        }
    }

    /// Provides a suitable channel for a faked successful WipeVolume call.
    fn grab_volume() -> ClientEnd<VolumeManagerMarker> {
        // We need a channel to a device supported by device_get_topo_path.
        // Grab the system's fist block device.
        let (client_channel, server_channel) = zx::Channel::create().unwrap();
        fdio::service_connect("/dev/class/block/000", server_channel).expect("Open block device");
        ClientEnd::<VolumeManagerMarker>::new(client_channel)
    }

    struct TestEnv {
        env: NestedEnvironment,
        _paver: Arc<MockPaver>,
    }

    impl TestEnv {
        fn new(paver: MockPaver) -> Self {
            let mut fs = ServiceFs::new();
            let paver = Arc::new(paver);
            let paver_clone = Arc::clone(&paver);
            fs.add_fidl_service(move |stream: PaverRequestStream| {
                let paver_clone = Arc::clone(&paver_clone);
                fuchsia_async::Task::local(
                    paver_clone
                        .run_service(stream)
                        .unwrap_or_else(|e| panic!("error running paver service: {:?}", e)),
                )
                .detach()
            });

            let env = fs
                .create_salted_nested_environment("recovery_test_env")
                .expect("nested environment to create successfully");
            fuchsia_async::Task::spawn(fs.collect()).detach();

            Self { env, _paver: paver }
        }
    }

    /// Tests that format_disk fails when the paver service fails the request.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_calls_paver_service_with_errors() {
        let env = TestEnv::new(MockPaver::new(Status::NOT_SUPPORTED));
        let paver = env.env.connect_to_service::<PaverMarker>().unwrap();

        let path = format_disk(paver.clone()).await;
        assert_eq!(
            (String::new(), String::new()),
            match path {
                Ok(path) => path,
                Err(err) => {
                    println!("{:?}", err);
                    (String::new(), String::new())
                }
            }
        );
    }

    /// Tests that a successful call to the paver service results in a path with
    /// the expected form.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_calls_paver_service() {
        let env = TestEnv::new(MockPaver::new(Status::OK));
        let paver = env.env.connect_to_service::<PaverMarker>().unwrap();

        let result = format_disk(paver.clone()).await;
        let (blobfs_path, minfs_path) = match result {
            Ok(paths) => paths,
            Err(err) => {
                println!("{:?}", err);
                (String::new(), String::new())
            }
        };
        assert!(blobfs_path.ends_with("/blobfs-p-1/block"));
        assert!(minfs_path.ends_with("/minfs-p-2/block"));
    }

    /// Tests that creating minfs works.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_minfs_create_works() {
        const BLOCK_SIZE: u64 = 512;
        const BLOCK_COUNT: u64 = 262144; // 128 MB
        let block_device = RamdiskClientBuilder::new(BLOCK_SIZE, BLOCK_COUNT)
            .isolated_dev_root()
            .build()
            .expect("Ramdisk creation succeeds");

        let ramdisk = block_device.open().expect("Open ramdisk");
        let mut minfs = create_minfs_from_channel(ramdisk).expect("creating minfs to succeed");
        minfs.fsck().expect("Fsck to succeed on the new minfs");
        minfs.mount("/minfs").expect("minfs mount to succeed");
    }
}
