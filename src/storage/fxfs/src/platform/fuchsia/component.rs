// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        crypt::Crypt,
        filesystem::{mkfs, FxFilesystem, OpenFxFilesystem, OpenOptions},
        fsck,
        log::*,
        object_store::volume::root_volume,
        platform::{
            fuchsia::{errors::map_to_status, volumes_directory::VolumesDirectory},
            RemoteCrypt,
        },
    },
    anyhow::{ensure, Context, Error},
    fidl::endpoints::{ClientEnd, DiscoverableProtocolMarker, RequestStream, ServerEnd},
    fidl_fuchsia_fs::{AdminMarker, AdminRequest, AdminRequestStream},
    fidl_fuchsia_fs_startup::{
        CheckOptions, FormatOptions, StartOptions, StartupMarker, StartupRequest,
        StartupRequestStream,
    },
    fidl_fuchsia_fxfs::{CryptProxy, VolumesMarker, VolumesRequest, VolumesRequestStream},
    fidl_fuchsia_hardware_block::BlockMarker,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_process_lifecycle::{LifecycleRequest, LifecycleRequestStream},
    fs_inspect::{FsInspect, FsInspectTree},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::TryStreamExt,
    inspect_runtime::service::{TreeServerSendPreference, TreeServerSettings},
    remote_block_device::RemoteBlockClient,
    std::sync::{Arc, Mutex, Weak},
    storage_device::{block_device::BlockDevice, DeviceHolder},
    vfs::{
        directory::{entry::DirectoryEntry, helper::DirectlyMutable},
        execution_scope::ExecutionScope,
        path::Path,
        remote::remote_boxed_with_type,
    },
};

const DEFAULT_VOLUME_NAME: &str = "default";

pub fn map_to_raw_status(e: Error) -> zx::sys::zx_status_t {
    map_to_status(e).into_raw()
}

/// Runs Fxfs as a component.
pub struct Component {
    // This is None until Start is called with a block device.
    state: Mutex<State>,

    // The execution scope of the pseudo filesystem.
    scope: ExecutionScope,

    // The root of the pseudo filesystem for the component.
    outgoing_dir: Arc<vfs::directory::immutable::Simple>,
}

enum State {
    PreStart { queued: Vec<(fio::OpenFlags, u32, Path, ServerEnd<fio::NodeMarker>)> },
    Started(Started),
    Stopped,
}

struct Started {
    fs: OpenFxFilesystem,
    volumes: Arc<VolumesDirectory>,
    _inspect_tree: FsInspectTree,
}

impl State {
    fn maybe_stop(&mut self) -> Option<Started> {
        if let State::Started(_) = self {
            if let State::Started(started) = std::mem::replace(self, State::Stopped) {
                Some(started)
            } else {
                unsafe {
                    std::hint::unreachable_unchecked();
                }
            }
        } else {
            None
        }
    }
}

impl Component {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            state: Mutex::new(State::PreStart { queued: Vec::new() }),
            scope: ExecutionScope::new(),
            outgoing_dir: vfs::directory::immutable::simple(),
        })
    }

    /// Runs Fxfs as a component.
    pub async fn run(
        self: Arc<Self>,
        outgoing_dir: zx::Channel,
        lifecycle_channel: Option<zx::Channel>,
    ) -> Result<(), Error> {
        self.outgoing_dir
            .add_entry(
                "diagnostics",
                inspect_runtime::create_diagnostics_dir_with_options(
                    fuchsia_inspect::component::inspector().clone(),
                    TreeServerSettings {
                        send_vmo_preference: TreeServerSendPreference::frozen_or(
                            TreeServerSendPreference::DeepCopy,
                        ),
                    },
                ),
            )
            .expect("unable to create diagnostics dir");

        let weak = Arc::downgrade(&self);
        self.outgoing_dir.add_entry(
            "root",
            // remote_boxed_with_type will work slightly differently to how it will once we've
            // mounted because opening with NODE_REFERENCE will succeed and open the pseudo
            // entry. This difference shouldn't matter.
            remote_boxed_with_type(
                Box::new(move |_, open_flags, mode, path, channel| {
                    if let Some(me) = weak.upgrade() {
                        if let State::PreStart { queued } = &mut *me.state.lock().unwrap() {
                            queued.push((open_flags, mode, path, channel));
                        }
                    }
                }),
                fio::DirentType::Directory,
            ),
        )?;

        let svc_dir = vfs::directory::immutable::simple();
        self.outgoing_dir.add_entry("svc", svc_dir.clone()).expect("Unable to create svc dir");
        let weak = Arc::downgrade(&self);
        svc_dir.add_entry(
            StartupMarker::PROTOCOL_NAME,
            vfs::service::host(move |requests| {
                let weak = weak.clone();
                async move {
                    if let Some(me) = weak.upgrade() {
                        let _ = me.handle_startup_requests(requests).await;
                    }
                }
            }),
        )?;
        let weak = Arc::downgrade(&self);
        svc_dir.add_entry(
            VolumesMarker::PROTOCOL_NAME,
            vfs::service::host(move |requests| {
                let weak = weak.clone();
                async move {
                    if let Some(me) = weak.upgrade() {
                        me.handle_volumes_requests(requests).await;
                    }
                }
            }),
        )?;

        let weak = Arc::downgrade(&self);
        svc_dir.add_entry(
            AdminMarker::PROTOCOL_NAME,
            vfs::service::host(move |requests| {
                let weak = weak.clone();
                async move {
                    if let Some(me) = weak.upgrade() {
                        let _ = me.handle_admin_requests(requests).await;
                    }
                }
            }),
        )?;

        self.outgoing_dir.clone().open(
            self.scope.clone(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            0,
            Path::dot(),
            outgoing_dir.into(),
        );

        if let Some(channel) = lifecycle_channel {
            let me = self.clone();
            self.scope.spawn(async move {
                if let Err(e) = me.handle_lifecycle_requests(channel).await {
                    warn!(error = e.as_value(), "handle_lifecycle_requests");
                }
            });
        }

        self.scope.wait().await;

        Ok(())
    }

    async fn handle_startup_requests(&self, mut stream: StartupRequestStream) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            match request {
                StartupRequest::Start { responder, device, options } => responder.send(
                    &mut self.handle_start(device, options).await.map_err(map_to_raw_status),
                )?,

                StartupRequest::Format { responder, device, options } => responder.send(
                    &mut self.handle_format(device, options).await.map_err(map_to_raw_status),
                )?,

                StartupRequest::Check { responder, device, options } => responder.send(
                    &mut self.handle_check(device, options).await.map_err(map_to_raw_status),
                )?,
            }
        }
        Ok(())
    }

    async fn handle_start(
        &self,
        device: ClientEnd<BlockMarker>,
        options: StartOptions,
    ) -> Result<(), Error> {
        ensure!(!matches!(&*self.state.lock().unwrap(), State::Started(_)), zx::Status::BAD_STATE);

        let client = RemoteBlockClient::new(device.into_channel()).await?;
        let crypt = if let Some(crypt) = options.crypt {
            Some(Arc::new(RemoteCrypt::new(CryptProxy::new(fasync::Channel::from_channel(
                crypt.into_channel(),
            )?))) as Arc<dyn Crypt>)
        } else {
            None
        };
        let fs = FxFilesystem::open_with_options(
            DeviceHolder::new(BlockDevice::new(Box::new(client), options.read_only).await?),
            OpenOptions { read_only: options.read_only, ..Default::default() },
        )
        .await?;
        let volumes = VolumesDirectory::new(root_volume(&fs).await?).await?;
        // TODO(fxbug.dev/99182): We should eventually not open the default volume.
        let volume = volumes.mount_volume(DEFAULT_VOLUME_NAME, crypt).await?;
        let root = volume.root();
        if let Some(migrate_root) = options.migrate_root {
            let scope = volume.volume().scope();
            root.clone().open(
                scope.clone(),
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                0,
                Path::dot(),
                migrate_root.into_channel().into(),
            );
            scope.wait().await;
        }

        self.outgoing_dir.add_entry_impl(
            "root".to_string(),
            volume.root().clone(),
            /* overwrite: */ true,
        )?;

        self.outgoing_dir.add_entry_impl(
            "volumes".to_string(),
            volumes.directory_node().clone(),
            /* overwrite: */ true,
        )?;

        if let State::PreStart { queued } = std::mem::replace(
            &mut *self.state.lock().unwrap(),
            State::Started(Started {
                fs,
                volumes,
                _inspect_tree: FsInspectTree::new(
                    Arc::downgrade(volume.volume()) as Weak<dyn FsInspect + Send + Sync>,
                    &crate::metrics::FXFS_ROOT_NODE.lock().unwrap(),
                ),
            }),
        ) {
            let scope = volume.volume().scope();
            for (open_flags, mode, path, channel) in queued {
                root.clone().open(scope.clone(), open_flags, mode, path, channel);
            }
        }
        info!("Mounted");
        Ok(())
    }

    async fn handle_format(
        &self,
        device: ClientEnd<BlockMarker>,
        options: FormatOptions,
    ) -> Result<(), Error> {
        let client = RemoteBlockClient::new(device.into_channel()).await?;
        let crypt = Arc::new(RemoteCrypt::new(CryptProxy::new(fasync::Channel::from_channel(
            options.crypt.ok_or(zx::Status::INVALID_ARGS)?.into_channel(),
        )?)));
        mkfs(
            DeviceHolder::new(BlockDevice::new(Box::new(client), /* read_only: */ false).await?),
            Some(crypt),
        )
        .await?;
        Ok(())
    }

    async fn handle_check(
        &self,
        device: ClientEnd<BlockMarker>,
        options: CheckOptions,
    ) -> Result<(), Error> {
        let client = RemoteBlockClient::new(device.into_channel()).await?;
        let fs = FxFilesystem::open_with_options(
            DeviceHolder::new(BlockDevice::new(Box::new(client), /* read_only: */ true).await?),
            OpenOptions { read_only: true, ..Default::default() },
        )
        .await?;
        let fsck_options = fsck::default_options();
        let crypt = Arc::new(RemoteCrypt::new(CryptProxy::new(fasync::Channel::from_channel(
            options.crypt.ok_or(zx::Status::INVALID_ARGS)?.into_channel(),
        )?)));
        fsck::fsck_with_options(&fs, Some(crypt), fsck_options).await?;
        let _ = fs.close().await;
        Ok(())
    }

    async fn handle_admin_requests(&self, mut stream: AdminRequestStream) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await.context("Reading request")? {
            if self.handle_admin(request).await? {
                break;
            }
        }
        Ok(())
    }

    // Returns true if we should close the connection.
    async fn handle_admin(&self, req: AdminRequest) -> Result<bool, Error> {
        match req {
            AdminRequest::Shutdown { responder } => {
                info!("Received shutdown request");
                self.shutdown().await;
                responder
                    .send()
                    .unwrap_or_else(|e| warn!("Failed to send shutdown response: {}", e));
                return Ok(true);
            }
        }
    }

    async fn shutdown(&self) {
        let state = self.state.lock().unwrap().maybe_stop();
        if let Some(state) = state {
            let _ = self
                .outgoing_dir
                .remove_entry_impl("root".into(), /* must_be_directory: */ false);
            let _ = self
                .outgoing_dir
                .remove_entry_impl("volumes".into(), /* must_be_directory: */ false);
            state.volumes.terminate().await;
            let _ = state.fs.close().await;
        }
        info!("Filesystem terminated");
    }

    async fn handle_volumes_requests(&self, mut stream: VolumesRequestStream) {
        let volumes = if let State::Started(Started { volumes, .. }) = &*self.state.lock().unwrap()
        {
            volumes.clone()
        } else {
            let _ = stream.into_inner().0.shutdown_with_epitaph(zx::Status::BAD_STATE);
            return;
        };
        while let Ok(Some(request)) = stream.try_next().await {
            match request {
                VolumesRequest::Create { name, crypt, outgoing_directory, responder } => {
                    info!(name = name.as_str(), "Create volume");
                    responder
                        .send(
                            &mut volumes
                                .create_and_serve_volume(
                                    &name,
                                    crypt,
                                    outgoing_directory.into_channel().into(),
                                )
                                .await
                                .map_err(map_to_raw_status),
                        )
                        .unwrap_or_else(|e| {
                            warn!(error = e.as_value(), "Failed to send volume creation response")
                        });
                }
                VolumesRequest::Remove { name, responder } => {
                    info!(name = name.as_str(), "Remove volume");
                    responder
                        .send(&mut volumes.remove_volume(&name).await.map_err(map_to_raw_status))
                        .unwrap_or_else(|e| {
                            warn!(error = e.as_value(), "Failed to send volume removal response")
                        });
                }
            }
        }
    }

    async fn handle_lifecycle_requests(&self, lifecycle_channel: zx::Channel) -> Result<(), Error> {
        let mut stream =
            LifecycleRequestStream::from_channel(fasync::Channel::from_channel(lifecycle_channel)?);
        match stream.try_next().await.context("Reading request")? {
            Some(LifecycleRequest::Stop { .. }) => {
                info!("Received Lifecycle::Stop request");
                self.shutdown().await;
            }
            None => {}
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::Component,
        crate::{filesystem::FxFilesystem, object_store::volume::root_volume},
        fidl::{
            encoding::Decodable,
            endpoints::{Proxy, ServerEnd},
        },
        fidl_fuchsia_fs::AdminMarker,
        fidl_fuchsia_fs_startup::{StartOptions, StartupMarker},
        fidl_fuchsia_fxfs::VolumesMarker,
        fidl_fuchsia_io as fio,
        fidl_fuchsia_process_lifecycle::{LifecycleMarker, LifecycleProxy},
        fuchsia_async as fasync,
        fuchsia_component::client::connect_to_protocol_at_dir_svc,
        fuchsia_fs::directory::readdir,
        fuchsia_zircon as zx,
        futures::future::{BoxFuture, FusedFuture},
        futures::{future::FutureExt, pin_mut, select},
        ramdevice_client::{wait_for_device, RamdiskClientBuilder},
        remote_block_device::RemoteBlockClient,
        std::{collections::HashSet, pin::Pin},
        storage_device::block_device::BlockDevice,
        storage_device::DeviceHolder,
    };

    async fn run_test(
        callback: impl Fn(&fio::DirectoryProxy, LifecycleProxy) -> BoxFuture<'static, ()>,
    ) -> Pin<Box<impl FusedFuture>> {
        const WAIT_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(10);
        wait_for_device("/dev/sys/platform/00:00:2d/ramctl", WAIT_TIMEOUT)
            .expect("ramctl did not appear");

        let ramdisk =
            RamdiskClientBuilder::new(512, 16384).build().expect("Failed to build ramdisk");

        {
            let fs = FxFilesystem::new_empty(DeviceHolder::new(
                BlockDevice::new(
                    Box::new(
                        RemoteBlockClient::new(ramdisk.open().expect("Unable to open ramdisk"))
                            .await
                            .expect("Unable to create block client"),
                    ),
                    false,
                )
                .await
                .unwrap(),
            ))
            .await
            .expect("FxFilesystem::new_empty failed");
            {
                let root_volume = root_volume(&fs).await.expect("Open root_volume failed");
                root_volume.new_volume("default", None).await.expect("Create volume failed");
            }
            fs.close().await.expect("close failed");
        }

        let (client_end, server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();

        let (lifecycle_client, lifecycle_server) =
            fidl::endpoints::create_proxy::<LifecycleMarker>().unwrap();

        let mut component_task = Box::pin(
            async {
                Component::new()
                    .run(server_end.into_channel(), Some(lifecycle_server.into_channel()))
                    .await
                    .expect("Failed to run component");
            }
            .fuse(),
        );

        let startup_proxy = connect_to_protocol_at_dir_svc::<StartupMarker>(&client_end)
            .expect("Unable to connect to Startup protocol");
        let task = async {
            startup_proxy
                .start(
                    ramdisk.open().expect("Unable to open ramdisk").into(),
                    &mut StartOptions::new_empty(),
                )
                .await
                .expect("Start failed (FIDL)")
                .expect("Start failed");
            callback(&client_end, lifecycle_client).await;
        }
        .fuse();

        pin_mut!(task);

        loop {
            select! {
                () = component_task => {},
                () = task => break,
            }
        }

        component_task
    }

    #[fasync::run(2, test)]
    async fn test_shutdown() {
        let component_task = run_test(|client, _| {
            let admin_proxy = connect_to_protocol_at_dir_svc::<AdminMarker>(client)
                .expect("Unable to connect to Admin protocol");
            async move {
                admin_proxy.shutdown().await.expect("shutdown failed");
            }
            .boxed()
        })
        .await;
        assert!(!component_task.is_terminated());
    }

    #[fasync::run(2, test)]
    async fn test_lifecycle_stop() {
        let component_task = run_test(|_, lifecycle_client| {
            lifecycle_client.stop().expect("Stop failed");
            async move {
                fasync::OnSignals::new(
                    &lifecycle_client.into_channel().expect("into_channel failed"),
                    zx::Signals::CHANNEL_PEER_CLOSED,
                )
                .await
                .expect("OnSignals failed");
            }
            .boxed()
        })
        .await;
        component_task.await;
    }

    #[fasync::run(2, test)]
    async fn test_create_and_remove() {
        run_test(|client, _| {
            let volumes_proxy = connect_to_protocol_at_dir_svc::<VolumesMarker>(client)
                .expect("Unable to connect to Volumes protocol");

            let fs_admin_proxy = connect_to_protocol_at_dir_svc::<AdminMarker>(client)
                .expect("Unable to connect to Admin protocol");

            async move {
                let (dir_proxy, server_end) =
                    fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
                        .expect("create_proxy failed");
                volumes_proxy
                    .create("test", None, server_end)
                    .await
                    .expect("fidl failed")
                    .expect("create failed");

                // This should fail whilst the volume is mounted.
                volumes_proxy
                    .remove("test")
                    .await
                    .expect("fidl failed")
                    .expect_err("remove succeeded");

                let admin_proxy = connect_to_protocol_at_dir_svc::<AdminMarker>(&dir_proxy)
                    .expect("Unable to connect to Volumes protocol");
                admin_proxy.shutdown().await.expect("shutdown failed");

                // Creating another volume with the same name should fail.
                let (_dir_proxy, server_end) =
                    fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
                        .expect("create_proxy failed");
                volumes_proxy
                    .create("test", None, server_end)
                    .await
                    .expect("fidl failed")
                    .expect_err("create succeeded");

                volumes_proxy.remove("test").await.expect("fidl failed").expect("remove failed");

                // Removing a non-existent volume should fail.
                volumes_proxy
                    .remove("test")
                    .await
                    .expect("fidl failed")
                    .expect_err("remove failed");

                // Create the same volume again and it should now succeed.
                let (_dir_proxy, server_end) =
                    fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
                        .expect("create_proxy failed");
                volumes_proxy
                    .create("test", None, server_end)
                    .await
                    .expect("fidl failed")
                    .expect("create failed");

                fs_admin_proxy.shutdown().await.expect("shutdown failed");
            }
            .boxed()
        })
        .await;
    }

    #[fasync::run(2, test)]
    async fn test_volumes_enumeration() {
        run_test(|client, _| {
            let volumes_proxy = connect_to_protocol_at_dir_svc::<VolumesMarker>(client)
                .expect("Unable to connect to Volumes protocol");

            let (volumes_dir_proxy, server_end) =
                fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
                    .expect("create_proxy failed");
            client
                .open(
                    fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                    0,
                    "volumes",
                    ServerEnd::new(server_end.into_channel()),
                )
                .expect("open failed");

            let fs_admin_proxy = connect_to_protocol_at_dir_svc::<AdminMarker>(client)
                .expect("Unable to connect to Admin protocol");

            async move {
                let (_dir_proxy, server_end) =
                    fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
                        .expect("create_proxy failed");
                volumes_proxy
                    .create("test", None, server_end)
                    .await
                    .expect("fidl failed")
                    .expect("create failed");

                assert_eq!(
                    readdir(&volumes_dir_proxy)
                        .await
                        .expect("readdir failed")
                        .iter()
                        .map(|d| d.name.as_str())
                        .collect::<HashSet<_>>(),
                    HashSet::from(["default", "test"])
                );

                fs_admin_proxy.shutdown().await.expect("shutdown failed");
            }
            .boxed()
        })
        .await;
    }
}
