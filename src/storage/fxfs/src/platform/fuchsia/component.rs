// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        filesystem::{mkfs, FxFilesystem, OpenFxFilesystem, OpenOptions},
        fsck,
        object_store::volume::root_volume,
        platform::{
            fuchsia::{
                errors::map_to_status, runtime::DEFAULT_VOLUME_NAME, volume::FxVolumeAndRoot,
                volumes_directory::VolumesDirectory,
            },
            RemoteCrypt,
        },
    },
    anyhow::{Context, Error},
    fidl::endpoints::{ClientEnd, DiscoverableProtocolMarker, ServerEnd},
    fidl_fuchsia_fs::{AdminMarker, AdminRequest, AdminRequestStream},
    fidl_fuchsia_fs_startup::{
        CheckOptions, FormatOptions, StartOptions, StartupMarker, StartupRequest,
        StartupRequestStream,
    },
    fidl_fuchsia_fxfs::CryptProxy,
    fidl_fuchsia_hardware_block::BlockMarker,
    fidl_fuchsia_io as fio,
    fs_inspect::{FsInspect, FsInspectTree},
    fuchsia_async as fasync,
    fuchsia_component::server::MissingStartupHandle,
    fuchsia_runtime::HandleType,
    fuchsia_zircon as zx,
    futures::TryStreamExt,
    inspect_runtime::service::{TreeServerSendPreference, TreeServerSettings},
    remote_block_device::RemoteBlockClient,
    std::sync::{Arc, Mutex, Weak},
    storage_device::{block_device::BlockDevice, DeviceHolder},
    vfs::{
        directory::{entry::DirectoryEntry, helper::DirectlyMutable},
        execution_scope::ExecutionScope,
        path::Path,
        registry::token_registry,
        remote::remote_boxed_with_type,
    },
};

fn map_to_raw_status(e: Error) -> zx::sys::zx_status_t {
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
    volumes: VolumesDirectory,
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
        let registry = token_registry::Simple::new();
        let outgoing_dir = vfs::directory::immutable::simple();
        Arc::new(Self {
            state: Mutex::new(State::PreStart { queued: Vec::new() }),
            scope: ExecutionScope::build().token_registry(registry).new(),
            outgoing_dir,
        })
    }

    /// Runs Fxfs as a component.
    // TODO(fxbug.dev/99591): Add support for lifecycle methods.
    pub async fn run(self: Arc<Self>) -> Result<(), Error> {
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
            fuchsia_runtime::take_startup_handle(HandleType::DirectoryRequest.into())
                .ok_or(MissingStartupHandle)?
                .into(),
        );

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
        log::info!("Mounting");
        // TODO(fxbug.dev/99591): When runring as a component, it's possible for us to end up with
        // orphaned filesystems in the case where a client crashes and is unable to send
        // Admin/Shutdown and this causes problems in some tests which do this deliberately.  To
        // address this, we forcibly terminate any existing filesystem if there's an attempt to
        // start another instance.  This is a problem whilst we are using static routing.  We can
        // probably address this by switching to dynamic routing: e.g. change the Start method so
        // that it supplies an export root and then we can notice when the client goes away.
        let state = self.state.lock().unwrap().maybe_stop();
        if let Some(state) = state {
            // TODO(fxbug.dev/99591): There's a race here that we should think about: it's
            // possible that Shutdown has been called on an old filesystem but hasn't completed,
            // in which case this we'll skip over here and possibly fail below.
            let _ = self
                .outgoing_dir
                .remove_entry_impl("root".into(), /* must_be_directory: */ false);
            state.volumes.terminate().await;
            let _ = state.fs.close().await;
        }
        let client = RemoteBlockClient::new(device.into_channel()).await?;
        let fs = FxFilesystem::open_with_options(
            DeviceHolder::new(BlockDevice::new(Box::new(client), options.read_only).await?),
            OpenOptions { read_only: options.read_only, ..Default::default() },
        )
        .await?;
        let volumes = VolumesDirectory::new(root_volume(&fs).await?).await?;
        let crypt = Arc::new(RemoteCrypt::new(CryptProxy::new(fasync::Channel::from_channel(
            options.crypt.ok_or(zx::Status::INVALID_ARGS)?.into_channel(),
        )?)));
        // TODO(fxbug.dev/99182): We should eventually not open the default volume.
        let volume = volumes
            .open_or_create_volume(DEFAULT_VOLUME_NAME, Some(crypt), /* create_only: */ false)
            .await?;
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
        self.start_serving(&volume).await?;
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
        log::info!("Mounted");
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
            crypt,
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
                log::info!("Received shutdown request");
                let state = self.state.lock().unwrap().maybe_stop();
                if let Some(state) = state {
                    let _ = self
                        .outgoing_dir
                        .remove_entry_impl("root".into(), /* must_be_directory: */ false);
                    state.volumes.terminate().await;
                    let _ = state.fs.close().await;
                }
                log::info!("Filesystem terminated");
                responder
                    .send()
                    .unwrap_or_else(|e| log::warn!("Failed to send shutdown response: {}", e));
                return Ok(true);
            }
        }
    }

    /// Serves this volume on `outgoing_dir`.
    async fn start_serving(&self, volume: &FxVolumeAndRoot) -> Result<(), Error> {
        self.outgoing_dir.add_entry_impl(
            "root".to_string(),
            volume.root().clone(),
            /* overwrite: */ true,
        )?;

        Ok(())
    }
}
