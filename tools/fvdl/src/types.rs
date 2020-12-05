// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::args::StartCommand;
use anyhow::{anyhow, Result};
use std::convert::From;
use std::env;
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::os::unix;
use std::path::PathBuf;

pub fn read_env_path(var: &str) -> Result<PathBuf> {
    env::var_os(var).map(PathBuf::from).ok_or(anyhow!("{} contained invalid Unicode", var))
}

pub struct HostTools {
    pub aemu: PathBuf,
    pub device_finder: PathBuf,
    pub far: PathBuf,
    pub fvm: PathBuf,
    pub grpcwebproxy: PathBuf,
    pub pm: PathBuf,
    pub vdl: PathBuf,
    pub zbi: PathBuf,
}

impl HostTools {
    pub fn from_env() -> Result<HostTools> {
        let host_out_dir = read_env_path("HOST_OUT_DIR")?;
        Ok(HostTools {
            aemu: read_env_path("PREBUILT_AEMU_DIR")?.join("emulator"),
            device_finder: host_out_dir.join("device-finder"),
            far: host_out_dir.join("far"),
            fvm: host_out_dir.join("fvm"),
            grpcwebproxy: read_env_path("PREBUILT_GRPCWEBPROXY_DIR")?.join("grpcwebproxy"),
            pm: host_out_dir.join("pm"),
            vdl: read_env_path("PREBUILT_VDL_DIR")?.join("device_launcher"),
            zbi: host_out_dir.join("zbi"),
        })
    }
}

pub struct ImageFiles {
    pub amber_files: PathBuf,
    pub build_args: PathBuf,
    pub fvm: PathBuf,
    pub kernel: PathBuf,
    pub zbi: PathBuf,
}

impl ImageFiles {
    pub fn from_env() -> Result<ImageFiles> {
        let fuchsia_build_dir = read_env_path("FUCHSIA_BUILD_DIR")?;
        Ok(ImageFiles {
            amber_files: fuchsia_build_dir.join("amber-files"),
            build_args: fuchsia_build_dir.join("args.gn"),
            fvm: fuchsia_build_dir.join(read_env_path("IMAGE_FVM_RAW")?),
            kernel: fuchsia_build_dir.join(read_env_path("IMAGE_QEMU_KERNEL_RAW")?),
            zbi: fuchsia_build_dir.join(read_env_path("IMAGE_ZIRCONA_ZBI")?),
        })
    }
    pub fn stage_files(&mut self, dir: &PathBuf) -> Result<()> {
        let vdl_kernel_dest = dir.join("qemu_kernel");
        let vdl_kernel_src = self.kernel.as_path();
        unix::fs::symlink(&vdl_kernel_src, &vdl_kernel_dest)?;
        self.kernel = vdl_kernel_dest.to_path_buf();

        let vdl_fvm_dest = dir.join("qemu_fvm");
        let vdl_fvm_src = self.fvm.as_path();
        unix::fs::symlink(&vdl_fvm_src, &vdl_fvm_dest)?;
        self.fvm = vdl_fvm_dest.to_path_buf();

        let vdl_args_dest = dir.join("qemu_buildargs");
        let vdl_args_src = self.build_args.as_path();
        unix::fs::symlink(&vdl_args_src, &vdl_args_dest)?;
        self.build_args = vdl_args_dest.to_path_buf();
        Ok(())
    }
}

pub struct SSHKeys {
    pub auth_key: PathBuf,
    pub config: PathBuf,
    pub private_key: PathBuf,
    pub public_key: PathBuf,
}

impl SSHKeys {
    #[allow(dead_code)]
    pub fn print(&self) {
        println!("private_key {:?}", self.private_key);
        println!("public_key {:?}", self.public_key);
        println!("auth_key {:?}", self.auth_key);
        println!("config {:?}", self.config);
    }

    pub fn from_env() -> Result<SSHKeys> {
        let ssh_file = File::open(read_env_path("FUCHSIA_DIR")?.join(".fx-ssh-path"))?;
        let ssh_file = BufReader::new(ssh_file);
        let mut lines = ssh_file.lines();

        let private_key = PathBuf::from(lines.next().unwrap()?);
        let auth_key = PathBuf::from(lines.next().unwrap()?);
        let mut pkey = private_key.clone();
        pkey.set_extension("pub");
        let public_key = pkey.to_path_buf();

        Ok(SSHKeys {
            auth_key: auth_key,
            config: read_env_path("FUCHSIA_BUILD_DIR")?.join("ssh-keys/ssh_config"),
            private_key: private_key,
            public_key: public_key,
        })
    }
    pub fn stage_files(&mut self, dir: &PathBuf) -> Result<()> {
        let vdl_priv_key_dest = dir.join("id_ed25519");
        let vdl_priv_key_src = self.private_key.as_path();
        unix::fs::symlink(&vdl_priv_key_src, &vdl_priv_key_dest)?;
        self.private_key = vdl_priv_key_dest.to_path_buf();

        let vdl_auth_key_dest = dir.join("id_ed25519.pub");
        let vdl_auth_key_src = self.public_key.as_path();
        unix::fs::symlink(&vdl_auth_key_src, &vdl_auth_key_dest)?;
        self.public_key = vdl_auth_key_dest.to_path_buf();
        Ok(())
    }
}

#[derive(Debug)]
pub struct VDLArgs {
    pub headless: bool,
    pub tuntap: bool,
    pub enable_grpcwebproxy: bool,
    pub grpcwebproxy_port: String,
    pub upscript: String,
    pub packages_to_serve: String,
    pub image_size: String,
    pub device_proto: String,
    pub gpu: String,
    pub pointing_device: String,
}

impl From<&StartCommand> for VDLArgs {
    fn from(cmd: &StartCommand) -> Self {
        let mut gpu = "swiftshader_indirect";
        if cmd.host_gpu {
            gpu = "host";
        }
        if cmd.software_gpu {
            gpu = "swiftshader_indirect";
        }
        let mut enable_grpcwebproxy = false;
        let mut grpcwebproxy_port = "0".to_string();

        match cmd.grpcwebproxy {
            Some(port) => {
                enable_grpcwebproxy = true;
                grpcwebproxy_port = format!("{}", port);
            }
            _ => (),
        }
        VDLArgs {
            headless: cmd.headless,
            tuntap: cmd.tuntap,
            upscript: cmd.upscript.as_ref().unwrap_or(&String::from("")).to_string(),
            packages_to_serve: cmd
                .packages_to_serve
                .as_ref()
                .unwrap_or(&String::from(""))
                .to_string(),
            image_size: cmd.image_size.as_ref().unwrap_or(&String::from("2G")).to_string(),
            device_proto: cmd.device_proto.as_ref().unwrap_or(&String::from("")).to_string(),
            gpu: gpu.to_string(),
            pointing_device: cmd
                .pointing_device
                .as_ref()
                .unwrap_or(&String::from("touch"))
                .to_string(),
            enable_grpcwebproxy: enable_grpcwebproxy,
            grpcwebproxy_port: grpcwebproxy_port,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::TempDir;

    #[test]
    fn test_convert_start_cmd_to_vdl() {
        let start_command = &StartCommand {
            headless: false,
            tuntap: true,
            upscript: Some("/path/to/upscript".to_string()),
            packages_to_serve: Some("pkg1.far,pkg2.far".to_string()),
            image_size: None,
            device_proto: None,
            aemu_path: None,
            vdl_path: None,
            host_gpu: true,
            software_gpu: false,
            window_width: 1280,
            window_height: 800,
            grpcwebproxy: None,
            grpcwebproxy_path: None,
            pointing_device: Some("mouse".to_string()),
        };
        let vdl_args: VDLArgs = start_command.into();
        assert_eq!(vdl_args.headless, false);
        assert_eq!(vdl_args.tuntap, true);
        assert_eq!(vdl_args.upscript, "/path/to/upscript");
        assert_eq!(vdl_args.packages_to_serve, "pkg1.far,pkg2.far");
        assert_eq!(vdl_args.image_size, "2G");
        assert_eq!(vdl_args.device_proto, "");
        assert_eq!(vdl_args.gpu, "host");
    }

    #[test]
    fn test_host_tools() -> Result<()> {
        env::set_var("HOST_OUT_DIR", "/host/out");
        env::set_var("PREBUILT_AEMU_DIR", "/host/out/aemu");
        env::set_var("PREBUILT_VDL_DIR", "/host/out/vdl");
        env::set_var("PREBUILT_GRPCWEBPROXY_DIR", "/host/out/grpcwebproxy");
        let host_tools = HostTools::from_env()?;
        assert_eq!(host_tools.aemu.to_str().unwrap(), "/host/out/aemu/emulator");
        assert_eq!(host_tools.vdl.to_str().unwrap(), "/host/out/vdl/device_launcher");
        assert_eq!(host_tools.far.to_str().unwrap(), "/host/out/far");
        assert_eq!(host_tools.fvm.to_str().unwrap(), "/host/out/fvm");
        assert_eq!(host_tools.pm.to_str().unwrap(), "/host/out/pm");
        assert_eq!(host_tools.device_finder.to_str().unwrap(), "/host/out/device-finder");
        assert_eq!(host_tools.zbi.to_str().unwrap(), "/host/out/zbi");
        Ok(())
    }

    #[test]
    fn test_image_files() -> Result<()> {
        env::set_var("FUCHSIA_BUILD_DIR", "/build/out");
        env::set_var("IMAGE_ZIRCONA_ZBI", "zircona");
        env::set_var("IMAGE_QEMU_KERNEL_RAW", "kernel");
        env::set_var("IMAGE_FVM_RAW", "fvm");

        let mut image_files = ImageFiles::from_env()?;
        assert_eq!(image_files.zbi.to_str().unwrap(), "/build/out/zircona");
        assert_eq!(image_files.kernel.to_str().unwrap(), "/build/out/kernel");
        assert_eq!(image_files.fvm.to_str().unwrap(), "/build/out/fvm");
        assert_eq!(image_files.build_args.to_str().unwrap(), "/build/out/args.gn");
        assert_eq!(image_files.amber_files.to_str().unwrap(), "/build/out/amber-files");

        let tmp_dir = TempDir::new()?.into_path();
        image_files.stage_files(&tmp_dir)?;
        assert_eq!(image_files.kernel.to_str(), tmp_dir.join("qemu_kernel").to_str());

        Ok(())
    }

    #[test]
    fn test_ssh_files() -> Result<()> {
        let data = format!(
            "/usr/local/home/foo/.ssh/fuchsia_ed25519
/usr/local/home/foo/.ssh/fuchsia_authorized_keys
",
        );
        let tmp_dir = TempDir::new()?.into_path();
        File::create(tmp_dir.join(".fx-ssh-path"))?.write_all(data.as_bytes())?;

        env::set_var("FUCHSIA_DIR", tmp_dir.to_str().unwrap());
        env::set_var("FUCHSIA_BUILD_DIR", "/build/out");
        let mut ssh_files = SSHKeys::from_env()?;
        assert_eq!(
            ssh_files.private_key.to_str().unwrap(),
            "/usr/local/home/foo/.ssh/fuchsia_ed25519"
        );
        assert_eq!(
            ssh_files.public_key.to_str().unwrap(),
            "/usr/local/home/foo/.ssh/fuchsia_ed25519.pub"
        );
        assert_eq!(
            ssh_files.auth_key.to_str().unwrap(),
            "/usr/local/home/foo/.ssh/fuchsia_authorized_keys"
        );
        ssh_files.stage_files(&tmp_dir)?;
        assert_eq!(ssh_files.private_key.to_str(), tmp_dir.join("id_ed25519").to_str());
        Ok(())
    }
}
