// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Context, Error},
    fidl_fuchsia_mem::Buffer,
    fidl_fuchsia_paver::{Asset, Configuration, DataSinkProxy, WriteFirmwareResult},
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_zircon::{Status, VmoChildOptions},
    thiserror::Error,
    update_package::{Image, UpdatePackage},
};

#[derive(Debug, Error)]
enum WriteAssetError {
    #[error("while performing write_asset call")]
    Fidl(#[from] fidl::Error),

    #[error("write_asset responded with")]
    Status(#[from] Status),
}

async fn paver_write_firmware(
    paver: &DataSinkProxy,
    subtype: &str,
    mut buffer: Buffer,
) -> Result<(), Error> {
    let res = paver
        .write_firmware(subtype, &mut buffer)
        .await
        .context("while performing write_firmware call")?;

    match res {
        WriteFirmwareResult::Status(status) => {
            Status::ok(status).context("firmware failed to write")?;
        }
        WriteFirmwareResult::UnsupportedType(_) => {
            fx_log_info!("skipping unsupported firmware type: {}", subtype);
        }
    }

    Ok(())
}

async fn paver_write_asset(
    paver: &DataSinkProxy,
    configuration: Configuration,
    asset: Asset,
    mut buffer: Buffer,
) -> Result<(), WriteAssetError> {
    let status = paver.write_asset(configuration, asset, &mut buffer).await?;
    Status::ok(status)?;
    Ok(())
}

/// The [`fidl_fuchsia_paver::Configuration`]s to which an image should be written.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TargetConfiguration {
    /// The image has a well defined configuration to which it should be written. For example,
    /// Recovery, or, for devices that support ABR, the "inactive" configuration.
    Single(Configuration),

    /// The image would target the A or B configuration, but ABR is not supported on this platform,
    /// so write to A and try to write to B (if a B partition exists).
    AB,
}

impl TargetConfiguration {
    /// For an image that should target the inactive configuration, determine the appropriate
    /// [`TargetConfiguration`].
    fn from_inactive_config(inactive_config: Option<Configuration>) -> Self {
        match inactive_config {
            Some(config) => TargetConfiguration::Single(config),
            None => TargetConfiguration::AB,
        }
    }
}

#[derive(Debug, PartialEq, Eq)]
enum ImageTarget<'a> {
    Firmware { subtype: &'a str },
    Asset { asset: Asset, configuration: TargetConfiguration },
}

fn classify_image(
    image: &Image,
    inactive_config: Option<Configuration>,
) -> Result<ImageTarget<'_>, Error> {
    let target = match image.name() {
        "zbi" | "zbi.signed" => ImageTarget::Asset {
            asset: Asset::Kernel,
            configuration: TargetConfiguration::from_inactive_config(inactive_config),
        },
        "fuchsia.vbmeta" => ImageTarget::Asset {
            asset: Asset::VerifiedBootMetadata,
            configuration: TargetConfiguration::from_inactive_config(inactive_config),
        },
        "zedboot" | "zedboot.signed" => ImageTarget::Asset {
            asset: Asset::Kernel,
            configuration: TargetConfiguration::Single(Configuration::Recovery),
        },
        "recovery.vbmeta" => ImageTarget::Asset {
            asset: Asset::VerifiedBootMetadata,
            configuration: TargetConfiguration::Single(Configuration::Recovery),
        },
        "bootloader" | "firmware" => {
            // Keep support for update packages still using the older "bootloader" file, which is
            // haandled identically to "firmware" but without subtype support.
            ImageTarget::Firmware { subtype: image.subtype().unwrap_or("") }
        }
        _ => bail!("unrecognized image: {}", image.name()),
    };

    if let (ImageTarget::Asset { .. }, Some(subtype)) = (&target, image.subtype()) {
        bail!("unsupported subtype {:?} for asset {:?}", subtype, target);
    }

    Ok(target)
}

struct Payload<'a> {
    display_name: &'a str,
    buffer: Buffer,
}

impl Payload<'_> {
    fn clone_buffer(&self) -> Result<Buffer, Status> {
        Ok(Buffer {
            vmo: self.buffer.vmo.create_child(
                VmoChildOptions::COPY_ON_WRITE | VmoChildOptions::RESIZABLE,
                0,
                self.buffer.size,
            )?,
            size: self.buffer.size,
        })
    }
}

/// Writes the given image to the configuration/asset location. If configuration is not given, the
/// image is written to both A and B (if the B partition exists).
async fn write_asset_to_configurations(
    paver: &DataSinkProxy,
    configuration: TargetConfiguration,
    asset: Asset,
    payload: Payload<'_>,
) -> Result<(), Error> {
    match configuration {
        TargetConfiguration::Single(configuration) => {
            // Devices supports ABR and/or a specific configuration (ex. Recovery) was requested.
            paver_write_asset(paver, configuration, asset, payload.buffer).await?;
        }
        TargetConfiguration::AB => {
            // Device does not support ABR, so write the image to the A partition.
            //
            // Also try to write the image to the B partition to be forwards compatible with devices
            // that will eventually support ABR. If the device does not have a B partition yet, log the
            // error and continue.

            paver_write_asset(paver, Configuration::A, asset, payload.clone_buffer()?).await?;

            let res = paver_write_asset(paver, Configuration::B, asset, payload.buffer).await;
            if let Err(WriteAssetError::Status(Status::NOT_SUPPORTED)) = res {
                fx_log_warn!("skipping writing {} to B", payload.display_name);
            } else {
                res?;
            }
        }
    }

    Ok(())
}

async fn write_image_buffer(
    paver: &DataSinkProxy,
    buffer: Buffer,
    image: &Image,
    inactive_config: Option<Configuration>,
) -> Result<(), Error> {
    let target = classify_image(image, inactive_config)?;
    let payload = Payload { display_name: image.filename(), buffer };

    match target {
        ImageTarget::Firmware { subtype } => {
            paver_write_firmware(paver, subtype, payload.buffer).await?;
        }
        ImageTarget::Asset { asset, configuration } => {
            write_asset_to_configurations(paver, configuration, asset, payload).await?;
        }
    }

    Ok(())
}

/// Write the given `image` from the update package through the paver to the appropriate
/// partitions.
// TODO(49911) use this. Note: this behavior will be tested with the integration tests.
// TODO(49911) ensure the error isn't logged twice when this is wired up in main.
#[allow(dead_code)]
pub async fn write_image(
    paver: &DataSinkProxy,
    update_pkg: UpdatePackage,
    image: Image,
    inactive_config: Option<Configuration>,
) -> Result<(), Error> {
    let buffer = update_pkg
        .open_image(&image)
        .await
        .with_context(|| format!("while opening {}", image.filename()))?;

    fx_log_info!("writing {} from update package", image.filename());

    let res = write_image_buffer(paver, buffer, &image, inactive_config).await;

    match &res {
        Ok(()) => fx_log_info!("wrote {} successfully", image.filename()),
        Err(e) => fx_log_err!("error writing {}: {:#}", image.filename(), e),
    }
    res
}

#[cfg(test)]
fn make_buffer(contents: impl AsRef<[u8]>) -> Buffer {
    use {
        fuchsia_zircon::{Vmo, VmoOptions},
        std::convert::TryInto,
    };
    let contents = contents.as_ref();
    let size = contents.len().try_into().unwrap();

    let vmo = Vmo::create_with_opts(VmoOptions::RESIZABLE, size).unwrap();
    vmo.write(contents, 0).unwrap();

    Buffer { vmo, size }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_paver::DataSinkMarker,
        matches::assert_matches,
        mock_paver::{MockPaverServiceBuilder, PaverEvent},
        std::sync::Arc,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_writes_firmware() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("firmware contents"),
            &Image::new("bootloader"),
            Some(Configuration::A), // Configuration is ignored for firmware
        )
        .await
        .unwrap();

        write_image_buffer(
            &data_sink,
            make_buffer("firmware_foo contents"),
            &Image::join("firmware", "foo"),
            None,
        )
        .await
        .unwrap();

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::WriteFirmware {
                    firmware_type: "".to_owned(),
                    payload: b"firmware contents".to_vec()
                },
                PaverEvent::WriteFirmware {
                    firmware_type: "foo".to_owned(),
                    payload: b"firmware_foo contents".to_vec()
                }
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_ignores_unsupported_firmware() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .firmware_hook(|event| match event {
                    PaverEvent::WriteFirmware { .. } => WriteFirmwareResult::UnsupportedType(true),
                    _ => panic!("Unexpected event: {:?}", event),
                })
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("firmware of the future!"),
            &Image::join("firmware", "unknown"),
            None,
        )
        .await
        .unwrap();

        assert_eq!(
            paver.take_events(),
            vec![PaverEvent::WriteFirmware {
                firmware_type: "unknown".to_owned(),
                payload: b"firmware of the future!".to_vec()
            },]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_forwards_other_firmware_errors() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .firmware_hook(|event| match event {
                    PaverEvent::WriteFirmware { .. } => {
                        WriteFirmwareResult::Status(Status::INTERNAL.into_raw())
                    }
                    _ => panic!("Unexpected event: {:?}", event),
                })
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("oops"),
                &Image::join("firmware", "error"),
                None,
            )
            .await,
            Err(e) if e.to_string().contains("firmware failed to write")
        );

        assert_eq!(
            paver.take_events(),
            vec![PaverEvent::WriteFirmware {
                firmware_type: "error".to_owned(),
                payload: b"oops".to_vec()
            }]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_rejects_unknown_images() {
        let (data_sink, _server_end) = fidl::endpoints::create_proxy::<DataSinkMarker>().unwrap();

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("unknown"),
                &Image::new("unknown"),
                Some(Configuration::A),
            )
            .await,
            Err(e) if e.to_string().contains("unrecognized image")
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_rejects_asset_with_subtype() {
        let (data_sink, _server_end) = fidl::endpoints::create_proxy::<DataSinkMarker>().unwrap();

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("unknown"),
                &Image::join("zbi", "2"),
                Some(Configuration::A),
            )
            .await,
            Err(e) if e.to_string().contains("unsupported subtype")
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_writes_asset_to_single_config() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("zbi contents"),
            &Image::new("zbi"),
            Some(Configuration::A),
        )
        .await
        .unwrap();

        assert_eq!(
            paver.take_events(),
            vec![PaverEvent::WriteAsset {
                configuration: Configuration::A,
                asset: Asset::Kernel,
                payload: b"zbi contents".to_vec()
            }]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_write_asset_forwards_errors() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .call_hook(|event| match event {
                    PaverEvent::WriteAsset { .. } => Status::INTERNAL,
                    _ => panic!("Unexpected event: {:?}", event),
                })
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("zbi contents"),
                &Image::new("zbi"),
                Some(Configuration::A),
            )
            .await,
            Err(e) if e.to_string().contains("write_asset responded with")
        );

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("vbmeta contents"),
                &Image::new("fuchsia.vbmeta"),
                Some(Configuration::A),
            )
            .await,
            Err(e) if e.to_string().contains("write_asset responded with")
        );

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("zbi contents"),
                &Image::new("zbi"),
                Some(Configuration::B),
            )
            .await,
            Err(e) if e.to_string().contains("write_asset responded with")
        );

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("vbmeta contents"),
                &Image::new("fuchsia.vbmeta"),
                Some(Configuration::B),
            )
            .await,
            Err(e) if e.to_string().contains("write_asset responded with")
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_supports_known_image_names() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let data_sink = paver.spawn_data_sink_service();

        for name in &[
            "bootloader",
            "firmware",
            "zbi",
            "zbi.signed",
            "fuchsia.vbmeta",
            "zedboot",
            "zedboot.signed",
            "recovery.vbmeta",
        ] {
            write_image_buffer(
                &data_sink,
                make_buffer(format!("{} buffer", name)),
                &Image::new(*name),
                Some(Configuration::B),
            )
            .await
            .unwrap();
        }

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::WriteFirmware {
                    firmware_type: "".to_owned(),
                    payload: b"bootloader buffer".to_vec()
                },
                PaverEvent::WriteFirmware {
                    firmware_type: "".to_owned(),
                    payload: b"firmware buffer".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::B,
                    asset: Asset::Kernel,
                    payload: b"zbi buffer".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::B,
                    asset: Asset::Kernel,
                    payload: b"zbi.signed buffer".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::B,
                    asset: Asset::VerifiedBootMetadata,
                    payload: b"fuchsia.vbmeta buffer".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::Recovery,
                    asset: Asset::Kernel,
                    payload: b"zedboot buffer".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::Recovery,
                    asset: Asset::Kernel,
                    payload: b"zedboot.signed buffer".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::Recovery,
                    asset: Asset::VerifiedBootMetadata,
                    payload: b"recovery.vbmeta buffer".to_vec()
                },
            ]
        );
    }
}

#[cfg(test)]
mod abr_not_supported_tests {
    use {
        super::*,
        matches::assert_matches,
        mock_paver::{MockPaverServiceBuilder, PaverEvent},
        std::sync::Arc,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_writes_asset_to_both_configs() {
        let paver = Arc::new(MockPaverServiceBuilder::new().build());
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("zbi.signed contents"),
            &Image::new("zbi.signed"),
            None,
        )
        .await
        .unwrap();

        write_image_buffer(
            &data_sink,
            make_buffer("the new vbmeta"),
            &Image::new("fuchsia.vbmeta"),
            None,
        )
        .await
        .unwrap();

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::WriteAsset {
                    configuration: Configuration::A,
                    asset: Asset::Kernel,
                    payload: b"zbi.signed contents".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::B,
                    asset: Asset::Kernel,
                    payload: b"zbi.signed contents".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::A,
                    asset: Asset::VerifiedBootMetadata,
                    payload: b"the new vbmeta".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::B,
                    asset: Asset::VerifiedBootMetadata,
                    payload: b"the new vbmeta".to_vec()
                },
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_forwards_config_a_error() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .call_hook(|event| match event {
                    PaverEvent::WriteAsset {
                        configuration: Configuration::A,
                        asset: Asset::Kernel,
                        payload: _,
                    } => Status::INTERNAL,
                    _ => Status::OK,
                })
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("zbi.signed contents"),
                &Image::new("zbi.signed"),
                None,
            )
            .await,
            Err(_)
        );

        assert_eq!(
            paver.take_events(),
            vec![PaverEvent::WriteAsset {
                configuration: Configuration::A,
                asset: Asset::Kernel,
                payload: b"zbi.signed contents".to_vec()
            },]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_ignores_config_b_not_supported_error() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .call_hook(|event| match event {
                    PaverEvent::WriteAsset {
                        configuration: Configuration::B,
                        asset: Asset::Kernel,
                        payload: _,
                    } => Status::NOT_SUPPORTED,
                    _ => Status::OK,
                })
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        write_image_buffer(
            &data_sink,
            make_buffer("zbi.signed contents"),
            &Image::new("zbi.signed"),
            None,
        )
        .await
        .unwrap();

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::WriteAsset {
                    configuration: Configuration::A,
                    asset: Asset::Kernel,
                    payload: b"zbi.signed contents".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::B,
                    asset: Asset::Kernel,
                    payload: b"zbi.signed contents".to_vec()
                },
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn write_image_buffer_forwards_other_config_b_errors() {
        let paver = Arc::new(
            MockPaverServiceBuilder::new()
                .call_hook(|event| match event {
                    PaverEvent::WriteAsset {
                        configuration: Configuration::B,
                        asset: Asset::Kernel,
                        ..
                    } => Status::INTERNAL,
                    _ => Status::OK,
                })
                .build(),
        );
        let data_sink = paver.spawn_data_sink_service();

        assert_matches!(
            write_image_buffer(
                &data_sink,
                make_buffer("zbi.signed contents"),
                &Image::new("zbi.signed"),
                None,
            )
            .await,
            Err(_)
        );

        assert_eq!(
            paver.take_events(),
            vec![
                PaverEvent::WriteAsset {
                    configuration: Configuration::A,
                    asset: Asset::Kernel,
                    payload: b"zbi.signed contents".to_vec()
                },
                PaverEvent::WriteAsset {
                    configuration: Configuration::B,
                    asset: Asset::Kernel,
                    payload: b"zbi.signed contents".to_vec()
                },
            ]
        );
    }
}
