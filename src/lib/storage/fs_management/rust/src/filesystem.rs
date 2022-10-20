// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Contains the asynchronous version of [`Filesystem`][`crate::Filesystem`].

use {
    crate::{
        error::{CommandError, KillError, QueryError, ShutdownError},
        launch_process, FSConfig, Mode,
    },
    anyhow::{anyhow, bail, ensure, Error},
    cstr::cstr,
    fdio::SpawnAction,
    fidl::{
        encoding::Decodable,
        endpoints::{create_endpoints, create_proxy, ClientEnd, Proxy, ServerEnd},
    },
    fidl_fuchsia_component::{self as fcomponent, RealmMarker},
    fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_fs_startup::{
        CheckOptions, CompressionAlgorithm, EvictionPolicyOverride, FormatOptions, StartOptions,
        StartupMarker,
    },
    fidl_fuchsia_fxfs::MountOptions,
    fidl_fuchsia_io as fio,
    fuchsia_async::OnSignals,
    fuchsia_component::client::{
        connect_to_named_protocol_at_dir_root, connect_to_protocol,
        connect_to_protocol_at_dir_root, open_childs_exposed_directory,
    },
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon as zx,
    fuchsia_zircon::{AsHandleRef, Channel, Handle, Process, Signals, Status, Task},
    std::{
        collections::HashMap,
        sync::{
            atomic::{AtomicU64, Ordering},
            Arc,
        },
    },
    tracing::warn,
};

/// Asynchronously manages a block device for filesystem operations.
pub struct Filesystem<FSC> {
    config: FSC,
    block_device: fio::NodeProxy,
    component: Option<Arc<ComponentInstance>>,
}

// Used to disambiguate children in our component collection.
const COLLECTION_NAME: &str = "fs-collection";
static COLLECTION_COUNTER: AtomicU64 = AtomicU64::new(0);

impl<FSC: FSConfig> Filesystem<FSC> {
    pub fn config(&self) -> &FSC {
        &self.config
    }

    /// Creates a new `Filesystem` with the block device represented by `node_proxy`.
    pub fn from_node(node_proxy: fio::NodeProxy, config: FSC) -> Self {
        Self { config, block_device: node_proxy, component: None }
    }

    /// Creates a new `Filesystem` from the block device at the given path.
    pub fn from_path(path: &str, config: FSC) -> Result<Self, Error> {
        let (client, server) = create_endpoints::<fio::NodeMarker>()?;
        fdio::service_connect(&path, server.into_channel())?;
        Ok(Self::from_node(client.into_proxy()?, config))
    }

    /// Creates a new `Filesystem` with the block device represented by `channel`.
    pub fn from_channel(channel: Channel, config: FSC) -> Result<Self, Error> {
        Ok(Self::from_node(ClientEnd::<fio::NodeMarker>::new(channel).into_proxy()?, config))
    }

    // Clone a Channel to the block device.
    fn get_block_handle(&self) -> Result<Handle, fidl::Error> {
        let (block_device, server) = Channel::create().map_err(fidl::Error::ChannelPairCreate)?;
        self.block_device.clone(fio::OpenFlags::CLONE_SAME_RIGHTS, ServerEnd::new(server))?;
        Ok(block_device.into())
    }

    async fn get_component_exposed_dir(&mut self) -> Result<fio::DirectoryProxy, Error> {
        if let Some(component) = &self.component {
            return open_childs_exposed_directory(
                component.name(),
                Some(COLLECTION_NAME.to_string()),
            )
            .await;
        }

        // Try and connect to a static child.
        let mode = self.config.mode();
        let component_name = mode.component_name().unwrap();
        let realm_proxy = connect_to_protocol::<RealmMarker>()?;
        let (directory_proxy, server_end) = create_proxy::<fio::DirectoryMarker>()?;
        match realm_proxy
            .open_exposed_dir(
                &mut fdecl::ChildRef { name: component_name.to_string(), collection: None },
                server_end,
            )
            .await?
        {
            Ok(()) => return Ok(directory_proxy),
            Err(e) => {
                if e != fcomponent::Error::InstanceNotFound {
                    return Err(anyhow!("open_exposed_dir failed: {:?}", e));
                }
            }
        }

        // We failed to connect to a static child, so we need to launch a component in our
        // collection.  We need a unique name, so we pull in the process Koid here since it's
        // possible for the same binary in a component to be launched multiple times and we don't
        // want to collide with children created by other processes.
        let name = format!(
            "{}-{}-{}",
            component_name,
            fuchsia_runtime::process_self().get_koid().unwrap().raw_koid(),
            COLLECTION_COUNTER.fetch_add(1, Ordering::Relaxed)
        );

        // Launch a new component in our collection.
        realm_proxy
            .create_child(
                &mut fdecl::CollectionRef { name: COLLECTION_NAME.to_string() },
                fdecl::Child {
                    name: Some(name.clone()),
                    url: Some(format!("#meta/{}.cm", component_name)),
                    startup: Some(fdecl::StartupMode::Lazy),
                    environment: None,
                    ..fdecl::Child::EMPTY
                },
                fcomponent::CreateChildArgs::EMPTY,
            )
            .await?
            .map_err(|e| anyhow!("create_child failed: {:?}", e))?;

        let component = Arc::new(ComponentInstance(name));

        let proxy =
            open_childs_exposed_directory(component.name(), Some(COLLECTION_NAME.to_string()))
                .await?;

        self.component = Some(component);
        Ok(proxy)
    }

    /// Runs `mkfs`, which formats the filesystem onto the block device.
    ///
    /// Which flags are passed to the `mkfs` command are controlled by the config this `Filesystem`
    /// was created with.
    ///
    /// See [`FSConfig`].
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if the filesystem process failed to launch or returned a non-zero exit code.
    pub async fn format(&mut self) -> Result<(), Error> {
        match self.config.mode() {
            Mode::Component { .. } => {
                let exposed_dir = self.get_component_exposed_dir().await?;
                let proxy = connect_to_protocol_at_dir_root::<StartupMarker>(&exposed_dir)?;
                proxy
                    .format(self.get_block_handle()?.into(), &mut FormatOptions::new_empty())
                    .await?
                    .map_err(Status::from_raw)?;
            }
            Mode::Legacy(mut config) => {
                // SpawnAction is not Send, so make sure it is dropped before any `await`s.
                let process = {
                    let mut args = vec![config.binary_path, cstr!("mkfs")];
                    args.append(&mut config.generic_args);
                    args.append(&mut config.format_args);
                    let actions = vec![
                        // device handle is passed in as a PA_USER0 handle at argument 1
                        SpawnAction::add_handle(
                            HandleInfo::new(HandleType::User0, 1),
                            self.get_block_handle()?,
                        ),
                    ];
                    launch_process(&args, actions)?
                };
                wait_for_successful_exit(process).await?;
            }
        }
        Ok(())
    }

    /// Runs `fsck`, which checks and optionally repairs the filesystem on the block device.
    ///
    /// Which flags are passed to the `fsck` command are controlled by the config this `Filesystem`
    /// was created with.
    ///
    /// See [`FSConfig`].
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if the filesystem process failed to launch or returned a non-zero exit code.
    pub async fn fsck(&mut self) -> Result<(), Error> {
        match self.config.mode() {
            Mode::Component { .. } => {
                let exposed_dir = self.get_component_exposed_dir().await?;
                let proxy = connect_to_protocol_at_dir_root::<StartupMarker>(&exposed_dir)?;
                let mut options = CheckOptions::new_empty();
                proxy
                    .check(self.get_block_handle()?.into(), &mut options)
                    .await?
                    .map_err(Status::from_raw)?;
            }
            Mode::Legacy(mut config) => {
                // SpawnAction is not Send, so make sure it is dropped before any `await`s.
                let process = {
                    let mut args = vec![config.binary_path, cstr!("fsck")];
                    args.append(&mut config.generic_args);
                    let actions = vec![
                        // device handle is passed in as a PA_USER0 handle at argument 1
                        SpawnAction::add_handle(
                            HandleInfo::new(HandleType::User0, 1),
                            self.get_block_handle()?,
                        ),
                    ];
                    launch_process(&args, actions)?
                };
                wait_for_successful_exit(process).await?;
            }
        }
        Ok(())
    }

    /// Serves the filesystem on the block device and returns a [`ServingSingleVolumeFilesystem`]
    /// representing the running filesystem process.
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if serving the filesystem failed.
    pub async fn serve(&mut self) -> Result<ServingSingleVolumeFilesystem, Error> {
        if self.config.is_multi_volume() {
            bail!("Can't serve a multivolume filesystem; use serve_multi_volume");
        }
        if let Mode::Component { reuse_component_after_serving, .. } = self.config.mode() {
            let exposed_dir = self.get_component_exposed_dir().await?;
            let proxy = connect_to_protocol_at_dir_root::<StartupMarker>(&exposed_dir)?;
            let mut options = StartOptions::new_empty();
            options.write_compression_algorithm = CompressionAlgorithm::ZstdChunked;
            options.cache_eviction_policy_override = EvictionPolicyOverride::None;
            proxy
                .start(self.get_block_handle()?.into(), &mut options)
                .await?
                .map_err(Status::from_raw)?;

            let (root_dir, server_end) = create_endpoints::<fio::NodeMarker>()?;
            exposed_dir.open(
                fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::POSIX_EXECUTABLE
                    | fio::OpenFlags::POSIX_WRITABLE,
                0,
                "root",
                server_end,
            )?;
            let component = self.component.clone();
            if !reuse_component_after_serving {
                self.component = None;
            }
            Ok(ServingSingleVolumeFilesystem {
                process: None,
                _component: component,
                exposed_dir,
                root_dir: ClientEnd::<fio::DirectoryMarker>::new(root_dir.into_channel())
                    .into_proxy()?,
                binding: None,
            })
        } else {
            self.serve_legacy().await
        }
    }

    /// Serves the filesystem on the block device and returns a [`ServingMultiVolumeFilesystem`]
    /// representing the running filesystem process.  No volumes are opened; clients have to do that
    /// explicitly.
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if serving the filesystem failed.
    pub async fn serve_multi_volume(&mut self) -> Result<ServingMultiVolumeFilesystem, Error> {
        if !self.config.is_multi_volume() {
            bail!("Can't serve_multi_volume a single-volume filesystem; use serve");
        }
        if let Mode::Component { .. } = self.config.mode() {
            let exposed_dir = self.get_component_exposed_dir().await?;
            let proxy = connect_to_protocol_at_dir_root::<StartupMarker>(&exposed_dir)?;
            proxy
                .start(self.get_block_handle()?.into(), &mut StartOptions::new_empty())
                .await?
                .map_err(Status::from_raw)?;

            Ok(ServingMultiVolumeFilesystem {
                _component: self.component.clone(),
                exposed_dir,
                volumes: HashMap::default(),
            })
        } else {
            bail!("Can't serve a multivolume filesystem which isn't componentized")
        }
    }

    // TODO(fxbug.dev/87511): This is temporarily public so that we can migrate an OOT user.
    pub async fn serve_legacy(&self) -> Result<ServingSingleVolumeFilesystem, Error> {
        let (export_root, server_end) = create_proxy::<fio::DirectoryMarker>()?;

        let mode = self.config.mode();
        let mut config = mode.into_legacy_config().unwrap();

        // SpawnAction is not Send, so make sure it is dropped before any `await`s.
        let process = {
            let mut args = vec![config.binary_path, cstr!("mount")];
            args.append(&mut config.generic_args);
            args.append(&mut config.mount_args);
            let actions = vec![
                // export root handle is passed in as a PA_DIRECTORY_REQUEST handle at argument 0
                SpawnAction::add_handle(
                    HandleInfo::new(HandleType::DirectoryRequest, 0),
                    server_end.into_channel().into(),
                ),
                // device handle is passed in as a PA_USER0 handle at argument 1
                SpawnAction::add_handle(
                    HandleInfo::new(HandleType::User0, 1),
                    self.get_block_handle()?,
                ),
            ];

            launch_process(&args, actions)?
        };

        // Wait until the filesystem is ready to take incoming requests.
        let (root_dir, server_end) = create_proxy::<fio::DirectoryMarker>()?;
        export_root.open(
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::POSIX_EXECUTABLE
                | fio::OpenFlags::POSIX_WRITABLE,
            0,
            "root",
            server_end.into_channel().into(),
        )?;
        let _: Vec<_> = root_dir.query().await?;

        Ok(ServingSingleVolumeFilesystem {
            process: Some(process),
            _component: None,
            exposed_dir: export_root,
            root_dir,
            binding: None,
        })
    }
}

// Destroys the child when dropped.
struct ComponentInstance(/* name: */ String);

impl ComponentInstance {
    fn name(&self) -> &str {
        &self.0
    }
}

impl Drop for ComponentInstance {
    fn drop(&mut self) {
        if let Ok(realm_proxy) = connect_to_protocol::<RealmMarker>() {
            let _ = realm_proxy.destroy_child(&mut fdecl::ChildRef {
                name: self.0.clone(),
                collection: Some(COLLECTION_NAME.to_string()),
            });
        }
    }
}

/// Manages the binding of a `fuchsia_io::DirectoryProxy` into the local namespace.  When the object
/// is dropped, the binding is removed.
#[derive(Default)]
pub struct NamespaceBinding(String);

impl NamespaceBinding {
    pub fn create(root_dir: &fio::DirectoryProxy, path: String) -> Result<NamespaceBinding, Error> {
        let (client_end, server_end) = create_endpoints()?;
        root_dir
            .clone(fio::OpenFlags::CLONE_SAME_RIGHTS, ServerEnd::new(server_end.into_channel()))?;
        let namespace = fdio::Namespace::installed()?;
        namespace.bind(&path, client_end)?;
        Ok(Self(path))
    }
}

impl std::ops::Deref for NamespaceBinding {
    type Target = str;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl Drop for NamespaceBinding {
    fn drop(&mut self) {
        if let Ok(namespace) = fdio::Namespace::installed() {
            let _ = namespace.unbind(&self.0);
        }
    }
}

// TODO(fxbug.dev/93066): Soft migration; remove this after completion
pub type ServingFilesystem = ServingSingleVolumeFilesystem;

/// Asynchronously manages a serving filesystem. Created from [`Filesystem::serve()`].
pub struct ServingSingleVolumeFilesystem {
    // If the filesystem is running as a component, there will be no process.
    process: Option<Process>,
    _component: Option<Arc<ComponentInstance>>,
    exposed_dir: fio::DirectoryProxy,
    root_dir: fio::DirectoryProxy,

    // The path in the local namespace that this filesystem is bound to (optional).
    binding: Option<NamespaceBinding>,
}

impl ServingSingleVolumeFilesystem {
    /// Returns a proxy to the root directory of the serving filesystem.
    pub fn root(&self) -> &fio::DirectoryProxy {
        &self.root_dir
    }

    /// Binds the root directory being served by this filesystem to a path in the local namespace.
    /// The path must be absolute, containing no "." nor ".." entries.  The binding will be dropped
    /// when self is dropped.  Only one binding is supported.
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if binding failed.
    pub fn bind_to_path(&mut self, path: &str) -> Result<(), Error> {
        ensure!(self.binding.is_none(), "Already bound");
        self.binding = Some(NamespaceBinding::create(&self.root_dir, path.to_string())?);
        Ok(())
    }

    pub fn bound_path(&self) -> Option<&str> {
        self.binding.as_deref()
    }

    /// Returns a [`FilesystemInfo`] object containing information about the serving filesystem.
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if querying the filesystem failed.
    pub async fn query(&self) -> Result<Box<fio::FilesystemInfo>, QueryError> {
        let (status, info) = self.root_dir.query_filesystem().await?;
        Status::ok(status).map_err(QueryError::DirectoryQuery)?;
        info.ok_or(QueryError::DirectoryEmptyResult)
    }

    /// Attempts to shutdown the filesystem using the
    /// [`fidl_fuchsia_fs::AdminProxy::shutdown()`] FIDL method and waiting for the filesystem
    /// process to terminate.
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if the shutdown failed or the filesystem process did not terminate.
    pub async fn shutdown(mut self) -> Result<(), ShutdownError> {
        async fn do_shutdown(exposed_dir: &fio::DirectoryProxy) -> Result<(), Error> {
            connect_to_protocol_at_dir_root::<fidl_fuchsia_fs::AdminMarker>(exposed_dir)?
                .shutdown()
                .await?;
            Ok(())
        }

        if let Err(e) = do_shutdown(&self.exposed_dir).await {
            if let Some(process) = self.process.take() {
                if process.kill().is_ok() {
                    let _ = OnSignals::new(&process, Signals::PROCESS_TERMINATED).await;
                }
            }
            return Err(e.into());
        }

        if let Some(process) = self.process.take() {
            let _ = OnSignals::new(&process, Signals::PROCESS_TERMINATED)
                .await
                .map_err(ShutdownError::ProcessTerminatedSignal)?;

            let info = process.info().map_err(ShutdownError::GetProcessReturnCode)?;
            if info.return_code != 0 {
                warn!(
                    code = info.return_code,
                    "process returned non-zero exit code after shutdown"
                );
            }
        }
        Ok(())
    }

    /// Attempts to kill the filesystem process and waits for the process to terminate.
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if the filesystem process could not be terminated. There is no way to
    /// recover the [`Filesystem`] from this error.
    pub async fn kill(mut self) -> Result<(), Error> {
        // Prevent the drop impl from killing the process again.
        if let Some(process) = self.process.take() {
            process.kill().map_err(KillError::TaskKill)?;
            let _ = OnSignals::new(&process, Signals::PROCESS_TERMINATED)
                .await
                .map_err(KillError::ProcessTerminatedSignal)?;
        } else {
            // For components, just shut down the filesystem.
            self.shutdown().await?;
        }
        Ok(())
    }
}

impl Drop for ServingSingleVolumeFilesystem {
    fn drop(&mut self) {
        // Make a best effort attempt to shut down the filesystem via the admin service.
        if let Ok(proxy) =
            connect_to_protocol_at_dir_root::<fidl_fuchsia_fs::AdminMarker>(&self.exposed_dir)
        {
            // Nothing to do if this returns an error.
            let _ =
                fidl_fuchsia_fs::AdminSynchronousProxy::new(proxy.into_channel().unwrap().into())
                    .shutdown(zx::Time::INFINITE);
        }
    }
}

/// Asynchronously manages a serving multivolume filesystem. Created from
/// [`Filesystem::serve_multi_volume()`].
pub struct ServingMultiVolumeFilesystem {
    _component: Option<Arc<ComponentInstance>>,
    exposed_dir: fio::DirectoryProxy,
    volumes: HashMap<String, ServingVolume>,
}

/// Represents an opened volume in a [`ServingMultiVolumeFilesystem'] instance.
pub struct ServingVolume {
    root_dir: fio::DirectoryProxy,
    binding: Option<NamespaceBinding>,
}

impl ServingVolume {
    /// Returns a proxy to the root directory of the serving volume.
    pub fn root(&self) -> &fio::DirectoryProxy {
        &self.root_dir
    }

    /// Binds the root directory being served by this filesystem to a path in the local namespace.
    /// The path must be absolute, containing no "." nor ".." entries.  The binding will be dropped
    /// when self is dropped.  Only one binding is supported.
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if binding failed.
    pub fn bind_to_path(&mut self, path: &str) -> Result<(), Error> {
        ensure!(self.binding.is_none(), "Already bound");
        self.binding = Some(NamespaceBinding::create(&self.root_dir, path.to_string())?);
        Ok(())
    }

    pub fn bound_path(&self) -> Option<&str> {
        self.binding.as_deref()
    }

    /// Returns a [`FilesystemInfo`] object containing information about the serving volume.
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if querying the filesystem failed.
    pub async fn query(&self) -> Result<Box<fio::FilesystemInfo>, QueryError> {
        let (status, info) = self.root_dir.query_filesystem().await?;
        Status::ok(status).map_err(QueryError::DirectoryQuery)?;
        info.ok_or(QueryError::DirectoryEmptyResult)
    }
}

impl ServingMultiVolumeFilesystem {
    /// Gets a reference to the given volume, if it's already open.
    pub fn volume(&self, volume: &str) -> Option<&ServingVolume> {
        self.volumes.get(volume)
    }

    /// Gets a mutable reference to the given volume, if it's already open.
    pub fn volume_mut(&mut self, volume: &str) -> Option<&mut ServingVolume> {
        self.volumes.get_mut(volume)
    }

    #[cfg(test)]
    pub fn close_volume(&mut self, volume: &str) {
        self.volumes.remove(volume);
    }

    /// Creates the volume.  Fails if the volume already exists.
    /// If `crypt` is set, the volume will be encrypted using the provided Crypt instance.
    pub async fn create_volume(
        &mut self,
        volume: &str,
        crypt: Option<ClientEnd<fidl_fuchsia_fxfs::CryptMarker>>,
    ) -> Result<&mut ServingVolume, Error> {
        ensure!(!self.volumes.contains_key(volume), "Already bound");
        let (exposed_dir, server) = create_proxy::<fio::DirectoryMarker>()?;
        connect_to_protocol_at_dir_root::<fidl_fuchsia_fxfs::VolumesMarker>(&self.exposed_dir)?
            .create(volume, crypt, server)
            .await?
            .map_err(|e| anyhow!(zx::Status::from_raw(e)))?;
        self.insert_volume(volume.to_string(), exposed_dir).await
    }

    /// Mounts an existing volume.  Fails if the volume is already mounted or doesn't exist.
    /// If `crypt` is set, the volume will be decrypted using the provided Crypt instance.
    pub async fn open_volume(
        &mut self,
        volume: &str,
        crypt: Option<ClientEnd<fidl_fuchsia_fxfs::CryptMarker>>,
    ) -> Result<&mut ServingVolume, Error> {
        ensure!(!self.volumes.contains_key(volume), "Already bound");
        let (exposed_dir, server) = create_proxy::<fio::DirectoryMarker>()?;
        let path = format!("volumes/{}", volume);
        connect_to_named_protocol_at_dir_root::<fidl_fuchsia_fxfs::VolumeMarker>(
            &self.exposed_dir,
            &path,
        )?
        .mount(server, &mut MountOptions { crypt })
        .await?
        .map_err(|e| anyhow!(zx::Status::from_raw(e)))?;

        self.insert_volume(volume.to_string(), exposed_dir).await
    }

    pub async fn check_volume(
        &mut self,
        volume: &str,
        crypt: Option<ClientEnd<fidl_fuchsia_fxfs::CryptMarker>>,
    ) -> Result<(), Error> {
        ensure!(!self.volumes.contains_key(volume), "Already bound");
        let path = format!("volumes/{}", volume);
        connect_to_named_protocol_at_dir_root::<fidl_fuchsia_fxfs::VolumeMarker>(
            &self.exposed_dir,
            &path,
        )?
        .check(&mut fidl_fuchsia_fxfs::CheckOptions { crypt })
        .await?
        .map_err(|e| anyhow!(zx::Status::from_raw(e)))?;
        Ok(())
    }

    async fn insert_volume(
        &mut self,
        volume: String,
        exposed_dir: fio::DirectoryProxy,
    ) -> Result<&mut ServingVolume, Error> {
        let (root_dir, server_end) = create_endpoints::<fio::NodeMarker>()?;
        exposed_dir.open(
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::POSIX_EXECUTABLE
                | fio::OpenFlags::POSIX_WRITABLE,
            0,
            "root",
            server_end,
        )?;
        Ok(self.volumes.entry(volume).or_insert(ServingVolume {
            root_dir: ClientEnd::<fio::DirectoryMarker>::new(root_dir.into_channel())
                .into_proxy()?,
            binding: None,
        }))
    }

    /// Attempts to shutdown the filesystem using the [`fidl_fuchsia_fs::AdminProxy::shutdown()`]
    /// FIDL method.
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if the shutdown failed.
    pub async fn shutdown(self) -> Result<(), ShutdownError> {
        connect_to_protocol_at_dir_root::<fidl_fuchsia_fs::AdminMarker>(&self.exposed_dir)?
            .shutdown()
            .await?;
        Ok(())
    }
}

impl Drop for ServingMultiVolumeFilesystem {
    fn drop(&mut self) {
        // Make a best effort attempt to shut down the filesystem via the admin service.
        if let Ok(proxy) =
            connect_to_protocol_at_dir_root::<fidl_fuchsia_fs::AdminMarker>(&self.exposed_dir)
        {
            // Nothing to do if this returns an error.
            let _ =
                fidl_fuchsia_fs::AdminSynchronousProxy::new(proxy.into_channel().unwrap().into())
                    .shutdown(zx::Time::INFINITE);
        }
    }
}

async fn wait_for_successful_exit(process: Process) -> Result<(), CommandError> {
    let _ = OnSignals::new(&process, Signals::PROCESS_TERMINATED)
        .await
        .map_err(CommandError::ProcessTerminatedSignal)?;

    let info = process.info().map_err(CommandError::GetProcessReturnCode)?;
    if info.return_code == 0 {
        Ok(())
    } else {
        Err(CommandError::ProcessNonZeroReturnCode(info.return_code))
    }
}

#[cfg(test)]
mod tests {
    use std::io::Read;

    use {
        super::*,
        crate::{BlobCompression, BlobEvictionPolicy, Blobfs, Factoryfs, Fxfs, Minfs},
        fidl_fuchsia_io as fio, fuchsia_async as fasync,
        fuchsia_zircon::HandleBased,
        ramdevice_client::RamdiskClient,
        std::{
            io::{Seek, Write},
            time::Duration,
        },
    };

    fn ramdisk(block_size: u64) -> RamdiskClient {
        ramdevice_client::wait_for_device(
            "/dev/sys/platform/00:00:2d/ramctl",
            std::time::Duration::from_secs(60),
        )
        .unwrap();
        RamdiskClient::create(block_size, 1 << 16).unwrap()
    }

    fn new_fs<FSC: FSConfig>(ramdisk: &RamdiskClient, config: FSC) -> Filesystem<FSC> {
        Filesystem::from_channel(ramdisk.open().unwrap(), config).unwrap()
    }

    #[fuchsia::test]
    async fn blobfs_custom_config() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let config = Blobfs {
            verbose: true,
            readonly: true,
            blob_deprecated_padded_format: false,
            blob_compression: Some(BlobCompression::Uncompressed),
            blob_eviction_policy: Some(BlobEvictionPolicy::EvictImmediately),
        };
        let mut blobfs = new_fs(&ramdisk, config);

        blobfs.format().await.expect("failed to format blobfs");
        blobfs.fsck().await.expect("failed to fsck blobfs");
        let _ = blobfs.serve().await.expect("failed to serve blobfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn blobfs_format_fsck_success() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let mut blobfs = new_fs(&ramdisk, Blobfs::default());

        blobfs.format().await.expect("failed to format blobfs");
        blobfs.fsck().await.expect("failed to fsck blobfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn blobfs_format_fsck_error() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let mut blobfs = new_fs(&ramdisk, Blobfs::default());
        blobfs.format().await.expect("failed to format blobfs");
        let device_channel = ramdisk.open().expect("failed to get channel to device");

        // force fsck to fail by stomping all over one of blobfs's metadata blocks after formatting
        // TODO(fxbug.dev/35860): corrupt something other than the superblock
        {
            let mut file = fdio::create_fd::<std::fs::File>(device_channel.into_handle())
                .expect("failed to convert to file descriptor");
            let mut bytes: Vec<u8> = std::iter::repeat(0xff).take(block_size as usize).collect();
            file.write_all(&mut bytes).expect("failed to write to device");
        }

        blobfs.fsck().await.expect_err("fsck succeeded when it shouldn't have");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn blobfs_format_serve_write_query_restart_read_shutdown() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let mut blobfs = new_fs(&ramdisk, Blobfs::default());

        blobfs.format().await.expect("failed to format blobfs");

        let serving = blobfs.serve().await.expect("failed to serve blobfs the first time");

        // snapshot of FilesystemInfo
        let fs_info1 =
            serving.query().await.expect("failed to query filesystem info after first serving");

        // pre-generated merkle test fixture data
        let merkle = "be901a14ec42ee0a8ee220eb119294cdd40d26d573139ee3d51e4430e7d08c28";
        let content = String::from("test content").into_bytes();

        {
            let test_file = fuchsia_fs::directory::open_file(
                serving.root(),
                merkle,
                fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE,
            )
            .await
            .expect("failed to create test file");
            let () = test_file
                .resize(content.len() as u64)
                .await
                .expect("failed to send resize FIDL")
                .map_err(Status::from_raw)
                .expect("failed to resize file");
            let _: u64 = test_file
                .write(&content)
                .await
                .expect("failed to write to test file")
                .map_err(Status::from_raw)
                .expect("write error");
        }

        // check against the snapshot FilesystemInfo
        let fs_info2 = serving.query().await.expect("failed to query filesystem info after write");
        assert_eq!(
            fs_info2.used_bytes - fs_info1.used_bytes,
            fs_info2.block_size as u64 // assuming content < 8K
        );

        serving.shutdown().await.expect("failed to shutdown blobfs the first time");
        let serving = blobfs.serve().await.expect("failed to serve blobfs the second time");
        {
            let test_file = fuchsia_fs::directory::open_file(
                serving.root(),
                merkle,
                fio::OpenFlags::RIGHT_READABLE,
            )
            .await
            .expect("failed to open test file");
            let read_content =
                fuchsia_fs::file::read(&test_file).await.expect("failed to read from test file");
            assert_eq!(content, read_content);
        }

        // once more check against the snapshot FilesystemInfo
        let fs_info3 = serving.query().await.expect("failed to query filesystem info after read");
        assert_eq!(
            fs_info3.used_bytes - fs_info1.used_bytes,
            fs_info3.block_size as u64 // assuming content < 8K
        );

        serving.shutdown().await.expect("failed to shutdown blobfs the second time");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn blobfs_bind_to_path() {
        let block_size = 512;
        let merkle = "be901a14ec42ee0a8ee220eb119294cdd40d26d573139ee3d51e4430e7d08c28";
        let test_content = b"test content";
        let ramdisk = ramdisk(block_size);
        let mut blobfs = new_fs(&ramdisk, Blobfs::default());

        blobfs.format().await.expect("failed to format blobfs");
        let mut serving = blobfs.serve().await.expect("failed to serve blobfs");
        serving.bind_to_path("/test-blobfs-path").expect("bind_to_path failed");
        let test_path = format!("/test-blobfs-path/{}", merkle);

        {
            let mut file = std::fs::File::create(&test_path).expect("failed to create test file");
            file.set_len(test_content.len() as u64).expect("failed to set size");
            file.write_all(test_content).expect("write bytes");
        }

        {
            let mut file = std::fs::File::open(&test_path).expect("failed to open test file");
            let mut buf = Vec::new();
            file.read_to_end(&mut buf).expect("failed to read test file");
            assert_eq!(buf, test_content);
        }

        serving.shutdown().await.expect("failed to shutdown blobfs");

        std::fs::File::open(&test_path).expect_err("test file was not unbound");
    }

    #[fuchsia::test]
    async fn minfs_custom_config() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let config = Minfs { verbose: true, readonly: true, fsck_after_every_transaction: true };
        let mut minfs = new_fs(&ramdisk, config);

        minfs.format().await.expect("failed to format minfs");
        minfs.fsck().await.expect("failed to fsck minfs");
        let _ = minfs.serve().await.expect("failed to serve minfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn minfs_format_fsck_success() {
        let block_size = 8192;
        let ramdisk = ramdisk(block_size);
        let mut minfs = new_fs(&ramdisk, Minfs::default());

        minfs.format().await.expect("failed to format minfs");
        minfs.fsck().await.expect("failed to fsck minfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn minfs_format_fsck_error() {
        let block_size = 8192;
        let ramdisk = ramdisk(block_size);
        let mut minfs = new_fs(&ramdisk, Minfs::default());

        minfs.format().await.expect("failed to format minfs");

        // force fsck to fail by stomping all over one of minfs's metadata blocks after formatting
        {
            let device_channel = ramdisk.open().expect("failed to get channel to device");
            let mut file = fdio::create_fd::<std::fs::File>(device_channel.into_handle())
                .expect("failed to convert to file descriptor");

            // when minfs isn't on an fvm, the location for it's bitmap offset is the 8th block.
            // TODO(fxbug.dev/35861): parse the superblock for this offset and the block size.
            let bitmap_block_offset = 8;
            let bitmap_offset = block_size * bitmap_block_offset;

            let mut stomping_bytes: Vec<u8> =
                std::iter::repeat(0xff).take(block_size as usize).collect();
            let actual_offset = file
                .seek(std::io::SeekFrom::Start(bitmap_offset))
                .expect("failed to seek to bitmap");
            assert_eq!(actual_offset, bitmap_offset);
            file.write_all(&mut stomping_bytes).expect("failed to write to device");
        }

        minfs.fsck().await.expect_err("fsck succeeded when it shouldn't have");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn minfs_format_serve_write_query_restart_read_shutdown() {
        let block_size = 8192;
        let ramdisk = ramdisk(block_size);
        let mut minfs = new_fs(&ramdisk, Minfs::default());

        minfs.format().await.expect("failed to format minfs");
        let serving = minfs.serve().await.expect("failed to serve minfs the first time");

        // snapshot of FilesystemInfo
        let fs_info1 =
            serving.query().await.expect("failed to query filesystem info after first serving");

        let filename = "test_file";
        let content = String::from("test content").into_bytes();

        {
            let test_file = fuchsia_fs::directory::open_file(
                serving.root(),
                filename,
                fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE,
            )
            .await
            .expect("failed to create test file");
            let _: u64 = test_file
                .write(&content)
                .await
                .expect("failed to write to test file")
                .map_err(Status::from_raw)
                .expect("write error");
        }

        // check against the snapshot FilesystemInfo
        let fs_info2 = serving.query().await.expect("failed to query filesystem info after write");
        assert_eq!(
            fs_info2.used_bytes - fs_info1.used_bytes,
            fs_info2.block_size as u64 // assuming content < 8K
        );

        serving.shutdown().await.expect("failed to shutdown minfs the first time");
        let serving = minfs.serve().await.expect("failed to serve minfs the second time");

        {
            let test_file = fuchsia_fs::directory::open_file(
                serving.root(),
                filename,
                fio::OpenFlags::RIGHT_READABLE,
            )
            .await
            .expect("failed to open test file");
            let read_content =
                fuchsia_fs::file::read(&test_file).await.expect("failed to read from test file");
            assert_eq!(content, read_content);
        }

        // once more check against the snapshot FilesystemInfo
        let fs_info3 = serving.query().await.expect("failed to query filesystem info after read");
        assert_eq!(
            fs_info3.used_bytes - fs_info1.used_bytes,
            fs_info3.block_size as u64 // assuming content < 8K
        );

        let _ = serving.shutdown().await.expect("failed to shutdown minfs the second time");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn minfs_bind_to_path() {
        let block_size = 8192;
        let test_content = b"test content";
        let ramdisk = ramdisk(block_size);
        let mut minfs = new_fs(&ramdisk, Minfs::default());

        minfs.format().await.expect("failed to format minfs");
        let mut serving = minfs.serve().await.expect("failed to serve minfs");
        serving.bind_to_path("/test-minfs-path").expect("bind_to_path failed");
        let test_path = "/test-minfs-path/test_file";

        {
            let mut file = std::fs::File::create(test_path).expect("failed to create test file");
            file.write_all(test_content).expect("write bytes");
        }

        {
            let mut file = std::fs::File::open(test_path).expect("failed to open test file");
            let mut buf = Vec::new();
            file.read_to_end(&mut buf).expect("failed to read test file");
            assert_eq!(buf, test_content);
        }

        serving.shutdown().await.expect("failed to shutdown minfs");

        std::fs::File::open(test_path).expect_err("test file was not unbound");
    }

    #[fuchsia::test]
    async fn factoryfs_custom_config() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let config = Factoryfs { verbose: true };
        let mut factoryfs = new_fs(&ramdisk, config);

        factoryfs.format().await.expect("failed to format factoryfs");
        factoryfs.fsck().await.expect("failed to fsck factoryfs");
        let _ = factoryfs.serve().await.expect("failed to serve factoryfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn factoryfs_format_fsck_success() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let mut factoryfs = new_fs(&ramdisk, Factoryfs::default());

        factoryfs.format().await.expect("failed to format factoryfs");
        factoryfs.fsck().await.expect("failed to fsck factoryfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn factoryfs_format_serve_shutdown() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let mut factoryfs = new_fs(&ramdisk, Factoryfs::default());

        factoryfs.format().await.expect("failed to format factoryfs");
        let serving = factoryfs.serve().await.expect("failed to serve factoryfs");
        serving.shutdown().await.expect("failed to shutdown factoryfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn factoryfs_bind_to_path() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let mut factoryfs = new_fs(&ramdisk, Factoryfs::default());

        factoryfs.format().await.expect("failed to format factoryfs");
        {
            let mut serving = factoryfs.serve().await.expect("failed to serve factoryfs");
            serving.bind_to_path("/test-factoryfs-path").expect("bind_to_path failed");

            // factoryfs is read-only, so just check that we can open the root directory.
            {
                let file = std::fs::File::open("/test-factoryfs-path")
                    .expect("failed to open root directory");
                file.metadata().expect("failed to get metadata");
            }
        }

        std::fs::File::open("/test-factoryfs-path").expect_err("factoryfs path is still bound");
    }

    // TODO(fxbug.dev/93066): Re-enable this test; it depends on Fxfs failing repeated calls to
    // Start.
    #[ignore]
    #[fuchsia::test]
    async fn fxfs_shutdown_component_when_dropped() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let mut fxfs = new_fs(&ramdisk, Fxfs::default());

        fxfs.format().await.expect("failed to format fxfs");
        {
            let _fs = fxfs.serve_multi_volume().await.expect("failed to serve fxfs");

            // Serve should fail for the second time.
            assert!(
                fxfs.serve_multi_volume().await.is_err(),
                "serving succeeded when already mounted"
            );
        }

        // Fxfs should get shut down when dropped, but it's asynchronous, so we need to loop here.
        let mut attempts = 0;
        loop {
            if let Ok(_) = fxfs.serve_multi_volume().await {
                break;
            }
            attempts += 1;
            assert!(attempts < 10);
            fasync::Timer::new(Duration::from_secs(1)).await;
        }
    }

    #[fuchsia::test]
    async fn fxfs_open_volume() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let mut fxfs = new_fs(&ramdisk, Fxfs::default());

        fxfs.format().await.expect("failed to format fxfs");

        let mut fs = fxfs.serve_multi_volume().await.expect("failed to serve fxfs");

        assert!(
            fs.open_volume("foo", None).await.is_err(),
            "Opening nonexistent volume should fail"
        );

        let vol = fs.create_volume("foo", None).await.expect("Create volume failed");
        vol.query().await.expect("Query volume failed");
        fs.close_volume("foo");
        // TODO(fxbug.dev/106555) Closing the volume is not synchronous. Immediately reopening the
        // volume will race with the asynchronous close and sometimes fail because the volume is
        // still mounted.
        // fs.open_volume("foo", None).await.expect("Open volume failed");
    }
}
