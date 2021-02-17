// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::portpicker::{pick_unused_port, Port};
use crate::types::{get_sdk_data_dir, read_env_path, HostTools, ImageFiles, SSHKeys, VDLArgs};
use ansi_term::Colour::*;
use anyhow::{bail, Result};
use ffx_emulator_args::{KillCommand, StartCommand};
use regex::Regex;
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
            .arg(format!("data/ssh/authorized_keys={}", self.ssh_files.auth_key.display()))
            .status()?;
        if status.success() {
            return Ok(zbi_out);
        } else {
            bail!("Cannot provision zbi. Exit status was {}", status.code().unwrap_or_default())
        }
    }

    fn assemble_system_images(&self, sdk_version: &String) -> Result<String> {
        if sdk_version.is_empty() {
            return Ok(format!(
                "{},{},{},{},{},{},{}",
                self.ssh_files.private_key.display(),
                self.ssh_files.auth_key.display(),
                self.provision_zbi()?.display(),
                self.image_files.kernel.display(),
                self.image_files.fvm.display(),
                self.image_files.build_args.display(),
                self.image_files.amber_files.display(),
            ));
        } else {
            return Ok(format!(
                "{},{}",
                self.ssh_files.private_key.display(),
                self.ssh_files.auth_key.display(),
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

    // Notes for usage in SDK (ex: `fvdl --sdk ...`)
    // If `--aemu-path` is specified, use that.
    // Else If `--aemu-version` is specified, download aemu with that version label from cipd.
    // Else If `<sdk_root>/bin/aemu.version` is present, download aemu using that version from cipd.
    // Else download aemu using version `integration` from cipd.
    //
    // Notes for usage in-tree (ex: `fx vdl start ...`)
    // If `--aemu-path` is specified, use that.
    // Else If `--aemu-version` is specified, download aemu with that version label from cipd.
    // Else If env_var ${PREBUILT_AEMU_DIR} is set (in-tree default), use that.
    // Else download aemu using version `integration` from cipd.
    pub fn resolve_aemu_path(&self, start_command: &StartCommand) -> Result<PathBuf> {
        match &start_command.aemu_path {
            Some(aemu_path) => Ok(PathBuf::from(aemu_path)),
            None => {
                let aemu_cipd_version = match &start_command.aemu_version {
                    Some(version) => version.clone(),
                    None => {
                        if self.host_tools.aemu.as_os_str().is_empty() {
                            self.host_tools
                                .read_prebuild_version("aemu.version")
                                .unwrap_or(String::from("integration"))
                        } else {
                            return Ok(self.host_tools.aemu.clone());
                        }
                    }
                };
                Ok(self
                    .host_tools
                    .download_and_extract(
                        aemu_cipd_version.to_string(),
                        "third_party/aemu".to_string(),
                    )?
                    .join("emulator"))
            }
        }
    }

    pub fn resolve_grpcwebproxy_path(&self, start_command: &StartCommand) -> Result<PathBuf> {
        match &start_command.grpcwebproxy_path {
            Some(grpcwebproxy_path) => Ok(PathBuf::from(grpcwebproxy_path)),
            None => {
                let grpcwebproxy_cipd_version = match &start_command.grpcwebproxy_version {
                    Some(version) => version.clone(),
                    None => {
                        if self.host_tools.grpcwebproxy.as_os_str().is_empty() {
                            self.host_tools
                                .read_prebuild_version("grpcwebproxy.version")
                                .unwrap_or(String::from("latest"))
                        } else {
                            return Ok(self.host_tools.grpcwebproxy.clone());
                        }
                    }
                };
                Ok(self
                    .host_tools
                    .download_and_extract(
                        grpcwebproxy_cipd_version.to_string(),
                        "third_party/grpcwebproxy".to_string(),
                    )?
                    .join("grpcwebproxy"))
            }
        }
    }

    pub fn resolve_vdl_path(&self, start_command: &StartCommand) -> Result<PathBuf> {
        match &start_command.vdl_path {
            Some(vdl_path) => Ok(PathBuf::from(vdl_path)),
            None => {
                let vdl_cipd_version = match &start_command.vdl_version {
                    Some(version) => version.clone(),
                    None => {
                        if self.host_tools.vdl.as_os_str().is_empty() {
                            self.host_tools
                                .read_prebuild_version("device_launcher.version")
                                .unwrap_or(String::from("latest"))
                        } else {
                            return Ok(self.host_tools.vdl.clone());
                        }
                    }
                };
                Ok(self
                    .host_tools
                    .download_and_extract(vdl_cipd_version.to_string(), "vdl".to_string())?
                    .join("device_launcher"))
            }
        }
    }

    // Checks if user has specified a portmap. If portmap is specified, we'll check if ssh port is included.
    // If ssh port is not included, we'll pick a port and forward that together with the rest of portmap.
    pub fn resolve_portmap(&self, start_command: &StartCommand) -> (String, u16) {
        let mut ssh_port = 0;
        match &start_command.port_map {
            Some(port_map) => {
                let re = Regex::new(r"::(?P<ssh>\d+)-:22(,|$)").unwrap();
                re.captures(port_map).and_then(|cap| {
                    cap.name("ssh").map(|ssh| ssh_port = ssh.as_str().parse::<u16>().unwrap())
                });
                if ssh_port == 0 {
                    ssh_port = pick_unused_port().unwrap();
                    (format!("{},hostfwd=tcp::{}-:22", port_map.clone(), ssh_port), ssh_port)
                } else {
                    (port_map.clone(), ssh_port)
                }
            }
            None => {
                ssh_port = pick_unused_port().unwrap();
                (format!("hostfwd=tcp::{}-:22", ssh_port), ssh_port)
            }
        }
    }

    pub fn check_start_command(&self, command: &StartCommand) -> Result<()> {
        if command.nointeractive && command.vdl_output.is_none() {
            bail!(
                "--vdl-ouput must be specified for --nointeractive mode.\n\
                example: fx vdl start --nointeractive --vdl-output /path/to/saved/output.log\n\
                example: ./fvdl --sdk start --nointeractive --vdl-output /path/to/saved/output.log\n"
            )
        }
        Ok(())
    }

    /// Launches FEMU, opens an SSH session, and waits for the FEMU instance or SSH session to exit.
    pub fn start_emulator(&mut self, start_command: &StartCommand) -> Result<()> {
        self.check_start_command(&start_command)?;

        let vdl_args: VDLArgs = start_command.clone().into();

        let mut gcs_image = vdl_args.gcs_image_archive;
        let mut gcs_bucket = vdl_args.gcs_bucket;
        let mut sdk_version = vdl_args.sdk_version;

        // If cached images already exist, skip download by clearing out gcs related flags.
        if vdl_args.cache_root.to_str().unwrap_or("") != "" {
            self.image_files.update_paths_from_cache(&vdl_args.cache_root);
            if self.image_files.images_exist() {
                println!("[fvdl] using cached image files");
                gcs_image = String::from("");
                gcs_bucket = String::from("");
                sdk_version = String::from("");
            }
        }

        let fvd = match &start_command.device_proto {
            Some(proto) => PathBuf::from(proto),
            None => self.generate_fvd(&start_command.window_width, &start_command.window_height)?,
        };

        let aemu = self.resolve_aemu_path(start_command)?;
        if !aemu.exists() || !aemu.is_file() {
            bail!("Invalid 'emulator' binary at path {}", aemu.display())
        }

        let vdl = self.resolve_vdl_path(start_command)?;
        if !vdl.exists() || !vdl.is_file() {
            bail!("device_launcher binary cannot be found at {}", vdl.display())
        }

        let mut grpcwebproxy = self.host_tools.grpcwebproxy.clone();
        if vdl_args.enable_grpcwebproxy {
            grpcwebproxy = self.resolve_grpcwebproxy_path(start_command)?;
            if !grpcwebproxy.exists() || !grpcwebproxy.is_file() {
                bail!("grpcwebproxy binary cannot be found at {}", grpcwebproxy.display())
            }
        }

        let emu_log = match &start_command.emulator_log {
            Some(location) => PathBuf::from(location),
            None => self.emulator_log.clone(),
        };

        let vdl_output = match &start_command.vdl_output {
            Some(location) => PathBuf::from(location),
            None => self.output_proto.clone(),
        };

        let (port_map, ssh_port) = self.resolve_portmap(&start_command);

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
            .arg(format!("--system_images={}", self.assemble_system_images(&sdk_version)?))
            .arg("--host_port_map")
            .arg(&port_map)
            .arg("--output_launched_device_proto")
            .arg(&vdl_output)
            .arg("--emu_log")
            .arg(&emu_log)
            .arg("--proto_file_path")
            .arg(&fvd)
            .arg("--audio=true")
            .arg(format!("--debugger={}", &start_command.debugger))
            .arg(format!("--resize_fvm={}", vdl_args.image_size))
            .arg(format!("--gpu={}", vdl_args.gpu))
            .arg(format!("--headless_mode={}", vdl_args.headless))
            .arg(format!("--tuntap={}", vdl_args.tuntap))
            .arg(format!("--upscript={}", vdl_args.upscript))
            .arg(format!("--serve_packages={}", vdl_args.packages_to_serve))
            .arg(format!("--pointing_device={}", vdl_args.pointing_device))
            .arg(format!("--enable_webrtc={}", vdl_args.enable_grpcwebproxy))
            .arg(format!("--grpcwebproxy_port={}", vdl_args.grpcwebproxy_port))
            .arg(format!("--gcs_bucket={}", gcs_bucket))
            .arg(format!("--image_archive={}", gcs_image))
            .arg(format!("--build_id={}", sdk_version))
            .arg(format!("--enable_emu_controller={}", enable_emu_controller))
            .arg(format!("--hidpi_scaling={}", vdl_args.enable_hidpi_scaling))
            .arg(format!("--image_cache_path={}", vdl_args.cache_root.display()))
            .arg(format!("--kernel_args={}", vdl_args.extra_kerel_args))
            .status()?;
        if !status.success() {
            let persistent_emu_log = read_env_path("FUCHSIA_OUT_DIR")
                .unwrap_or(env::current_dir()?)
                .join("emu_crash.log");
            copy(&self.emulator_log, &persistent_emu_log)?;
            bail!(
                "Cannot start Fuchsia Emulator. Exit status is {}\nEmulator log is copied to {}",
                status.code().unwrap_or_default(),
                persistent_emu_log.display()
            )
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
        if start_command.nointeractive {
            println!(
                    "{}",
                    Yellow.paint(format!(
                    "\nNOTE: For --noninteractive launcher artifacts need to be manually cleaned using the `kill` subcommand: \n\
                    In Fuchsia Repo: \"fx vdl kill --launched-proto {}\"\n\
                    In SDK: \"./fvdl --sdk kill --launched-proto {}\"", vdl_output.display(), vdl_output.display()
                    ))
                );
        } else {
            self.ssh_and_wait(vdl_args.tuntap, ssh_port)?;
            self.stop_vdl(&KillCommand {
                launched_proto: Some(vdl_output.display().to_string()),
                vdl_path: Some(vdl.display().to_string()),
            })?;
        }
        Ok(())
    }

    /// SSH into the emulator and wait for exit signal.
    fn ssh_and_wait(&self, tuntap: bool, ssh_port: Port) -> Result<()> {
        if tuntap {
            let device_addr = Command::new(&self.host_tools.device_finder)
                .args(&["resolve", "-ipv4=false", "step-atom-yard-juicy"])
                .output()?;
            // Ref to SSH flags: http://man.openbsd.org/ssh_config
            Command::new("ssh")
                .args(&[
                    "-o",
                    "StrictHostKeyChecking=no",
                    "-o",
                    "CheckHostIP=no",
                    "-o",
                    "UserKnownHostsFile=/dev/null",
                    "-o",
                    "ConnectTimeout=10",
                    "-o",
                    "ServerAliveInterval=1",
                    "-o",
                    "ServerAliveCountMax=5",
                    "-i",
                    &self.ssh_files.private_key.to_str().unwrap(),
                    str::from_utf8(&device_addr.stdout).unwrap().trim_end_matches('\n'),
                ])
                .spawn()?
                .wait_with_output()?;
        } else {
            Command::new("ssh")
                .args(&[
                    "-o",
                    "StrictHostKeyChecking=no",
                    "-o",
                    "CheckHostIP=no",
                    "-o",
                    "UserKnownHostsFile=/dev/null",
                    "-i",
                    &self.ssh_files.private_key.to_str().unwrap(),
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
        let vdl: PathBuf = match &kill_command.vdl_path {
            Some(vdl_path) => PathBuf::from(vdl_path),
            None => match read_env_path("PREBUILT_VDL_DIR") {
                Ok(default_path) => default_path.join("device_launcher"),
                _ => {
                    let label = self
                        .host_tools
                        .read_prebuild_version("device_launcher.version")
                        .unwrap_or(String::from("latest"));
                    get_sdk_data_dir()?
                        .join("femu")
                        .join(format!("vdl-{}", label.replace(":", "-")))
                        .join("device_launcher")
                }
            },
        };
        if !vdl.exists() || !vdl.is_file() {
            bail!("device_launcher binary cannot be found at {}", vdl.display())
        }
        match &kill_command.launched_proto {
            None => {
                bail!(
                    "--launched-proto must be specified for `kill` subcommand.\n\
                    example: \"fx vdl kill --launched-proto /path/to/saved/output.log\"\n\
                    example: \"./fvdl --sdk kill --launched-proto /path/to/saved/output.log\"\n"
                )
            }
            Some(proto_location) => {
                Command::new(&vdl)
                    .arg("--action=kill")
                    .arg("--launched_virtual_device_proto")
                    .arg(proto_location)
                    .status()?;
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serial_test::serial;
    use std::io::Write;

    pub fn setup() {
        env::set_var("HOST_OUT_DIR", "/host/out");
        env::set_var("FUCHSIA_BUILD_DIR", "/build/out");
        env::set_var("IMAGE_FVM_RAW", "fvm");
        env::set_var("IMAGE_QEMU_KERNEL_RAW", "kernel");
        env::set_var("IMAGE_ZIRCONA_ZBI", "zircona");
        env::remove_var("PREBUILT_AEMU_DIR");
        env::remove_var("PREBUILT_VDL_DIR");
        env::remove_var("PREBUILT_GRPCWEBPROXY_DIR");
    }

    pub fn create_fake_ssh() -> Result<PathBuf> {
        let data = format!(
            "/usr/local/home/foo/.ssh/fuchsia_ed25519
/usr/local/home/foo/.ssh/fuchsia_authorized_keys
",
        );
        let tmp_dir = Builder::new().prefix("fvdl_fake_ssh_").tempdir()?;
        File::create(tmp_dir.path().join(".fx-ssh-path"))?.write_all(data.as_bytes())?;
        Ok(tmp_dir.into_path())
    }

    pub fn create_start_command() -> StartCommand {
        StartCommand {
            headless: false,
            tuntap: true,
            hidpi_scaling: false,
            upscript: Some("/path/to/upscript".to_string()),
            packages_to_serve: Some("pkg1.far,pkg2.far".to_string()),
            image_size: None,
            device_proto: None,
            aemu_path: Some("/path/to/aemu".to_string()),
            vdl_path: Some("/path/to/device_launcher".to_string()),
            host_gpu: true,
            software_gpu: false,
            window_width: 1280,
            window_height: 800,
            grpcwebproxy: None,
            grpcwebproxy_path: Some("/path/to/grpcwebproxy".to_string()),
            pointing_device: Some("mouse".to_string()),
            aemu_version: Some("git_revision:da1cc2ee512714a176f08b8b5fec035994ca305d".to_string()),
            gcs_bucket: None,
            grpcwebproxy_version: Some("git_revision:1".to_string()),
            sdk_version: Some("0.20201130.3.1".to_string()),
            image_name: Some("qemu-x64".to_string()),
            vdl_version: Some("git_revision:2".to_string()),
            emulator_log: None,
            port_map: None,
            vdl_output: None,
            nointeractive: false,
            cache_image: false,
            debugger: false,
            kernel_args: None,
        }
    }

    #[test]
    #[serial]
    fn test_choosing_prebuild_with_path_specified() -> Result<()> {
        setup();
        // hold on to the temp dir created in create_fake_ssh(), so it does not get deleted.
        env::set_var("FUCHSIA_DIR", create_fake_ssh()?);

        let start_command = &create_start_command();

        // --sdk
        let mut aemu = VDLFiles::new(true)?.resolve_aemu_path(start_command)?;
        assert_eq!(PathBuf::from("/path/to/aemu"), aemu);
        let mut vdl = VDLFiles::new(true)?.resolve_vdl_path(start_command)?;
        assert_eq!(PathBuf::from("/path/to/device_launcher"), vdl);
        let mut grpcwebproxy = VDLFiles::new(true)?.resolve_grpcwebproxy_path(start_command)?;
        assert_eq!(PathBuf::from("/path/to/grpcwebproxy"), grpcwebproxy);

        // in-tree
        aemu = VDLFiles::new(false)?.resolve_aemu_path(start_command)?;
        assert_eq!(PathBuf::from("/path/to/aemu"), aemu);
        vdl = VDLFiles::new(false)?.resolve_vdl_path(start_command)?;
        assert_eq!(PathBuf::from("/path/to/device_launcher"), vdl);
        grpcwebproxy = VDLFiles::new(false)?.resolve_grpcwebproxy_path(start_command)?;
        assert_eq!(PathBuf::from("/path/to/grpcwebproxy"), grpcwebproxy);
        Ok(())
    }

    #[test]
    #[serial]
    fn test_choosing_prebuild_with_cipd_label_specified() -> Result<()> {
        setup();
        // hold on to the temp dir created in create_fake_ssh(), so it does not get deleted.
        env::set_var("FUCHSIA_DIR", create_fake_ssh()?);

        let tmp_dir = Builder::new().prefix("fvdl_test_cipd_label_").tempdir()?;
        env::set_var("FEMU_DOWNLOAD_DIR", tmp_dir.path());
        env::set_var("PREBUILT_VDL_DIR", "/host/out/vdl");
        env::set_var("PREBUILT_AEMU_DIR", "/host/out/aemu");
        env::set_var("PREBUILT_GRPCWEBPROXY_DIR", "/host/out/grpcwebproxy");

        let mut start_command = &mut create_start_command();
        start_command.vdl_path = None;
        start_command.vdl_version = Some("g3-revision:vdl_fuchsia_20210113_RC00".to_string());

        // --sdk
        let mut vdl = VDLFiles::new(true)?.resolve_vdl_path(start_command)?;
        assert_eq!(
            tmp_dir.path().join("vdl-g3-revision-vdl_fuchsia_20210113_RC00/device_launcher"),
            vdl
        );

        // in-tree
        vdl = VDLFiles::new(false)?.resolve_vdl_path(start_command)?;
        assert_eq!(
            tmp_dir.path().join("vdl-g3-revision-vdl_fuchsia_20210113_RC00/device_launcher"),
            vdl
        );
        Ok(())
    }

    #[test]
    #[serial]
    fn test_choosing_prebuild_default() -> Result<()> {
        setup();
        // hold on to the temp dir created in create_fake_ssh(), so it does not get deleted.
        env::set_var("FUCHSIA_DIR", create_fake_ssh()?);

        let tmp_dir = Builder::new().prefix("fvdl_test_default_").tempdir()?;
        env::set_var("FEMU_DOWNLOAD_DIR", tmp_dir.path());
        env::set_var("PREBUILT_VDL_DIR", "/host/out/vdl");
        env::set_var("PREBUILT_AEMU_DIR", "/host/out/aemu");
        env::set_var("PREBUILT_GRPCWEBPROXY_DIR", "/host/out/grpcwebproxy");

        let mut start_command = &mut create_start_command();
        start_command.aemu_path = None;
        start_command.aemu_version = None;
        start_command.vdl_path = None;
        start_command.vdl_version = None;
        start_command.grpcwebproxy_path = None;
        start_command.grpcwebproxy_version = None;

        // --sdk
        let mut vdl = VDLFiles::new(true)?.resolve_vdl_path(start_command)?;
        assert_eq!(tmp_dir.path().join("vdl-latest/device_launcher"), vdl);

        // in-tree
        vdl = VDLFiles::new(false)?.resolve_vdl_path(start_command)?;
        assert_eq!(PathBuf::from("/host/out/vdl/device_launcher"), vdl);
        let aemu = VDLFiles::new(false)?.resolve_aemu_path(start_command)?;
        assert_eq!(PathBuf::from("/host/out/aemu/emulator"), aemu);
        let grpcwebproxy = VDLFiles::new(false)?.resolve_grpcwebproxy_path(start_command)?;
        assert_eq!(PathBuf::from("/host/out/grpcwebproxy/grpcwebproxy"), grpcwebproxy);
        Ok(())
    }

    #[test]
    #[serial]
    fn test_resolve_portmap() -> Result<()> {
        setup();

        let mut start_command = &mut create_start_command();
        start_command.port_map = None;
        let (port_map, ssh) = VDLFiles::new(true)?.resolve_portmap(start_command);
        assert!(ssh > 0);
        let re = Regex::new(r"hostfwd=tcp::\d+-:22").unwrap();
        assert!(re.is_match(&port_map));

        start_command.port_map = Some("".to_string());
        let (port_map, ssh) = VDLFiles::new(true)?.resolve_portmap(start_command);
        assert!(ssh > 0);
        let re = Regex::new(r"hostfwd=tcp::\d+-:22").unwrap();
        assert!(re.is_match(&port_map));

        start_command.port_map = Some("hostfwd=tcp::123-:222,hostfwd=tcp::80-:223".to_string());
        let (port_map, ssh) = VDLFiles::new(true)?.resolve_portmap(start_command);
        assert!(ssh > 0);
        let re =
            Regex::new(r"hostfwd=tcp::123-:222,hostfwd=tcp::80-:223,hostfwd=tcp::\d+-:22").unwrap();
        assert!(re.is_match(&port_map));

        start_command.port_map =
            Some("hostfwd=tcp::123-:223,hostfwd=tcp::80-:322,hostfwd=tcp::456-:22".to_string());
        let (port_map, ssh) = VDLFiles::new(true)?.resolve_portmap(start_command);
        assert_eq!(456, ssh);
        assert_eq!("hostfwd=tcp::123-:223,hostfwd=tcp::80-:322,hostfwd=tcp::456-:22", port_map);

        start_command.port_map = Some("hostfwd=tcp::789-:22".to_string());
        let (port_map, ssh) = VDLFiles::new(true)?.resolve_portmap(start_command);
        assert_eq!(789, ssh);
        assert_eq!("hostfwd=tcp::789-:22", port_map);

        start_command.port_map =
            Some("hostfwd=tcp::123-:22,hostfwd=tcp::80-:8022,hostfwd=tcp::456-:222".to_string());
        let (port_map, ssh) = VDLFiles::new(true)?.resolve_portmap(start_command);
        assert_eq!(123, ssh);
        assert_eq!("hostfwd=tcp::123-:22,hostfwd=tcp::80-:8022,hostfwd=tcp::456-:222", port_map);
        Ok(())
    }
}
