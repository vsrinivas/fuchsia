// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fdio::{create_fd, device_get_topo_path};
use fidl_fuchsia_paver::{PaverMarker, PaverProxy};
use fs_management as fs;
use fuchsia_component::client::connect_to_service;

/// Calls the paver service to format the system volume, and returns the path
/// to the newly created blobfs partition.
async fn format_disk(paver: PaverProxy) -> Result<String, Error> {
    let (data_sink, data_sink_server_end) = fidl::endpoints::create_proxy()?;
    paver.find_data_sink(data_sink_server_end)?;

    let server_end = match data_sink.wipe_volume().await? {
        Ok(server) => server,
        Err(err) => return Err(format_err!("failed to wipe volume: {}", err)),
    };

    let file = create_fd(server_end.into_channel().into())?;

    let mut path = device_get_topo_path(&file)?;
    path.push_str("/blobfs-p-1/block");
    Ok(path)
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
}

impl Storage {
    /// Re-initializes the storage stack on this device, returning an object that
    /// encapsulates the freshly minted blobfs.
    pub async fn new() -> Result<Storage, Error> {
        println!("About to Initialize storage");
        let paver = connect_to_service::<PaverMarker>()?;
        let path = format_disk(paver.clone()).await?;

        let blobfs = create_blobfs(path)?;

        println!("Storage initialized");
        Ok(Storage { _blobfs: blobfs })
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

#[cfg(test)]
mod tests {
    use super::{format_disk, mount_filesystem, Filesystem};
    use anyhow::Error;
    use fidl::endpoints::ServerEnd;
    use fidl_fuchsia_hardware_block_volume::VolumeManagerMarker;
    use fidl_fuchsia_paver::{
        DataSinkRequest, DataSinkRequestStream, PaverMarker, PaverRequest, PaverRequestStream,
    };
    use fuchsia_component::server::{NestedEnvironment, ServiceFs};
    use fuchsia_zircon as zx;
    use fuchsia_zircon::Status;
    use futures::prelude::*;
    use std::sync::Arc;

    // Mock for a Flesystem.
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
                        fuchsia_async::spawn(
                            mock_paver_clone
                                .run_data_sink_service(data_sink.into_stream()?)
                                .unwrap_or_else(|e| panic!("error running paver service: {:?}", e)),
                        );
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
    fn grab_volume() -> ServerEnd<VolumeManagerMarker> {
        // We need a channel to a device supported by device_get_topo_path.
        // Grab the system's fist block device.
        let (client_channel, server_channel) = zx::Channel::create().unwrap();
        fdio::service_connect("/dev/class/block/000", server_channel).expect("Open block device");
        ServerEnd::<VolumeManagerMarker>::new(client_channel)
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
                fuchsia_async::spawn_local(
                    paver_clone
                        .run_service(stream)
                        .unwrap_or_else(|e| panic!("error running paver service: {:?}", e)),
                )
            });

            let env = fs
                .create_salted_nested_environment("recovery_test_env")
                .expect("nested environment to create successfully");
            fuchsia_async::spawn(fs.collect());

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
            "",
            match path {
                Ok(path) => path,
                Err(err) => {
                    println!("{:?}", err);
                    "".to_string()
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

        let path = format_disk(paver.clone()).await;
        let path = match path {
            Ok(path) => path,
            Err(err) => {
                println!("{:?}", err);
                "".to_string()
            }
        };
        assert!(path.ends_with("/blobfs-p-1/block"));
    }
}
