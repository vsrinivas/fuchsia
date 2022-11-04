// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use async_trait::async_trait;
use blobfs_ramdisk::BlobfsRamdisk;
use fidl::endpoints::{ClientEnd, DiscoverableProtocolMarker, Proxy, ServerEnd};
use fidl_fuchsia_buildinfo as fbi;
use fidl_fuchsia_fshost as ffsh;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_paver::{self as fpaver, Asset, Configuration};
use fidl_fuchsia_recovery_ui as frui;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_component_test::{
    Capability, ChildOptions, ChildRef, LocalComponentHandles, RealmBuilder, Ref, Route,
};
use fuchsia_pkg_testing::{make_current_epoch_json, Package, PackageBuilder};
use futures::{channel::mpsc, FutureExt, StreamExt, TryStreamExt};
use isolated_ota::OmahaConfig;
use isolated_ota_env::{OmahaState, TestEnvBuilder, TestExecutor, TestParams};
use mock_omaha_server::OmahaResponse;
use mock_paver::{MockPaverService, PaverEvent};
use ota_lib::ota::{RecoveryUpdateConfig, UpdateType};
use std::{collections::BTreeSet, sync::Arc};
use vfs::{directory::entry::DirectoryEntry, file::vmo::asynchronous::read_only_const};

type ProgressRendererSender = mpsc::Sender<frui::ProgressRendererRender2Request>;

/// Represents the result of running a [`isolated_ota_env::TestEnv`]. This type is vended by
/// [`OtaComponentTestExecutor`] which is invoked by `TestEnv`.
///
/// `packages` is expected to be populated from the `TestParams` given to
/// `OtaComponentTestExecutor`. It is expected that all blobs of all packages contained within are
/// also stored within `blobfs` after a successful update.
///
/// `progress_renderer_requests` is expected to contain FIDL messages sent by the OTA component
/// during the execution of the test.
///
/// `paver` is expected to be populated from `TestParams`.
struct TestResult {
    blobfs: BlobfsRamdisk,
    packages: Vec<Package>,
    pub progress_renderer_requests: Vec<frui::ProgressRendererRender2Request>,
    pub paver: Arc<MockPaverService>,
}

impl TestResult {
    /// Asserts that all blobs in all the packages that were part of the Update
    /// have been installed into the blobfs, and that the blobfs contains no extra blobs.
    pub fn check_packages(&self) {
        let written_blobs = self.blobfs.list_blobs().expect("Listing blobfs blobs");
        let mut all_package_blobs = BTreeSet::new();
        for package in self.packages.iter() {
            all_package_blobs.append(&mut package.list_blobs().expect("Listing package blobs"));
        }

        assert_eq!(written_blobs, all_package_blobs);
    }
}

struct OtaComponentTestExecutor {}

impl OtaComponentTestExecutor {
    fn new() -> Box<Self> {
        Box::new(Self {})
    }
}

/// The [`TestExecutor`] for recovery OTA component integration tests is responsible for creating
/// the sandbox in which the OTA component runs. [`isolated_ota_env::TestEnv`] receives this object
/// and spins up some external dependencies for the test environment including:
/// - Mock TUF repository - provided by the [`fuchsia-pkg-testing`] library
/// - Mock Omaha server - provided by the [`mock-omaha-server`] library
/// - Mock fuchsia.paver.Paver FIDL service - provided by the [`mock-paver`] library
#[async_trait(?Send)]
impl TestExecutor<Result<TestResult, Error>> for OtaComponentTestExecutor {
    /// Runs [`OtaComponentTestExecutor`] which sets up the RealmBuilder environment and triggers
    /// an Omaha update check.
    ///
    /// Recovery OTA has numerous dependencies that must be mocked and/or routed including:
    /// - FIDL protocols from this test's parent, `test_manager`.
    /// - A directory where blobs are written - handled by [`blobfs_ramdisk::BlobfsRamdisk`].
    /// - Resources provided by [`isolated_ota_env::TestEnv`] which includes the following:
    ///   - SSL certificates directory
    ///   - Omaha configs - server URL and test environment app ID
    ///   - TUF repository parameters - packages, configs, and update merkle
    ///   - `fuchsia.paver.Paver` service directory
    ///
    /// The future returned by this function will not resolve until the OTA component sends a
    /// Render2 FIDL request with non-Active status via `fuchsia.recovery.ui.ProgressRenderer`.
    async fn run(&self, params: TestParams) -> Result<TestResult, Error> {
        let builder = RealmBuilder::new().await.unwrap();

        let system_recovery_ota_child = create_system_recovery_ota_child(&builder).await;
        route_capabilities_from_parent(&builder, &system_recovery_ota_child).await;
        route_ssl_certs(&builder, &system_recovery_ota_child, params.ssl_certs).await;

        if let Some(omaha_config) = params.omaha_config {
            route_config_data(
                &builder,
                &system_recovery_ota_child,
                omaha_config,
                params.channel,
                params.version,
                params.repo_config_dir,
            )
            .await;
        }

        let blobfs = BlobfsRamdisk::start().expect("launching blobfs");
        let progress_rx = route_and_serve_local_mocks(
            &builder,
            &system_recovery_ota_child,
            params.paver_connector,
            params.board,
            blobfs.root_dir_handle().expect("failed getting blobfs root handle"),
        )
        .await;

        let progress_renderer_requests = launch_ota_and_await_result(builder, progress_rx).await?;

        Ok(TestResult {
            blobfs,
            packages: params.packages,
            paver: params.paver,
            progress_renderer_requests,
        })
    }
}

async fn create_system_recovery_ota_child(builder: &RealmBuilder) -> ChildRef {
    let system_recovery_ota_child = builder
        .add_child(
            "system_recovery_ota",
            "#meta/system_recovery_ota.cm",
            ChildOptions::new().eager(),
        )
        .await
        .expect("failed to add system_recovery_ota child");
    system_recovery_ota_child
}

async fn route_capabilities_from_parent(
    builder: &RealmBuilder,
    system_recovery_ota_child: &ChildRef,
) {
    builder
        .add_route(
            Route::new()
                .capability(Capability::storage("tmp"))
                .from(Ref::parent())
                .to(system_recovery_ota_child),
        )
        .await
        .expect("failed to add system_recovery_ota route");

    for protocol in [
        "fuchsia.boot.WriteOnlyLog",
        "fuchsia.diagnostics.ArchiveAccessor",
        "fuchsia.logger.LogSink",
        "fuchsia.net.name.Lookup",
        "fuchsia.posix.socket.Provider",
        "fuchsia.process.Launcher",
        "fuchsia.sys.Environment",
        "fuchsia.sys.Loader",
    ] {
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(protocol))
                    .from(Ref::parent())
                    .to(system_recovery_ota_child),
            )
            .await
            .expect("failed to add routes from parent");
    }
}

async fn route_ssl_certs(
    builder: &RealmBuilder,
    system_recovery_ota_child: &ChildRef,
    ssl_certs: fio::DirectoryProxy,
) {
    let out_dir = vfs::pseudo_directory! {
        "ssl" => vfs::remote::remote_dir(ssl_certs),
    };
    let test_ssl_certs_child = builder
        .add_local_child(
            "test_ssl_certs",
            move |handles| {
                let scope = vfs::execution_scope::ExecutionScope::new();
                out_dir.clone().open(
                    scope.clone(),
                    fio::OpenFlags::RIGHT_READABLE,
                    0,
                    vfs::path::Path::dot(),
                    handles.outgoing_dir.into_channel().into(),
                );
                async move { Ok(scope.wait().await) }.boxed()
            },
            ChildOptions::new().eager(),
        )
        .await
        .expect("failed to add test_ssl_certs child");
    builder
        .add_route(
            Route::new()
                .capability(
                    Capability::directory("root-ssl-certificates")
                        .path("/ssl")
                        .rights(fio::R_STAR_DIR),
                )
                .from(&test_ssl_certs_child)
                .to(system_recovery_ota_child),
        )
        .await
        .expect("failed to route test_ssl_certs dir");
}

/// Routes dynamically generated config data to the OTA component.
///
/// The OTA component requires two files to appear in its namespace at these exact locations:
/// - /config/data/recovery-config.json - used to set the behavior of the OTA component including
/// how and from where the over-the-air update is downloaded. In this case, the OTA component is
/// fetching an update from an Omaha server hosted by [`isolated_ota_env::TestEnv`] which provides
/// the server and App ID within `omaha_config`, `channel`, and `version` string.
/// - /config/data/ota-configs/ota_config.json - used by Omaha and TUF libraries to route package
/// requests to the appropriate TUF repository. In this case, the appropriate TUF repo is a mock
/// TUF repository run by [`isolated_ota_env::TestEnv`] which provides the configurations inside of
/// `repo_config_dir`. The format of this file is determined by `RepositoryConfigs` within
/// //src/sys/lib/fidl-fuchsia-pkg-ext/src/repo.rs.
async fn route_config_data(
    builder: &RealmBuilder,
    system_recovery_ota_child: &ChildRef,
    omaha_config: OmahaConfig,
    channel: String,
    version: String,
    repo_config_dir: tempfile::TempDir,
) {
    let recovery_config = serde_json::to_vec(&RecoveryUpdateConfig {
        default_channel: channel,
        update_type: UpdateType::Omaha(omaha_config.app_id, Some(omaha_config.server_url)),
        override_version: Some(version),
    })
    .unwrap();

    let mut repo_config_dir_path = repo_config_dir.path().to_owned();
    repo_config_dir_path.push("repo_config.json");
    let ota_config = std::fs::read(repo_config_dir_path).expect("failed to read repo_config.json");

    let out_dir = vfs::pseudo_directory! {
        "config" => vfs::pseudo_directory! {
            "recovery-config.json" => read_only_const(&recovery_config),
            "ota-configs" => vfs::pseudo_directory! {
                "ota_config.json" => read_only_const(&ota_config)
            },
        },
    };

    let fake_config_data_child = builder
        .add_local_child(
            "fake_config_data",
            move |handles| {
                let scope = vfs::execution_scope::ExecutionScope::new();
                out_dir.clone().open(
                    scope.clone(),
                    fio::OpenFlags::RIGHT_READABLE,
                    0,
                    vfs::path::Path::dot(),
                    handles.outgoing_dir.into_channel().into(),
                );
                async move { Ok(scope.wait().await) }.boxed()
            },
            ChildOptions::new().eager(),
        )
        .await
        .expect("failed to add fake_config_data child");

    builder
        .add_route(
            Route::new()
                .capability(
                    Capability::directory("config-data").path("/config").rights(fio::R_STAR_DIR),
                )
                .from(&fake_config_data_child)
                .to(system_recovery_ota_child),
        )
        .await
        .expect("failed to route fake_config_data dir");
}

async fn route_and_serve_local_mocks(
    builder: &RealmBuilder,
    system_recovery_ota_child: &ChildRef,
    paver_connector: ClientEnd<fio::DirectoryMarker>,
    board: String,
    blobfs_handle: ClientEnd<fio::DirectoryMarker>,
) -> mpsc::Receiver<frui::ProgressRendererRender2Request> {
    let (progress_tx, progress_rx) = mpsc::channel(100);

    let progress_renderer_server = builder
        .add_local_child(
            "progress_renderer",
            move |handles: LocalComponentHandles| {
                progress_renderer_server_mock(handles, progress_tx.clone()).boxed()
            },
            ChildOptions::new(),
        )
        .await
        .expect("failed to add progress_renderer child");
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.recovery.ui.ProgressRenderer"))
                .from(&progress_renderer_server)
                .to(system_recovery_ota_child),
        )
        .await
        .expect("failed to route progress_renderer protocol");

    let build_info_server = builder
        .add_local_child(
            "build_info",
            move |handles: LocalComponentHandles| {
                build_info_server_mock(handles, board.clone()).boxed()
            },
            ChildOptions::new(),
        )
        .await
        .expect("failed to add build_info child");
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.buildinfo.Provider"))
                .from(&build_info_server)
                .to(system_recovery_ota_child),
        )
        .await
        .expect("failed to route build_info protocol");

    let paver_dir_proxy =
        paver_connector.into_proxy().expect("failed to convert paver dir client end to proxy");
    let paver_child = builder
        .add_local_child(
            "paver",
            move |handles: LocalComponentHandles| {
                paver_server_mock(
                    handles,
                    fuchsia_fs::directory::clone_no_describe(&paver_dir_proxy, None).unwrap(),
                )
                .boxed()
            },
            ChildOptions::new().eager(),
        )
        .await
        .expect("failed to add paver child");
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.paver.Paver"))
                .from(&paver_child)
                .to(system_recovery_ota_child),
        )
        .await
        .expect("failed to route paver protocol");

    let blobfs_proxy = fio::DirectoryProxy::from_channel(
        fasync::Channel::from_channel(blobfs_handle.into_channel()).unwrap(),
    );

    let fshost_admin_server = builder
        .add_local_child(
            "fshost_admin",
            move |handles: LocalComponentHandles| {
                fshost_admin_server_mock(
                    handles,
                    fuchsia_fs::directory::clone_no_describe(&blobfs_proxy, None).unwrap(),
                )
                .boxed()
            },
            ChildOptions::new(),
        )
        .await
        .expect("failed to add fshost_admin child");
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.fshost.Admin"))
                .from(&fshost_admin_server)
                .to(system_recovery_ota_child),
        )
        .await
        .expect("failed to route fshost_admin protocol");

    progress_rx
}

async fn launch_ota_and_await_result(
    builder: RealmBuilder,
    mut progress_rx: mpsc::Receiver<frui::ProgressRendererRender2Request>,
) -> Result<Vec<frui::ProgressRendererRender2Request>, Error> {
    let realm = builder.build().await?;

    let mut progress_renderer_requests = Vec::new();
    loop {
        let actual_event = progress_rx.next().await.unwrap();
        progress_renderer_requests.push(actual_event);

        match progress_renderer_requests.last() {
            Some(frui::ProgressRendererRender2Request { status: Some(s), .. })
                if *s != frui::Status::Active =>
            {
                break;
            }
            _ => continue,
        }
    }

    // Dropping realm will not wait for the destroy process to complete which may cause
    // data races. Destroy the realm to release shared resources and to prevent read/write conflicts
    // during test validation.
    realm.destroy().await.expect("failed to destroy OTA component");

    Ok(progress_renderer_requests)
}

async fn progress_renderer_server_mock(
    handles: LocalComponentHandles,
    sender: ProgressRendererSender,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();

    fs.dir("svc").add_fidl_service(move |mut stream: frui::ProgressRendererRequestStream| {
        let mut sender = sender.clone();
        fasync::Task::local(async move {
            while let Some(request) =
                stream.try_next().await.expect("failed to serve progress_renderer service")
            {
                if let frui::ProgressRendererRequest::Render2 { payload, responder } = request {
                    sender.start_send(payload).expect("Failed to send render2 payload");
                    responder.send().expect("Error replying to progress update");
                }
            }
        })
        .detach();
    });

    // Run the ServiceFs on the outgoing directory handle from the mock handles
    fs.serve_connection(handles.outgoing_dir).expect("failed to serve progress renderer server");
    fs.collect::<()>().await;

    Ok(())
}

async fn build_info_server_mock(
    handles: LocalComponentHandles,
    board: String,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();

    fs.dir("svc").add_fidl_service(move |mut stream: fbi::ProviderRequestStream| {
        let board = board.clone();
        fasync::Task::local(async move {
            while let Some(request) =
                stream.try_next().await.expect("failed to serve build_info service")
            {
                let fbi::ProviderRequest::GetBuildInfo { responder } = request;
                responder
                    .send(fbi::BuildInfo {
                        product_config: Some("test_recovery".to_string()),
                        board_config: Some(board.clone()),

                        // override_version will be used instead of this,
                        // but it still must be set for the component to work.
                        version: Some(String::new()),
                        ..fbi::BuildInfo::EMPTY
                    })
                    .expect("Error sending buildinfo response.");
            }
        })
        .detach();
    });

    // Run the ServiceFs on the outgoing directory handle from the mock handles
    fs.serve_connection(handles.outgoing_dir).expect("failed to serve build info server");
    fs.collect::<()>().await;
    Ok(())
}

/// Connects the local component to the mock paver.
///
/// Unlike other mocks, the `fuchsia.paver.Paver` is serviced by [`isolated_ota_env::TestEnv`], so
/// this function proxies to the given `paver_dir_proxy` which is expected to host a
/// file named "fuchsia.paver.Paver" which implements the `fuchsia.paver.Paver` FIDL protocol.
async fn paver_server_mock(
    handles: LocalComponentHandles,
    paver_dir_proxy: fio::DirectoryProxy,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();

    fs.dir("svc").add_service_connector(move |server_end: ServerEnd<fpaver::PaverMarker>| {
        fdio::service_connect_at(
            &paver_dir_proxy.as_channel().as_ref(),
            &format!("/{}", fpaver::PaverMarker::PROTOCOL_NAME),
            server_end.into_channel(),
        )
        .expect("failed to connect to paver service node");
    });

    fs.serve_connection(handles.outgoing_dir).expect("failed to serve paver fs connection");
    fs.collect::<()>().await;
    Ok(())
}

async fn fshost_admin_server_mock(
    handles: LocalComponentHandles,
    blobfs_proxy: fio::DirectoryProxy,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();

    fs.dir("svc").add_fidl_service(move |mut stream: ffsh::AdminRequestStream| {
        let blobfs_proxy = fuchsia_fs::directory::clone_no_describe(&blobfs_proxy, None)
            .expect("failed to clone blobfs proxy");
        fasync::Task::local(async move {
            while let Some(request) =
                stream.try_next().await.expect("failed to serve fshost_admin service")
            {
                if let ffsh::AdminRequest::WipeStorage { blobfs_root, responder } = request {
                    fuchsia_fs::directory::clone_onto_no_describe(&blobfs_proxy, None, blobfs_root)
                        .expect("failed to clone blobfs proxy");
                    responder.send(&mut Ok(())).expect("Error replying to wipe storage request");
                }
            }
        })
        .detach();
    });

    // Run the ServiceFs on the outgoing directory handle from the mock handles
    fs.serve_connection(handles.outgoing_dir).expect("failed to serve fshost admin server");
    fs.collect::<()>().await;

    Ok(())
}

async fn add_test_packages<T>(mut builder: TestEnvBuilder<T>) -> TestEnvBuilder<T> {
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
            .expect("failed to build package");
        builder = builder.add_package(package);
    }
    builder
}

#[fuchsia::test]
async fn test_ota_component_successfully_updates_with_empty_blobfs() -> Result<(), Error> {
    let mut builder = TestEnvBuilder::new()
        .test_executor(OtaComponentTestExecutor::new())
        .omaha_state(OmahaState::Auto(OmahaResponse::Update))
        .add_image("zbi.signed", "This is a zbi".as_bytes())
        .add_image("fuchsia.vbmeta", "This is a vbmeta".as_bytes())
        .add_image("recovery", "This is recovery".as_bytes())
        .add_image("recovery.vbmeta", "This is another vbmeta".as_bytes())
        .add_image("bootloader", "This is a bootloader upgrade".as_bytes())
        .add_image("epoch.json", make_current_epoch_json().as_bytes())
        .add_image("firmware_test", "This is the test firmware".as_bytes());

    builder = add_test_packages(builder).await;

    let env = builder.build().await.expect("failed to build TestEnv");

    let result = env.run().await.expect("failed to run TestEnv");
    result.check_packages();

    assert_eq!(
        result.progress_renderer_requests,
        vec![
            frui::ProgressRendererRender2Request {
                status: Some(frui::Status::Active),
                percent_complete: Some(0.0),
                ..frui::ProgressRendererRender2Request::EMPTY
            },
            frui::ProgressRendererRender2Request {
                status: Some(frui::Status::Complete),
                percent_complete: Some(100.0),
                ..frui::ProgressRendererRender2Request::EMPTY
            },
        ]
    );

    assert_eq!(
        result.paver.take_events(),
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

#[fuchsia::test]
async fn test_ota_component_reports_error_when_omaha_broken() -> Result<(), Error> {
    // When Omaha is broken, the OTA component is expected to report that as an error.
    // The origin of this error is from within the isolated_ota library.
    let bad_omaha_config = OmahaConfig {
        app_id: "broken-omaha-test".to_owned(),
        server_url: "http://does-not-exist.fuchsia.com".to_owned(),
    };

    let package = PackageBuilder::new("test-package")
        .add_resource_at("data/test", "hello, world!".as_bytes())
        .build()
        .await
        .unwrap();

    let builder = TestEnvBuilder::new()
        .test_executor(OtaComponentTestExecutor::new())
        .add_package(package)
        .add_image("zbi.signed", "ZBI".as_bytes())
        .omaha_state(OmahaState::Manual(bad_omaha_config));

    let env = builder.build().await.expect("failed to build TestEnv");

    let result = env.run().await.expect("failed to run TestEnv");

    assert_eq!(
        result.progress_renderer_requests,
        vec![
            frui::ProgressRendererRender2Request {
                status: Some(frui::Status::Active),
                percent_complete: Some(0.0),
                ..frui::ProgressRendererRender2Request::EMPTY
            },
            frui::ProgressRendererRender2Request {
                status: Some(frui::Status::Error),
                ..frui::ProgressRendererRender2Request::EMPTY
            },
        ]
    );

    assert_eq!(result.paver.take_events(), vec![]);
    Ok(())
}

// TODO(b/257130699): Add more test cases to cover blobfs issues and invalid configurations.
