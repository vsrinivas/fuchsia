// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::args::{KillCommand, StartCommand};
use crate::portpicker::{pick_unused_port, Port};
use crate::types::{read_env_path, HostTools, ImageFiles, SSHKeys, VDLArgs};
use ansi_term::Colour::*;
use anyhow::{format_err, Result};
use std::env;
use std::fs::{copy, File};
use std::io::Write;
use std::path::PathBuf;
use std::process::Command;
use std::str;
use tempfile::{Builder, TempDir};

pub struct VDLFiles {
    image_files: ImageFiles,
    host_tools: HostTools,
    ssh_files: SSHKeys,

    /// A temp directory to stage required files used to start emulator.
    staging_dir: TempDir,

    /// A file created under staging_dir that stores vdl proto output.
    output_proto: PathBuf,

    /// A file created under staging_dir to store emulator output.
    emulator_log: PathBuf,
}

impl VDLFiles {
    pub fn new(is_sdk: bool) -> Result<VDLFiles> {
        let staging_dir = Builder::new().prefix("vdl_staging_").tempdir()?;
        let staging_dir_path = staging_dir.path().to_owned();
        if is_sdk {
            let mut vdl_files = VDLFiles {
                image_files: ImageFiles::from_sdk_env()?,
                host_tools: HostTools::from_sdk_env()?,
                ssh_files: SSHKeys::from_sdk_env()?,
                output_proto: staging_dir_path.join("vdl_proto"),
                emulator_log: staging_dir_path.join("emu_log"),
                staging_dir: staging_dir,
            };

            vdl_files.image_files.stage_files(&staging_dir_path)?;
            vdl_files.ssh_files.stage_files(&staging_dir_path)?;

            Ok(vdl_files)
        } else {
            let mut vdl_files = VDLFiles {
                image_files: ImageFiles::from_tree_env()?,
                host_tools: HostTools::from_tree_env()?,
                ssh_files: SSHKeys::from_tree_env()?,
                output_proto: staging_dir_path.join("vdl_proto"),
                emulator_log: staging_dir_path.join("emu_log"),
                staging_dir: staging_dir,
            };

            vdl_files.image_files.stage_files(&staging_dir_path)?;
            vdl_files.ssh_files.stage_files(&staging_dir_path)?;

            Ok(vdl_files)
        }
    }

    fn provision_zbi(&self) -> Result<PathBuf> {
        let zbi_out = self.staging_dir.path().join("femu_zircona-ed25519");
        let status = Command::new(&self.host_tools.zbi)
            .arg(format!(
                "--compressed={}",
                env::var("FUCHSIA_ZBI_COMPRESSION").unwrap_or("zstd".to_string())
            ))
            .arg("-o")
            .arg(&zbi_out)
            .arg(&self.image_files.zbi)
            .arg("--entry")
            .arg(format!("data/ssh/authorized_keys={}", self.ssh_files.public_key.display()))
            .status()?;
        if status.success() {
            return Ok(zbi_out);
        } else {
            return Err(format_err!(
                "Cannot provision zbi. Exit status was {}",
                status.code().unwrap_or_default()
            ));
        }
    }

    fn assemble_system_images(&self, gcs_build_id: &String) -> Result<String> {
        if gcs_build_id.is_empty() {
            return Ok(format!(
                "{},{},{},{},{},{},{},{}",
                self.ssh_files.private_key.display(),
                self.ssh_files.public_key.display(),
                self.ssh_files.config.display(),
                self.provision_zbi()?.display(),
                self.image_files.kernel.display(),
                self.image_files.fvm.display(),
                self.image_files.build_args.display(),
                self.image_files.amber_files.display(),
            ));
        } else {
            return Ok(format!(
                "{},{},{}",
                self.ssh_files.private_key.display(),
                self.ssh_files.public_key.display(),
                self.ssh_files.config.display(),
            ));
        }
    }

    fn generate_fvd(&self, window_width: &usize, window_height: &usize) -> Result<PathBuf> {
        let data = format!(
            "device_spec {{
  horizontal_resolution: {}
  vertical_resolution: {}
  vm_heap: 192
  ram: 4096
  cache: 32
  screen_density: 240
}}
",
            window_width, window_height
        );
        let fvd_proto = self.staging_dir.path().join("virtual_device.textproto");
        File::create(&fvd_proto)?.write_all(data.as_bytes())?;
        Ok(fvd_proto)
    }

    /// Launches FEMU, opens an SSH session, and waits for the FEMU instance or SSH session to exit.
    pub fn start_emulator(&self, start_command: &StartCommand) -> Result<()> {
        let vdl_args: VDLArgs = start_command.clone().into();
        let fvd = match &start_command.device_proto {
            Some(proto) => PathBuf::from(proto),
            None => self.generate_fvd(&start_command.window_width, &start_command.window_height)?,
        };
        let aemu = match &start_command.aemu_path {
            Some(aemu_path) => PathBuf::from(aemu_path),
            None => {
                if self.host_tools.aemu.as_os_str().is_empty() {
                    self.host_tools
                        .download_and_extract(
                            start_command
                                .aemu_version
                                .as_ref()
                                .unwrap_or(&String::from("integration"))
                                .to_string(),
                            "aemu".to_string(),
                        )?
                        .join("emulator")
                } else {
                    self.host_tools.aemu.clone()
                }
            }
        };
        let grpcwebproxy = match &start_command.grpcwebproxy_path {
            Some(grpcwebproxy_path) => PathBuf::from(grpcwebproxy_path),
            None => {
                if vdl_args.enable_grpcwebproxy && self.host_tools.aemu.as_os_str().is_empty() {
                    self.host_tools
                        .download_and_extract(
                            start_command
                                .grpcwebproxy_version
                                .as_ref()
                                .unwrap_or(&String::from("latest"))
                                .to_string(),
                            "grpcwebproxy".to_string(),
                        )?
                        .join("emulator")
                } else {
                    self.host_tools.grpcwebproxy.clone()
                }
            }
        };
        let vdl = match &start_command.vdl_path {
            Some(vdl_path) => PathBuf::from(vdl_path),
            None => self.host_tools.vdl.clone(),
        };

        let ssh_port = pick_unused_port().unwrap();

        // Enable emulator grpc server if running on linux
        // doc: https://android.googlesource.com/platform/external/qemu/+/refs/heads/emu-master-dev/android/android-grpc/docs
        let enable_emu_controller = match env::consts::OS {
            "linux" => true,
            _ => false,
        };
        let status = Command::new(&vdl)
            .arg("--action=start")
            .arg("--emulator_binary_path")
            .arg(&aemu)
            .arg("--pm_tool")
            .arg(&self.host_tools.pm)
            .arg("--far_tool")
            .arg(&self.host_tools.far)
            .arg("--fvm_tool")
            .arg(&self.host_tools.fvm)
            .arg("--device_finder_tool")
            .arg(&self.host_tools.device_finder)
            .arg("--zbi_tool")
            .arg(&self.host_tools.zbi)
            .arg("--grpcwebproxy_tool")
            .arg(&grpcwebproxy)
            .arg(format!(
                "--system_images={}",
                self.assemble_system_images(&vdl_args.gcs_build_id)?
            ))
            .arg(format!("--host_port_map=hostfwd=tcp::{}-:22", ssh_port))
            .arg("--output_launched_device_proto")
            .arg(&self.output_proto)
            .arg("--emu_log")
            .arg(&self.emulator_log)
            .arg("--proto_file_path")
            .arg(&fvd)
            .arg("--audio=true")
            .arg(format!("--resize_fvm={}", vdl_args.image_size))
            .arg(format!("--gpu={}", vdl_args.gpu))
            .arg(format!("--headless_mode={}", vdl_args.headless))
            .arg(format!("--tuntap={}", vdl_args.tuntap))
            .arg(format!("--upscript={}", vdl_args.upscript))
            .arg(format!("--serve_packages={}", vdl_args.packages_to_serve))
            .arg(format!("--pointing_device={}", vdl_args.pointing_device))
            .arg(format!("--enable_webrtc={}", vdl_args.enable_grpcwebproxy))
            .arg(format!("--grpcwebproxy_port={}", vdl_args.grpcwebproxy_port))
            .arg(format!("--gcs_bucket={}", vdl_args.gcs_bucket))
            .arg(format!("--build_id={}", vdl_args.gcs_build_id))
            .arg(format!("--image_archive={}", vdl_args.gcs_image_archive))
            .arg(format!("--enable_emu_controller={}", enable_emu_controller))
            .arg(format!("--hidpi_scaling={}", vdl_args.enable_hidpi_scaling))
            .status()?;
        if !status.success() {
            let persistent_emu_log = read_env_path("FUCHSIA_OUT_DIR")
                .unwrap_or(env::current_dir()?)
                .join("emu_crash.log");
            copy(&self.emulator_log, &persistent_emu_log)?;
            return Err(format_err!(
                "Cannot start Fuchsia Emulator. Exit status is {}\nEmulator log is copied to {}",
                status.code().unwrap_or_default(),
                persistent_emu_log.display()
            ));
        }
        if vdl_args.tuntap {
            println!("{}", Yellow.paint("To support fx tools on emulator, please run \"fx set-device step-atom-yard-juicy\""));
        } else {
            println!(
                "{}",
                Yellow.paint(format!(
                    "To support fx tools on emulator, please run \"fx set-device 127.0.0.1:{}\"",
                    ssh_port
                ))
            );
        }
        self.ssh_and_wait(vdl_args.tuntap, ssh_port)?;
        self.stop_vdl(&KillCommand {
            launched_proto: Some(self.output_proto.display().to_string()),
            vdl_path: Some(vdl.clone().to_str().unwrap().to_string()),
        })?;
        Ok(())
    }

    /// SSH into the emulator and wait for exit signal.
    fn ssh_and_wait(&self, tuntap: bool, ssh_port: Port) -> Result<()> {
        if tuntap {
            let device_addr = Command::new(&self.host_tools.device_finder)
                .args(&["resolve", "-ipv4=false", "step-atom-yard-juicy"])
                .output()?;
            Command::new("ssh")
                .args(&[
                    "-F",
                    &self.ssh_files.config.to_str().unwrap(),
                    str::from_utf8(&device_addr.stdout).unwrap().trim_end_matches('\n'),
                ])
                .spawn()?
                .wait_with_output()?;
        } else {
            Command::new("ssh")
                .args(&[
                    "-F",
                    &self.ssh_files.config.to_str().unwrap(),
                    "fuchsia@localhost",
                    "-p",
                    &ssh_port.to_string(),
                ])
                .spawn()?
                .wait_with_output()?;
        }
        Ok(())
    }

    pub fn stop_vdl(&self, kill_command: &KillCommand) -> Result<()> {
        let vdl_path = match &kill_command.vdl_path {
            None => String::from(
                read_env_path("PREBUILT_VDL_DIR")?.join("device_launcher").to_str().unwrap(),
            ),
            Some(vdl_path) => vdl_path.to_string(),
        };
        match &kill_command.launched_proto {
            None => {
                Command::new(vdl_path)
                    .arg("--action=kill")
                    .arg("--launched_virtual_device_proto")
                    .arg(&self.output_proto)
                    .status()?;
            }
            Some(proto_location) => {
                Command::new(vdl_path)
                    .arg("--action=kill")
                    .arg("--launched_virtual_device_proto")
                    .arg(proto_location)
                    .status()?;
            }
        }
        Ok(())
    }
}
