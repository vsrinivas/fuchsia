// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::manifest::{Flash, FlashManifest},
    anyhow::{Context, Result},
    ffx_core::{ffx_bail, ffx_plugin},
    ffx_flash_args::FlashCommand,
    fidl_fuchsia_developer_bridge::FastbootProxy,
    std::fs::File,
    std::io::{stdout, BufReader, Read, Write},
    std::path::Path,
};

mod manifest;

#[ffx_plugin()]
pub async fn flash(fastboot_proxy: FastbootProxy, cmd: FlashCommand) -> Result<()> {
    let path = Path::new(&cmd.manifest);
    if !path.is_file() {
        ffx_bail!("File does not exist: {}", cmd.manifest);
    }
    let mut writer = Box::new(stdout());
    let reader = File::open(path).context("opening file for read").map(BufReader::new)?;
    flash_impl(&mut writer, reader, fastboot_proxy, cmd).await
}

async fn flash_impl<W: Write + Send, R: Read>(
    mut writer: W,
    reader: R,
    fastboot_proxy: FastbootProxy,
    cmd: FlashCommand,
) -> Result<()> {
    FlashManifest::load(reader)?.flash(&mut writer, fastboot_proxy, cmd).await
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use ffx_flash_args::OemFile;
    use fidl_fuchsia_developer_bridge::FastbootRequest;
    use regex::Regex;
    use std::default::Default;
    use std::io::BufWriter;
    use std::sync::{Arc, Mutex};
    use tempfile::NamedTempFile;

    #[derive(Default)]
    struct FakeServiceCommands {
        staged_files: Vec<String>,
        oem_commands: Vec<String>,
    }

    fn setup() -> (Arc<Mutex<FakeServiceCommands>>, FastbootProxy) {
        let state = Arc::new(Mutex::new(FakeServiceCommands { ..Default::default() }));
        (
            state.clone(),
            setup_fake_fastboot_proxy(move |req| match req {
                FastbootRequest::Flash { partition_name: _, path: _, responder } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::Erase { partition_name: _, responder } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::Reboot { responder } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::RebootBootloader { listener, responder } => {
                    listener.into_proxy().unwrap().on_reboot().unwrap();
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::ContinueBoot { responder } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::SetActive { slot: _, responder } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::Stage { path, responder } => {
                    let mut state = state.lock().unwrap();
                    state.staged_files.push(path);
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::Oem { command, responder } => {
                    let mut state = state.lock().unwrap();
                    state.oem_commands.push(command);
                    responder.send(&mut Ok(())).unwrap();
                }
            }),
        )
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_nonexistent_file_throws_err() {
        assert!(flash(
            setup().1,
            FlashCommand { manifest: "ffx_test_does_not_exist".to_string(), ..Default::default() }
        )
        .await
        .is_err())
    }

    ////////////////////////////////////////////////////////////////////////////////
    // V1 tests

    const V1_MANIFEST: &'static str = r#"{
        "version": 1,
        "manifest": [ 
            {
                "name": "zedboot", 
                "bootloader_partitions": [
                    ["test1", "path1"],
                    ["test2", "path2"]
                ],
                "partitions": [
                    ["test1", "path1"],
                    ["test2", "path2"],
                    ["test3", "path3"],
                    ["test4", "path4"],
                    ["test5", "path5"]
                ],
                "oem_files": [
                    ["test1", "path1"],
                    ["test2", "path2"]
                ]
            },
            {
                "name": "product", 
                "bootloader_partitions": [
                    ["test1", "path1"],
                    ["test2", "path2"]
                ],
                "partitions": [
                    ["test10", "path10"],
                    ["test20", "path20"],
                    ["test30", "path30"]
                ],
                "oem_files": [
                    ["test1", "path1"],
                    ["test2", "path2"]
                ]
            }
        ]
    }"#;

    const V1_SIMPLE_MANIFEST: &'static str = r#"{
        "version": 1,
        "manifest": [
            {
                "name": "zedboot",
                "bootloader_partitions": [],
                "partitions": [
                    ["test1", "path1"],
                    ["test2", "path2"],
                    ["test3", "path3"],
                    ["test4", "path4"],
                    ["test5", "path5"]
                ],
                "oem_files": []
            }
        ]
    }"#;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_no_product_should_succeed_if_one_product() -> Result<()> {
        let mut output = String::new();
        let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
        let manifest_contents = V1_SIMPLE_MANIFEST.to_string();
        flash_impl(
            writer,
            manifest_contents.as_bytes(),
            setup().1,
            FlashCommand { manifest: "whatever".to_string(), ..Default::default() },
        )
        .await?;
        let FlashManifest::V1(manifest) = FlashManifest::load(manifest_contents.as_bytes())?;
        for partition in &manifest.0[0].partitions {
            let name_listing = Regex::new(&partition.name()).expect("test regex");
            let path_listing = Regex::new(&partition.file()).expect("test regex");
            assert_eq!(1, name_listing.find_iter(&output).count());
            assert_eq!(1, path_listing.find_iter(&output).count());
        }
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_no_product_should_fail_if_multiple_products() -> Result<()> {
        let mut output = String::new();
        let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
        let manifest_contents = V1_MANIFEST.to_string();
        assert!(flash_impl(
            writer,
            manifest_contents.as_bytes(),
            setup().1,
            FlashCommand { manifest: "whatever".to_string(), ..Default::default() },
        )
        .await
        .is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_no_product_should_fail_if_product_missing() -> Result<()> {
        let mut output = String::new();
        let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
        let manifest_contents = V1_MANIFEST.to_string();
        assert!(flash_impl(
            writer,
            manifest_contents.as_bytes(),
            setup().1,
            FlashCommand {
                manifest: "whatever".to_string(),
                product: Some("Unknown".to_string()),
                ..Default::default()
            }
        )
        .await
        .is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_should_succeed_if_product_found() -> Result<()> {
        let mut output = String::new();
        let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
        let manifest_contents = V1_MANIFEST.to_string();
        flash_impl(
            writer,
            manifest_contents.as_bytes(),
            setup().1,
            FlashCommand {
                manifest: "whatever".to_string(),
                product: Some("product".to_string()),
                ..Default::default()
            },
        )
        .await?;
        let FlashManifest::V1(manifest) = FlashManifest::load(manifest_contents.as_bytes())?;
        for partition in &manifest.0[1].partitions {
            let name_listing = Regex::new(&partition.name()).expect("test regex");
            let path_listing = Regex::new(&partition.file()).expect("test regex");
            assert_eq!(1, name_listing.find_iter(&output).count());
            assert_eq!(1, path_listing.find_iter(&output).count());
        }
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_oem_file_should_be_staged_from_command() -> Result<()> {
        let mut output = String::new();
        let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
        let manifest_contents = V1_SIMPLE_MANIFEST.to_string();
        let (state, proxy) = setup();

        let test_oem_cmd = "test-oem-cmd";
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        let test_staged_file = format!("{},{}", test_oem_cmd, tmp_file_name).parse::<OemFile>()?;

        flash_impl(
            writer,
            manifest_contents.as_bytes(),
            proxy,
            FlashCommand {
                manifest: "whatever".to_string(),
                oem_stage: vec![test_staged_file],
                ..Default::default()
            },
        )
        .await?;
        let state = state.lock().unwrap();
        assert_eq!(1, state.staged_files.len());
        assert_eq!(1, state.oem_commands.len());
        Ok(())
    }
}
