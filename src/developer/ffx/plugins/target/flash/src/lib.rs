// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::manifest::{Flash, FlashManifest},
    anyhow::{Context, Result},
    ffx_config::file,
    ffx_core::{ffx_bail, ffx_plugin},
    ffx_flash_args::{FlashCommand, OemFile},
    fidl_fuchsia_developer_bridge::FastbootProxy,
    std::fs::File,
    std::io::{stdout, BufReader, Read, Write},
    std::path::Path,
};

mod manifest;

const SSH_OEM_COMMAND: &str = "add-staged-bootloader-file ssh.authorized_keys";

#[ffx_plugin()]
pub async fn flash(
    fastboot_proxy: FastbootProxy,
    // TODO(fxb/74841): remove allow attribute
    #[allow(unused_mut)] mut cmd: FlashCommand,
) -> Result<()> {
    let path = Path::new(&cmd.manifest);
    if !path.is_file() {
        ffx_bail!("Manifest \"{}\" is not a file.", cmd.manifest);
    }
    match cmd.ssh_key.as_ref() {
        Some(ssh) => {
            let ssh_file = Path::new(ssh);
            if !ssh_file.is_file() {
                ffx_bail!("SSH key \"{}\" is not a file.", ssh);
            }
            if cmd.oem_stage.iter().find(|f| f.command() == SSH_OEM_COMMAND).is_some() {
                ffx_bail!("Both the SSH key and the SSH OEM Stage flags were set. Only use one.");
            }
            cmd.oem_stage.push(OemFile::new(SSH_OEM_COMMAND.to_string(), ssh.to_string()));
        }
        None => {
            if cmd.oem_stage.iter().find(|f| f.command() == SSH_OEM_COMMAND).is_none() {
                let key: Option<String> = file("ssh.pub").await?;
                match key {
                    Some(k) => {
                        println!("No `--ssh-key` flag, using {}", k);
                        cmd.oem_stage.push(OemFile::new(SSH_OEM_COMMAND.to_string(), k));
                    }
                    None => ffx_bail!(
                        "Warning: flashing without a SSH key is not advised. \n\
                              Use the `--ssh-key` to pass a ssh key."
                    ),
                }
            }
        }
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
                FastbootRequest::Flash { listener, responder, .. } => {
                    listener.into_proxy().unwrap().on_finished().unwrap();
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::Erase { responder, .. } => {
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
                FastbootRequest::SetActive { responder, .. } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::Stage { path, responder, .. } => {
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_nonexistent_ssh_file_throws_err() {
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        assert!(flash(
            setup().1,
            FlashCommand {
                manifest: tmp_file_name,
                ssh_key: Some("ssh_does_not_exist".to_string()),
                ..Default::default()
            }
        )
        .await
        .is_err())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_using_and_oem_stage_and_not_ssh_param_does_not_throw_err() {
        let manifest_file = NamedTempFile::new().expect("tmp access failed");
        let manifest_path = manifest_file.path().to_string_lossy().to_string();
        let ssh_file = NamedTempFile::new().expect("tmp access failed");
        let ssh_path = ssh_file.path().to_string_lossy().to_string();
        let mut oem_stage = Vec::new();
        oem_stage.push(OemFile::new(SSH_OEM_COMMAND.to_string(), ssh_path));
        assert!(flash(
            setup().1,
            FlashCommand { manifest: manifest_path, oem_stage, ..Default::default() }
        )
        .await
        .is_err())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_using_both_ssh_and_oem_stage_param_throws_err() {
        let manifest_file = NamedTempFile::new().expect("tmp access failed");
        let manifest_path = manifest_file.path().to_string_lossy().to_string();
        let ssh_file = NamedTempFile::new().expect("tmp access failed");
        let ssh_path = ssh_file.path().to_string_lossy().to_string();
        let mut oem_stage = Vec::new();
        oem_stage.push(OemFile::new(SSH_OEM_COMMAND.to_string(), ssh_path.clone()));
        assert!(flash(
            setup().1,
            FlashCommand {
                manifest: manifest_path,
                ssh_key: Some(ssh_path),
                oem_stage,
                ..Default::default()
            }
        )
        .await
        .is_err())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_not_using_ssh_switch_and_no_config_throws_an_err() {
        let manifest_file = NamedTempFile::new().expect("tmp access failed");
        let manifest_path = manifest_file.path().to_string_lossy().to_string();
        assert!(flash(setup().1, FlashCommand { manifest: manifest_path, ..Default::default() })
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

    const V1_MANIFEST_FUCHSIA: &'static str = r#"{
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
                "name": "fuchsia",
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
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        flash_impl(
            writer,
            manifest_contents.as_bytes(),
            setup().1,
            FlashCommand { manifest: tmp_file_name, ..Default::default() },
        )
        .await?;
        let FlashManifest::V1(manifest) = FlashManifest::load(manifest_contents.as_bytes())?;
        for partition in &manifest.0[0].partitions {
            let name_listing = Regex::new(&partition.name()).expect("test regex");
            let path_listing = Regex::new(&partition.file()).expect("test regex");
            assert_eq!(name_listing.find_iter(&output).count(), 1);
            assert_eq!(path_listing.find_iter(&output).count(), 1);
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
    async fn test_no_product_should_succeed_if_multiple_products_and_fuchsia() -> Result<()> {
        let mut output = String::new();
        let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
        let manifest_contents = V1_MANIFEST_FUCHSIA.to_string();
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        flash_impl(
            writer,
            manifest_contents.as_bytes(),
            setup().1,
            FlashCommand { manifest: tmp_file_name, ..Default::default() },
        )
        .await
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
        let tmp_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_file_name = tmp_file.path().to_string_lossy().to_string();
        flash_impl(
            writer,
            manifest_contents.as_bytes(),
            setup().1,
            FlashCommand {
                manifest: tmp_file_name,
                product: Some("product".to_string()),
                ..Default::default()
            },
        )
        .await?;
        let FlashManifest::V1(manifest) = FlashManifest::load(manifest_contents.as_bytes())?;
        for partition in &manifest.0[1].partitions {
            let name_listing = Regex::new(&partition.name()).expect("test regex");
            let path_listing = Regex::new(&partition.file()).expect("test regex");
            assert_eq!(name_listing.find_iter(&output).count(), 1);
            assert_eq!(path_listing.find_iter(&output).count(), 1);
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

        let tmp_manifest_file = NamedTempFile::new().expect("tmp access failed");
        let tmp_manifest_file_name = tmp_manifest_file.path().to_string_lossy().to_string();

        flash_impl(
            writer,
            manifest_contents.as_bytes(),
            proxy,
            FlashCommand {
                manifest: tmp_manifest_file_name,
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
