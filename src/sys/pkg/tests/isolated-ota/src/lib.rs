// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use {
    anyhow::{Context, Error},
    assert_matches::assert_matches,
    async_trait::async_trait,
    blobfs_ramdisk::BlobfsRamdisk,
    fidl::endpoints::{ClientEnd, Proxy, RequestStream, ServerEnd},
    fidl_fuchsia_boot::{ArgumentsRequest, ArgumentsRequestStream},
    fidl_fuchsia_io as fio,
    fidl_fuchsia_paver::{Asset, Configuration},
    fidl_fuchsia_pkg_ext::{MirrorConfigBuilder, RepositoryConfigBuilder, RepositoryConfigs},
    fuchsia_async as fasync,
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, Ref, Route},
    fuchsia_pkg_testing::{make_current_epoch_json, Package, PackageBuilder},
    fuchsia_zircon as zx,
    futures::{future::FutureExt, prelude::*},
    http::uri::Uri,
    isolated_ota::{
        download_and_apply_update_with_pre_configured_components, OmahaConfig, UpdateError,
    },
    isolated_ota_env::{
        OmahaState, TestEnvBuilder, TestExecutor, TestParams, GLOBAL_SSL_CERTS_PATH,
    },
    isolated_swd::{cache::Cache, resolver::Resolver},
    mock_omaha_server::OmahaResponse,
    mock_paver::{hooks as mphooks, PaverEvent},
    parking_lot::Mutex,
    std::{collections::BTreeSet, sync::Arc},
    vfs::directory::entry::DirectoryEntry,
};

struct TestResult {
    blobfs: Option<BlobfsRamdisk>,
    packages: Vec<Package>,
    pub paver_events: Vec<PaverEvent>,
    pub result: Result<(), UpdateError>,
}

impl TestResult {
    /// Assert that all blobs in all the packages that were part of the Update
    /// have been installed into the blobfs, and that the blobfs contains no extra blobs.
    pub fn check_packages(&self) {
        let written_blobs = self
            .blobfs
            .as_ref()
            .unwrap_or_else(|| panic!("Test had no blobfs"))
            .list_blobs()
            .expect("Listing blobfs blobs");
        let mut all_package_blobs = BTreeSet::new();
        for package in self.packages.iter() {
            all_package_blobs.append(&mut package.list_blobs().expect("Listing package blobs"));
        }

        assert_eq!(written_blobs, all_package_blobs);
    }
}

struct IsolatedOtaTestExecutor {}
impl IsolatedOtaTestExecutor {
    pub fn new() -> Box<Self> {
        Box::new(Self {})
    }

    async fn serve_boot_arguments(mut stream: ArgumentsRequestStream, channel: Option<String>) {
        while let Some(req) = stream.try_next().await.unwrap() {
            match req {
                ArgumentsRequest::GetString { key, responder } => {
                    if key == "tuf_repo_config" {
                        responder.send(channel.as_deref()).unwrap();
                    } else {
                        eprintln!("Unexpected arguments GetString: {}, closing channel.", key);
                    }
                }
                _ => eprintln!("Unexpected arguments request, closing channel."),
            }
        }
    }

    async fn run_boot_arguments(
        handles: fuchsia_component_test::LocalComponentHandles,
        channel: Option<String>,
    ) -> Result<(), Error> {
        let mut fs = fuchsia_component::server::ServiceFs::new();
        fs.dir("svc")
            .add_fidl_service(move |stream| Self::serve_boot_arguments(stream, channel.clone()));
        fs.serve_connection(handles.outgoing_dir)?;
        fs.for_each_concurrent(None, |req| async { req.await }).await;
        Ok(())
    }
}

#[async_trait(?Send)]
impl TestExecutor<TestResult> for IsolatedOtaTestExecutor {
    async fn run(&self, params: TestParams) -> TestResult {
        let realm_builder = RealmBuilder::new().await.unwrap();
        let pkg_component =
            realm_builder.add_child("pkg", "#meta/pkg.cm", ChildOptions::new()).await.unwrap();
        realm_builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .capability(Capability::protocol_by_name(
                        "fuchsia.metrics.MetricEventLoggerFactory",
                    ))
                    .capability(Capability::protocol_by_name("fuchsia.net.name.Lookup"))
                    .capability(Capability::protocol_by_name("fuchsia.posix.socket.Provider"))
                    .capability(Capability::protocol_by_name("fuchsia.tracing.provider.Registry"))
                    .from(Ref::parent())
                    .to(&pkg_component),
            )
            .await
            .unwrap();

        realm_builder
            .add_route(
                // TODO(fxbug.dev/104918): clean up when system-updater-isolated is v2.
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.pkg.PackageCache"))
                    .capability(Capability::protocol_by_name("fuchsia.pkg.PackageResolver"))
                    .capability(Capability::protocol_by_name("fuchsia.pkg.RetainedPackages"))
                    .capability(Capability::protocol_by_name("fuchsia.space.Manager"))
                    .from(&pkg_component)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();

        realm_builder.add_route(Route::new().from(&pkg_component).to(Ref::parent())).await.unwrap();

        let pkg_resolver_directories_out_dir = vfs::pseudo_directory! {
            "config" => vfs::pseudo_directory! {
                "data" => vfs::pseudo_directory!{
                        "repositories" => vfs::remote::remote_dir(fuchsia_fs::directory::open_in_namespace(params.repo_config_dir.path().to_str().unwrap(), fio::OpenFlags::RIGHT_READABLE).unwrap())
                },
                "ssl" => vfs::remote::remote_dir(
                    params.ssl_certs
                ),
            },
        };
        let pkg_resolver_directories_out_dir = Mutex::new(Some(pkg_resolver_directories_out_dir));
        let pkg_resolver_directories = realm_builder
            .add_local_child(
                "pkg_resolver_directories",
                move |handles| {
                    let pkg_resolver_directories_out_dir = pkg_resolver_directories_out_dir
                        .lock()
                        .take()
                        .expect("mock component should only be launched once");
                    let scope = vfs::execution_scope::ExecutionScope::new();
                    let () = pkg_resolver_directories_out_dir.open(
                        scope.clone(),
                        fio::OpenFlags::RIGHT_READABLE
                            | fio::OpenFlags::RIGHT_WRITABLE
                            | fio::OpenFlags::RIGHT_EXECUTABLE,
                        0,
                        vfs::path::Path::dot(),
                        handles.outgoing_dir.into_channel().into(),
                    );
                    async move {
                        scope.wait().await;
                        Ok(())
                    }
                    .boxed()
                },
                ChildOptions::new(),
            )
            .await
            .unwrap();

        // Directory routes
        realm_builder
            .add_route(
                Route::new()
                    .capability(
                        Capability::directory("config-data")
                            .path("/config/data")
                            .rights(fio::R_STAR_DIR),
                    )
                    .from(&pkg_resolver_directories)
                    .to(&pkg_component),
            )
            .await
            .unwrap();
        realm_builder
            .add_route(
                Route::new()
                    .capability(
                        Capability::directory("root-ssl-certificates")
                            .path(GLOBAL_SSL_CERTS_PATH)
                            .rights(fio::R_STAR_DIR),
                    )
                    .from(&pkg_resolver_directories)
                    .to(&pkg_component),
            )
            .await
            .unwrap();

        let (blobfs_ramdisk, blobfs_handle) = match params.blobfs {
            Some(blobfs_handle) => (None, blobfs_handle),
            None => {
                let blobfs_ramdisk = BlobfsRamdisk::start().expect("launching blobfs");
                let blobfs_handle =
                    blobfs_ramdisk.root_dir_handle().expect("getting blobfs root handle");
                (Some(blobfs_ramdisk), blobfs_handle)
            }
        };

        let blobfs_proxy = fio::DirectoryProxy::from_channel(
            fasync::Channel::from_channel(blobfs_handle.into_channel()).unwrap(),
        );

        let (blobfs_client_end_clone, remote) =
            fidl::endpoints::create_endpoints::<fio::DirectoryMarker>().unwrap();
        blobfs_proxy
            .clone(fio::OpenFlags::CLONE_SAME_RIGHTS, ServerEnd::from(remote.into_channel()))
            .unwrap();

        let blobfs_proxy_clone = blobfs_client_end_clone.into_proxy().unwrap();
        let blobfs_vfs = vfs::remote::remote_dir(blobfs_proxy_clone);
        let blobfs_reflector = realm_builder
            .add_local_child(
                "pkg_cache_blobfs",
                move |handles| {
                    let blobfs_vfs = blobfs_vfs.clone();
                    let out_dir = vfs::pseudo_directory! {
                        "blob" => blobfs_vfs,
                    };
                    let scope = vfs::execution_scope::ExecutionScope::new();
                    let () = out_dir.open(
                        scope.clone(),
                        fio::OpenFlags::RIGHT_READABLE
                            | fio::OpenFlags::RIGHT_WRITABLE
                            | fio::OpenFlags::RIGHT_EXECUTABLE,
                        0,
                        vfs::path::Path::dot(),
                        handles.outgoing_dir.into_channel().into(),
                    );
                    async move {
                        scope.wait().await;
                        Ok(())
                    }
                    .boxed()
                },
                ChildOptions::new(),
            )
            .await
            .unwrap();

        realm_builder
            .add_route(
                Route::new()
                    .capability(
                        Capability::directory("blob-exec")
                            .path("/blob")
                            .rights(fio::RW_STAR_DIR | fio::Operations::EXECUTE),
                    )
                    .from(&blobfs_reflector)
                    .to(&pkg_component),
            )
            .await
            .unwrap();

        let channel = params.channel.clone();
        let boot_args_mock = realm_builder
            .add_local_child(
                "boot_arguments",
                move |handles| Box::pin(Self::run_boot_arguments(handles, Some(channel.clone()))),
                ChildOptions::new(),
            )
            .await
            .unwrap();

        realm_builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.boot.Arguments"))
                    .from(&boot_args_mock)
                    .to(&pkg_component),
            )
            .await
            .unwrap();

        let realm_instance = realm_builder.build().await.unwrap();
        let pkg_cache_proxy = realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<fidl_fuchsia_pkg::PackageCacheMarker>()
            .expect("connect to pkg cache");
        let space_manager_proxy = realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<fidl_fuchsia_space::ManagerMarker>()
            .expect("connect to space manager");

        let (cache_clone, remote) =
            fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>().unwrap();
        realm_instance
            .root
            .get_exposed_dir()
            .clone(
                fidl_fuchsia_io::OpenFlags::CLONE_SAME_RIGHTS,
                ServerEnd::from(remote.into_channel()),
            )
            .unwrap();

        let cache = Cache::new_with_proxies(
            pkg_cache_proxy,
            space_manager_proxy,
            cache_clone.into_proxy().unwrap(),
        )
        .unwrap();

        let pkg_resolver_proxy = realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<fidl_fuchsia_pkg::PackageResolverMarker>()
            .expect("connect to package resolver");
        let (resolver_svc_dir, remote) =
            fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>().unwrap();
        realm_instance
            .root
            .get_exposed_dir()
            .clone(
                fidl_fuchsia_io::OpenFlags::CLONE_SAME_RIGHTS,
                ServerEnd::from(remote.into_channel()),
            )
            .unwrap();

        let resolver =
            Resolver::new_with_proxy(pkg_resolver_proxy, resolver_svc_dir.into_proxy().unwrap())
                .unwrap();

        let result = download_and_apply_update_with_pre_configured_components(
            blobfs_proxy,
            params.paver_connector,
            &params.channel,
            &params.board,
            &params.version,
            params.omaha_config,
            Arc::new(cache),
            Arc::new(resolver),
        )
        .await;

        TestResult {
            blobfs: blobfs_ramdisk,
            packages: params.packages,
            paver_events: params.paver.take_events(),
            result,
        }
    }
}

async fn build_test_package() -> Result<Package, Error> {
    PackageBuilder::new("test-package")
        .add_resource_at("data/test", "hello, world!".as_bytes())
        .build()
        .await
        .context("Building test package")
}

#[fasync::run_singlethreaded(test)]
pub async fn test_no_network() -> Result<(), Error> {
    // Test what happens when we can't reach the remote repo.
    let bad_mirror =
        MirrorConfigBuilder::new("http://does-not-exist.fuchsia.com".parse::<Uri>().unwrap())?
            .build();
    let invalid_repo = RepositoryConfigs::Version1(vec![RepositoryConfigBuilder::new(
        fuchsia_url::RepositoryUrl::parse_host("fuchsia.com".to_owned()).unwrap(),
    )
    .add_mirror(bad_mirror)
    .build()]);

    let env = TestEnvBuilder::new()
        .test_executor(IsolatedOtaTestExecutor::new())
        .repo_config(invalid_repo)
        .build()
        .await
        .context("Building TestEnv")?;

    let update_result = env.run().await;
    assert_eq!(
        update_result.paver_events,
        vec![
            PaverEvent::QueryCurrentConfiguration,
            PaverEvent::ReadAsset {
                configuration: Configuration::A,
                asset: Asset::VerifiedBootMetadata
            },
            PaverEvent::ReadAsset { configuration: Configuration::A, asset: Asset::Kernel },
            PaverEvent::QueryCurrentConfiguration,
            PaverEvent::QueryConfigurationStatus { configuration: Configuration::A },
            PaverEvent::SetConfigurationUnbootable { configuration: Configuration::B },
            PaverEvent::BootManagerFlush,
        ]
    );
    update_result.check_packages();

    let err = update_result.result.unwrap_err();
    assert_matches!(err, UpdateError::InstallError(_));
    Ok(())
}

#[fasync::run_singlethreaded(test)]
pub async fn test_pave_fails() -> Result<(), Error> {
    // Test what happens if the paver fails while paving.
    let test_package = build_test_package().await?;
    let paver_hook = |p: &PaverEvent| {
        if let PaverEvent::WriteAsset { payload, .. } = p {
            if payload.as_slice() == "FAIL".as_bytes() {
                return zx::Status::IO;
            }
        }
        zx::Status::OK
    };

    let env = TestEnvBuilder::new()
        .test_executor(IsolatedOtaTestExecutor::new())
        .paver(|p| p.insert_hook(mphooks::return_error(paver_hook)))
        .add_package(test_package)
        .add_image("zbi.signed", "FAIL".as_bytes())
        .add_image("epoch.json", make_current_epoch_json().as_bytes())
        .add_image("fuchsia.vbmeta", "FAIL".as_bytes())
        .build()
        .await
        .context("Building TestEnv")?;

    let result = env.run().await;
    assert_eq!(
        result.paver_events,
        vec![
            PaverEvent::QueryCurrentConfiguration,
            PaverEvent::ReadAsset {
                configuration: Configuration::A,
                asset: Asset::VerifiedBootMetadata
            },
            PaverEvent::ReadAsset { configuration: Configuration::A, asset: Asset::Kernel },
            PaverEvent::QueryCurrentConfiguration,
            PaverEvent::QueryConfigurationStatus { configuration: Configuration::A },
            PaverEvent::SetConfigurationUnbootable { configuration: Configuration::B },
            PaverEvent::BootManagerFlush,
            PaverEvent::WriteAsset {
                asset: Asset::Kernel,
                configuration: Configuration::B,
                payload: "FAIL".as_bytes().to_vec(),
            },
        ]
    );
    println!("Paver events: {:?}", result.paver_events);
    assert_matches!(result.result.unwrap_err(), UpdateError::InstallError(_));

    Ok(())
}

#[fasync::run_singlethreaded(test)]
pub async fn test_updater_succeeds() -> Result<(), Error> {
    let mut builder = TestEnvBuilder::new()
        .test_executor(IsolatedOtaTestExecutor::new())
        .add_image("zbi.signed", "This is a zbi".as_bytes())
        .add_image("fuchsia.vbmeta", "This is a vbmeta".as_bytes())
        .add_image("recovery", "This is recovery".as_bytes())
        .add_image("recovery.vbmeta", "This is another vbmeta".as_bytes())
        .add_image("bootloader", "This is a bootloader upgrade".as_bytes())
        .add_image("epoch.json", make_current_epoch_json().as_bytes())
        .add_image("firmware_test", "This is the test firmware".as_bytes());
    for i in 0i64..3 {
        let name = format!("test-package{}", i);
        let package = PackageBuilder::new(name)
            .add_resource_at(
                format!("data/my-package-data-{}", i),
                format!("This is some test data for test package {}", i).as_bytes(),
            )
            .add_resource_at("bin/binary", "#!/boot/bin/sh\necho Hello".as_bytes())
            .build()
            .await
            .context("Building test package")?;
        builder = builder.add_package(package);
    }

    let env = builder.build().await.context("Building TestEnv")?;
    let result = env.run().await;

    result.check_packages();
    assert!(result.result.is_ok());
    assert_eq!(
        result.paver_events,
        vec![
            PaverEvent::QueryCurrentConfiguration,
            PaverEvent::ReadAsset {
                configuration: Configuration::A,
                asset: Asset::VerifiedBootMetadata
            },
            PaverEvent::ReadAsset { configuration: Configuration::A, asset: Asset::Kernel },
            PaverEvent::QueryCurrentConfiguration,
            PaverEvent::QueryConfigurationStatus { configuration: Configuration::A },
            PaverEvent::SetConfigurationUnbootable { configuration: Configuration::B },
            PaverEvent::BootManagerFlush,
            PaverEvent::WriteFirmware {
                configuration: Configuration::B,
                firmware_type: "".to_owned(),
                payload: "This is a bootloader upgrade".as_bytes().to_vec(),
            },
            PaverEvent::WriteFirmware {
                configuration: Configuration::B,
                firmware_type: "test".to_owned(),
                payload: "This is the test firmware".as_bytes().to_vec(),
            },
            PaverEvent::WriteAsset {
                configuration: Configuration::B,
                asset: Asset::Kernel,
                payload: "This is a zbi".as_bytes().to_vec(),
            },
            PaverEvent::WriteAsset {
                configuration: Configuration::B,
                asset: Asset::VerifiedBootMetadata,
                payload: "This is a vbmeta".as_bytes().to_vec(),
            },
            PaverEvent::DataSinkFlush,
            // Note that recovery isn't written, as isolated-ota skips them.
            PaverEvent::SetConfigurationActive { configuration: Configuration::B },
            PaverEvent::BootManagerFlush,
            // This is the isolated-ota library checking to see if the paver configured ABR properly.
            PaverEvent::QueryActiveConfiguration,
        ]
    );
    Ok(())
}

fn launch_cloned_blobfs(
    end: ServerEnd<fio::NodeMarker>,
    flags: fio::OpenFlags,
    parent_flags: fio::OpenFlags,
) {
    let flags =
        if flags.contains(fio::OpenFlags::CLONE_SAME_RIGHTS) { parent_flags } else { flags };
    let chan = fidl::AsyncChannel::from_channel(end.into_channel()).expect("cloning blobfs dir");
    let stream = fio::DirectoryRequestStream::from_channel(chan);
    fasync::Task::spawn(async move {
        serve_failing_blobfs(stream, flags)
            .await
            .unwrap_or_else(|e| panic!("Failed to serve cloned blobfs handle: {:?}", e));
    })
    .detach();
}

async fn serve_failing_blobfs(
    mut stream: fio::DirectoryRequestStream,
    open_flags: fio::OpenFlags,
) -> Result<(), Error> {
    if open_flags.contains(fio::OpenFlags::DESCRIBE) {
        stream
            .control_handle()
            .send_on_open_(
                zx::Status::OK.into_raw(),
                Some(&mut fio::NodeInfoDeprecated::Directory(fio::DirectoryObject)),
            )
            .context("sending on open")?;
    }
    while let Some(req) = stream.try_next().await? {
        match req {
            fio::DirectoryRequest::Clone { flags, object, control_handle: _ } => {
                launch_cloned_blobfs(object, flags, open_flags)
            }
            fio::DirectoryRequest::Reopen { rights_request, object_request, control_handle: _ } => {
                let _ = object_request;
                todo!("https://fxbug.dev/77623: rights_request={:?}", rights_request);
            }
            fio::DirectoryRequest::Close { responder } => {
                responder.send(&mut Err(zx::Status::IO.into_raw())).context("failing close")?
            }
            fio::DirectoryRequest::DescribeDeprecated { responder } => responder
                .send(&mut fio::NodeInfoDeprecated::Directory(fio::DirectoryObject))
                .context("describing")?,
            fio::DirectoryRequest::GetConnectionInfo { responder } => {
                let _ = responder;
                todo!("https://fxbug.dev/77623");
            }
            fio::DirectoryRequest::Sync { responder } => {
                responder.send(&mut Err(zx::Status::IO.into_raw())).context("failing sync")?
            }
            fio::DirectoryRequest::AdvisoryLock { request: _, responder } => {
                responder.send(&mut Err(zx::sys::ZX_ERR_NOT_SUPPORTED))?
            }
            fio::DirectoryRequest::GetAttr { responder } => responder
                .send(
                    zx::Status::IO.into_raw(),
                    &mut fio::NodeAttributes {
                        mode: 0,
                        id: 0,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 0,
                        creation_time: 0,
                        modification_time: 0,
                    },
                )
                .context("failing getattr")?,
            fio::DirectoryRequest::SetAttr { flags: _, attributes: _, responder } => {
                responder.send(zx::Status::IO.into_raw()).context("failing setattr")?
            }
            fio::DirectoryRequest::GetAttributes { query, responder } => {
                let _ = responder;
                todo!("https://fxbug.dev/77623: query={:?}", query);
            }
            fio::DirectoryRequest::UpdateAttributes { payload, responder } => {
                let _ = responder;
                todo!("https://fxbug.dev/77623: payload={:?}", payload);
            }
            fio::DirectoryRequest::GetFlags { responder } => responder
                .send(zx::Status::IO.into_raw(), fio::OpenFlags::empty())
                .context("failing getflags")?,
            fio::DirectoryRequest::SetFlags { flags: _, responder } => {
                responder.send(zx::Status::IO.into_raw()).context("failing setflags")?
            }
            fio::DirectoryRequest::Open { flags, mode: _, path, object, control_handle: _ } => {
                if &path == "." {
                    launch_cloned_blobfs(object, flags, open_flags);
                } else {
                    object.close_with_epitaph(zx::Status::IO).context("failing open")?;
                }
            }
            fio::DirectoryRequest::Open2 { path, protocols, object_request, control_handle: _ } => {
                let _ = object_request;
                todo!("https://fxbug.dev/77623: path={} protocols={:?}", path, protocols);
            }
            fio::DirectoryRequest::AddInotifyFilter {
                path,
                filter,
                watch_descriptor,
                socket: _,
                responder: _,
            } => {
                todo!(
                    "https://fxbug.dev/77623: path={} filter={:?} watch_descriptor={}",
                    path,
                    filter,
                    watch_descriptor
                );
            }
            fio::DirectoryRequest::Unlink { name: _, options: _, responder } => {
                responder.send(&mut Err(zx::Status::IO.into_raw())).context("failing unlink")?
            }
            fio::DirectoryRequest::ReadDirents { max_bytes: _, responder } => {
                responder.send(zx::Status::IO.into_raw(), &[]).context("failing readdirents")?
            }
            fio::DirectoryRequest::Enumerate { options, iterator, control_handle: _ } => {
                let _ = iterator;
                todo!("https://fxbug.dev/77623: options={:?}", options);
            }
            fio::DirectoryRequest::Rewind { responder } => {
                responder.send(zx::Status::IO.into_raw()).context("failing rewind")?
            }
            fio::DirectoryRequest::GetToken { responder } => {
                responder.send(zx::Status::IO.into_raw(), None).context("failing gettoken")?
            }
            fio::DirectoryRequest::Rename { src: _, dst_parent_token: _, dst: _, responder } => {
                responder.send(&mut Err(zx::Status::IO.into_raw())).context("failing rename")?
            }
            fio::DirectoryRequest::Link { src: _, dst_parent_token: _, dst: _, responder } => {
                responder.send(zx::Status::IO.into_raw()).context("failing link")?
            }
            fio::DirectoryRequest::Watch { mask: _, options: _, watcher: _, responder } => {
                responder.send(zx::Status::IO.into_raw()).context("failing watch")?
            }
            fio::DirectoryRequest::Query { responder } => {
                responder.send(fio::DIRECTORY_PROTOCOL_NAME.as_bytes())?;
            }
            fio::DirectoryRequest::QueryFilesystem { responder } => responder
                .send(zx::Status::IO.into_raw(), None)
                .context("failing queryfilesystem")?,
        };
    }

    Ok(())
}

#[fasync::run_singlethreaded(test)]
pub async fn test_blobfs_broken() -> Result<(), Error> {
    let (client, server) = zx::Channel::create().context("creating blobfs channel")?;
    let package = build_test_package().await?;
    let paver_hook = |_: &PaverEvent| zx::Status::IO;
    let env = TestEnvBuilder::new()
        .test_executor(IsolatedOtaTestExecutor::new())
        .add_package(package)
        .add_image("zbi.signed", "ZBI".as_bytes())
        .blobfs(ClientEnd::from(client))
        .paver(|p| p.insert_hook(mphooks::return_error(paver_hook)))
        .build()
        .await
        .context("Building TestEnv")?;

    let stream =
        fio::DirectoryRequestStream::from_channel(fidl::AsyncChannel::from_channel(server)?);

    fasync::Task::spawn(async move {
        serve_failing_blobfs(stream, fio::OpenFlags::empty())
            .await
            .unwrap_or_else(|e| panic!("Failed to serve blobfs: {:?}", e));
    })
    .detach();

    let result = env.run().await;

    assert_matches!(result.result, Err(UpdateError::InstallError(_)));

    Ok(())
}

#[fasync::run_singlethreaded(test)]
pub async fn test_omaha_broken() -> Result<(), Error> {
    let bad_omaha_config = OmahaConfig {
        app_id: "broken-omaha-test".to_owned(),
        server_url: "http://does-not-exist.fuchsia.com".to_owned(),
    };
    let package = build_test_package().await?;
    let env = TestEnvBuilder::new()
        .test_executor(IsolatedOtaTestExecutor::new())
        .add_package(package)
        .add_image("zbi.signed", "ZBI".as_bytes())
        .omaha_state(OmahaState::Manual(bad_omaha_config))
        .build()
        .await
        .context("Building TestEnv")?;

    let result = env.run().await;
    assert_matches!(result.result, Err(UpdateError::InstallError(_)));

    Ok(())
}

#[fasync::run_singlethreaded(test)]
pub async fn test_omaha_works() -> Result<(), Error> {
    let mut builder = TestEnvBuilder::new()
        .test_executor(IsolatedOtaTestExecutor::new())
        .add_image("zbi.signed", "This is a zbi".as_bytes())
        .add_image("fuchsia.vbmeta", "This is a vbmeta".as_bytes())
        .add_image("recovery", "This is recovery".as_bytes())
        .add_image("recovery.vbmeta", "This is another vbmeta".as_bytes())
        .add_image("bootloader", "This is a bootloader upgrade".as_bytes())
        .add_image("epoch.json", make_current_epoch_json().as_bytes())
        .add_image("firmware_test", "This is the test firmware".as_bytes());
    for i in 0i64..3 {
        let name = format!("test-package{}", i);
        let package = PackageBuilder::new(name)
            .add_resource_at(
                format!("data/my-package-data-{}", i),
                format!("This is some test data for test package {}", i).as_bytes(),
            )
            .add_resource_at("bin/binary", "#!/boot/bin/sh\necho Hello".as_bytes())
            .build()
            .await
            .context("Building test package")?;
        builder = builder.add_package(package);
    }

    let env = builder
        .omaha_state(OmahaState::Auto(OmahaResponse::Update))
        .build()
        .await
        .context("Building TestEnv")?;

    let result = env.run().await;
    result.check_packages();
    assert!(result.result.is_ok());
    assert_eq!(
        result.paver_events,
        vec![
            PaverEvent::QueryCurrentConfiguration,
            PaverEvent::ReadAsset {
                configuration: Configuration::A,
                asset: Asset::VerifiedBootMetadata
            },
            PaverEvent::ReadAsset { configuration: Configuration::A, asset: Asset::Kernel },
            PaverEvent::QueryCurrentConfiguration,
            PaverEvent::QueryConfigurationStatus { configuration: Configuration::A },
            PaverEvent::SetConfigurationUnbootable { configuration: Configuration::B },
            PaverEvent::BootManagerFlush,
            PaverEvent::WriteFirmware {
                configuration: Configuration::B,
                firmware_type: "".to_owned(),
                payload: "This is a bootloader upgrade".as_bytes().to_vec(),
            },
            PaverEvent::WriteFirmware {
                configuration: Configuration::B,
                firmware_type: "test".to_owned(),
                payload: "This is the test firmware".as_bytes().to_vec(),
            },
            PaverEvent::WriteAsset {
                configuration: Configuration::B,
                asset: Asset::Kernel,
                payload: "This is a zbi".as_bytes().to_vec(),
            },
            PaverEvent::WriteAsset {
                configuration: Configuration::B,
                asset: Asset::VerifiedBootMetadata,
                payload: "This is a vbmeta".as_bytes().to_vec(),
            },
            PaverEvent::DataSinkFlush,
            // Note that recovery isn't written, as isolated-ota skips them.
            PaverEvent::SetConfigurationActive { configuration: Configuration::B },
            PaverEvent::BootManagerFlush,
            // This is the isolated-ota library checking to see if the paver configured ABR properly.
            PaverEvent::QueryActiveConfiguration,
        ]
    );

    Ok(())
}
