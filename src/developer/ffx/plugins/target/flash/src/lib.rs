// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        boot::flash_boot,
        common::{file::EmptyResolver, prepare},
        info::flash_info,
        lock::flash_lock,
        manifest::{from_path, from_sdk},
        unlock::flash_unlock,
    },
    anyhow::Result,
    errors::ffx_bail,
    ffx_config::{file, sdk::SdkVersion},
    ffx_core::ffx_plugin,
    ffx_flash_args::{
        BootCommand, FlashCommand, OemFile,
        Subcommand::{Boot, Info, Lock, Unlock},
        UnlockCommand,
    },
    fidl_fuchsia_developer_bridge::FastbootProxy,
    std::io::{stdin, stdout, Write},
    std::path::Path,
};

mod boot;
mod common;
mod info;
mod lock;
mod manifest;
mod unlock;

const SSH_OEM_COMMAND: &str = "add-staged-bootloader-file ssh.authorized_keys";

const WARNING: &str = "WARNING: ALL SETTINGS USER CONTENT WILL BE ERASED!\n\
                        Do you want to continue? [yN]";

const MISSING_ZBI: &str = "Error: vbmeta parameter must be used with zbi parameter";

#[ffx_plugin()]
pub async fn flash(fastboot_proxy: FastbootProxy, cmd: FlashCommand) -> Result<()> {
    flash_plugin_impl(fastboot_proxy, cmd, &mut stdout()).await
}

pub async fn flash_plugin_impl<W: Write>(
    fastboot_proxy: FastbootProxy,
    mut cmd: FlashCommand,
    writer: &mut W,
) -> Result<()> {
    // Some operations don't need the manifest, just return early.
    match &cmd.subcommand {
        Some(Info(_)) => return flash_info(writer, &fastboot_proxy).await,
        Some(Lock(_)) => return flash_lock(writer, &fastboot_proxy).await,
        Some(Unlock(UnlockCommand { cred, force })) => {
            if !force {
                writeln!(writer, "{}", WARNING)?;
                let answer = blocking::unblock(|| {
                    use std::io::BufRead;
                    let mut line = String::new();
                    let stdin = stdin();
                    let mut locked = stdin.lock();
                    let _ = locked.read_line(&mut line);
                    line
                })
                .await;
                if answer.trim() != "y" {
                    ffx_bail!("User aborted");
                }
            }
            match cred {
                Some(cred_file) => {
                    prepare(writer, &fastboot_proxy).await?;
                    return flash_unlock(
                        writer,
                        &mut EmptyResolver::new()?,
                        &vec![cred_file.to_string()],
                        &fastboot_proxy,
                    )
                    .await;
                }
                _ => {}
            }
        }
        Some(Boot(BootCommand { zbi, vbmeta, .. })) => {
            if vbmeta.is_some() && zbi.is_none() {
                ffx_bail!("{}", MISSING_ZBI)
            }
            match zbi {
                Some(z) => {
                    prepare(writer, &fastboot_proxy).await?;
                    return flash_boot(
                        writer,
                        &mut EmptyResolver::new()?,
                        z.to_owned(),
                        vbmeta.to_owned(),
                        &fastboot_proxy,
                    )
                    .await;
                }
                _ => {}
            }
        }
        _ => {}
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
                        // Only needed when flashing.
                        if cmd.subcommand.is_none() {
                            eprintln!("No `--ssh-key` flag, using {}", k);
                            cmd.oem_stage.push(OemFile::new(SSH_OEM_COMMAND.to_string(), k));
                        }
                    }
                    None => ffx_bail!(
                        "Warning: flashing without a SSH key is not advised. \n\
                              Use the `--ssh-key` to pass a ssh key."
                    ),
                }
            }
        }
    }

    match &cmd.manifest {
        Some(manifest) => {
            if !manifest.is_file() {
                ffx_bail!("Manifest \"{}\" is not a file.", manifest.display());
            }
            from_path(writer, manifest.to_path_buf(), fastboot_proxy, cmd).await
        }
        None => {
            let sdk = ffx_config::get_sdk().await?;
            if !matches!(sdk.get_version(), SdkVersion::InTree) {
                // Currently SDK flashing only works for InTree.
                // TODO(fxb/82166) - Make SDK flashing work for out of tree use case.
                ffx_bail!("No manifest path was given, and no manifest was found in the configured SDK root ({})", 
                          sdk.get_path_prefix().display());
            }
            let mut path = sdk.get_path_prefix().to_path_buf();
            path.push("flash.json"); // Not actually used, placeholder value needed.
            writeln!(
                writer,
                "No manifest path was given, using SDK from {}.",
                sdk.get_path_prefix().display()
            )?;
            from_sdk(writer, path, fastboot_proxy, cmd).await
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use crate::common::file::FileResolver;
    use ffx_flash_args::LockCommand;
    use fidl_fuchsia_developer_bridge::FastbootRequest;
    use std::default::Default;
    use std::path::PathBuf;
    use std::sync::{Arc, Mutex};
    use tempfile::NamedTempFile;

    #[derive(Default)]
    pub(crate) struct FakeServiceCommands {
        pub(crate) staged_files: Vec<String>,
        pub(crate) oem_commands: Vec<String>,
        pub(crate) variables: Vec<String>,
        pub(crate) bootloader_reboots: usize,
        pub(crate) boots: usize,
    }

    pub(crate) struct TestResolver {
        manifest: PathBuf,
    }

    impl TestResolver {
        pub(crate) fn new() -> Self {
            let mut test = PathBuf::new();
            test.push("./flash.json");
            Self { manifest: test }
        }
    }

    impl FileResolver for TestResolver {
        fn manifest(&self) -> &Path {
            self.manifest.as_path()
        }

        fn get_file<W: Write>(&mut self, _writer: &mut W, file: &str) -> Result<String> {
            Ok(file.to_owned())
        }
    }

    pub(crate) fn setup() -> (Arc<Mutex<FakeServiceCommands>>, FastbootProxy) {
        let state = Arc::new(Mutex::new(FakeServiceCommands { ..Default::default() }));
        (
            state.clone(),
            setup_fake_fastboot_proxy(move |req| match req {
                FastbootRequest::Prepare { listener, responder } => {
                    listener.into_proxy().unwrap().on_reboot().unwrap();
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::GetVar { responder, .. } => {
                    let mut state = state.lock().unwrap();
                    let var = state.variables.pop().unwrap_or("test".to_string());
                    responder.send(&mut Ok(var)).unwrap();
                }
                FastbootRequest::GetAllVars { listener, responder, .. } => {
                    listener.into_proxy().unwrap().on_variable("test", "test").unwrap();
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::Flash { listener, responder, .. } => {
                    listener.into_proxy().unwrap().on_finished().unwrap();
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::GetStaged { responder, .. } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::Erase { responder, .. } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::Boot { responder } => {
                    let mut state = state.lock().unwrap();
                    state.boots += 1;
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::Reboot { responder } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::RebootBootloader { listener, responder } => {
                    listener.into_proxy().unwrap().on_reboot().unwrap();
                    let mut state = state.lock().unwrap();
                    state.bootloader_reboots += 1;
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
            FlashCommand {
                manifest: Some(PathBuf::from("ffx_test_does_not_exist")),
                ..Default::default()
            }
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
                manifest: Some(PathBuf::from(tmp_file_name)),
                ssh_key: Some("ssh_does_not_exist".to_string()),
                ..Default::default()
            }
        )
        .await
        .is_err())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_boot_stages_file_and_calls_boot() -> Result<()> {
        let zbi_file = NamedTempFile::new().expect("tmp access failed");
        let zbi_file_name = zbi_file.path().to_string_lossy().to_string();
        let vbmeta_file = NamedTempFile::new().expect("tmp access failed");
        let vbmeta_file_name = vbmeta_file.path().to_string_lossy().to_string();
        let (state, proxy) = setup();
        flash(
            proxy,
            FlashCommand {
                manifest: None,
                subcommand: Some(Boot(BootCommand {
                    zbi: Some(zbi_file_name),
                    vbmeta: Some(vbmeta_file_name),
                    slot: "a".to_string(),
                })),
                ..Default::default()
            },
        )
        .await?;
        let state = state.lock().unwrap();
        assert_eq!(1, state.staged_files.len());
        assert_eq!(1, state.boots);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_boot_stages_file_and_calls_boot_with_just_zbi() -> Result<()> {
        let zbi_file = NamedTempFile::new().expect("tmp access failed");
        let zbi_file_name = zbi_file.path().to_string_lossy().to_string();
        let (state, proxy) = setup();
        flash(
            proxy,
            FlashCommand {
                manifest: None,
                subcommand: Some(Boot(BootCommand {
                    zbi: Some(zbi_file_name),
                    vbmeta: None,
                    slot: "a".to_string(),
                })),
                ..Default::default()
            },
        )
        .await?;
        let state = state.lock().unwrap();
        assert_eq!(1, state.staged_files.len());
        assert_eq!(1, state.boots);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_boot_fails_with_just_vbmeta() {
        let vbmeta_file = NamedTempFile::new().expect("tmp access failed");
        let vbmeta_file_name = vbmeta_file.path().to_string_lossy().to_string();
        let (_, proxy) = setup();
        assert!(flash(
            proxy,
            FlashCommand {
                manifest: None,
                subcommand: Some(Boot(BootCommand {
                    zbi: None,
                    vbmeta: Some(vbmeta_file_name),
                    slot: "a".to_string(),
                })),
                ..Default::default()
            },
        )
        .await
        .is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_lock_calls_oem_command() -> Result<()> {
        let (state, proxy) = setup();
        {
            let mut state = state.lock().unwrap();
            // is_locked
            state.variables.push("no".to_string());
        }
        flash(
            proxy,
            FlashCommand {
                manifest: None,
                subcommand: Some(Lock(LockCommand {})),
                ..Default::default()
            },
        )
        .await?;
        let state = state.lock().unwrap();
        assert_eq!(1, state.oem_commands.len());
        assert_eq!("vx-lock".to_string(), state.oem_commands[0]);
        Ok(())
    }
}
