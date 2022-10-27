// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use {
    anyhow::{Context, Error},
    assert_matches::assert_matches,
    fidl::endpoints::{ClientEnd, RequestStream, ServerEnd},
    fidl_fuchsia_io as fio,
    fidl_fuchsia_paver::{Asset, Configuration},
    fidl_fuchsia_pkg_ext::{MirrorConfigBuilder, RepositoryConfigBuilder, RepositoryConfigs},
    fuchsia_async as fasync,
    fuchsia_pkg_testing::{make_current_epoch_json, Package, PackageBuilder},
    fuchsia_zircon as zx,
    futures::prelude::*,
    http::uri::Uri,
    isolated_ota::{OmahaConfig, UpdateError},
    isolated_ota_env::{OmahaState, TestEnvBuilder},
    mock_omaha_server::OmahaResponse,
    mock_paver::{hooks as mphooks, PaverEvent},
};

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
