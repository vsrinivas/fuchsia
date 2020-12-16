// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::args::StartCommand;
use crate::cipd;
use anyhow::{anyhow, format_err, Result};
use fuchsia_async::Executor;
use home::home_dir;
use hyper::{StatusCode, Uri};
use std::convert::From;
use std::env;
use std::fs::{create_dir, create_dir_all, File};
use std::io::{BufRead, BufReader, Write};
use std::os::unix;
use std::path::PathBuf;

pub fn read_env_path(var: &str) -> Result<PathBuf> {
    env::var_os(var).map(PathBuf::from).ok_or(anyhow!("{} contained invalid Unicode", var))
}

/// Returns either the path specified in the environment variable FUCHSIA_SDK_DATA_DIR or
/// $HOME/.fuchsia
pub fn get_sdk_data_dir() -> Result<PathBuf> {
    let sdk_data_dir = match read_env_path("FUCHSIA_SDK_DATA_DIR") {
        Ok(dir) => dir,
        _ => {
            let default = home_dir().unwrap_or_default().join(".fuchsia");
            if !default.exists() {
                create_dir(&default)?;
            }
            default
        }
    };
    Ok(sdk_data_dir)
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
    /// Initialize host tools for in-tree usage.
    ///
    /// Requires the environment variable HOST_OUT_DIR,
    /// PREBUILT_AEMU_DIR, PREBUILT_GRPCWEBPROXY_DIR, PREBUILT_VDL_DIR to be specified.
    /// See: //tools/devshell/vdl
    pub fn from_tree_env() -> Result<HostTools> {
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

    /// Initialize host tools for GN SDK usage.
    ///
    /// First check the existence of environment variable TOOL_DIR, if not specified
    /// look for host tools in the program's containing directory.
    pub fn from_sdk_env() -> Result<HostTools> {
        let sdk_tool_dir = match read_env_path("TOOL_DIR") {
            Ok(dir) => dir,
            _ => env::args_os()
                .nth(0)
                .map(PathBuf::from)
                .ok_or(anyhow!("Cannot get containing directory path."))?
                .parent()
                .ok_or(anyhow!("Cannot get parent path."))?
                .to_path_buf(),
        };

        Ok(HostTools {
            aemu: PathBuf::new(),
            grpcwebproxy: PathBuf::new(),
            vdl: sdk_tool_dir.join("device_launcher"),
            device_finder: sdk_tool_dir.join("device-finder"),
            far: sdk_tool_dir.join("far"),
            fvm: sdk_tool_dir.join("fvm"),
            pm: sdk_tool_dir.join("pm"),
            zbi: sdk_tool_dir.join("zbi"),
        })
    }

    /// Downloads & extract aemu.zip from CIPD, and returns the path containing the emulator executable.
    ///
    /// # Arguments
    ///
    /// * `label` - cipd label that specified a particular aemu version
    /// * `cipd_pkg` - this is appeneded to cipd url https://chrome-infra-packages.appspot.com/dl/fuchsia/third_party/.
    pub fn download_and_extract(&self, label: String, cipd_pkg: String) -> Result<PathBuf> {
        let mut executor = Executor::new().unwrap();
        executor.run_singlethreaded(async move {
            let root_path = match read_env_path("FEMU_DOWNLOAD_DIR") {
                Ok(path) => path,
                _ => {
                    let default_path = get_sdk_data_dir()?.join("femu");
                    if !default_path.exists() {
                        create_dir_all(&default_path)?;
                    }
                    default_path
                }
            };
            let arch = match env::consts::OS {
                "macos" => "mac-amd64",
                _ => "linux-amd64",
            };
            let url = format!(
                "https://chrome-infra-packages.appspot.com/dl/fuchsia/third_party/{}/{}/+/{}",
                cipd_pkg, arch, label
            )
            .parse::<Uri>()?;
            let aemu_zip = root_path.join(format!("{}.zip", cipd_pkg));
            if aemu_zip.exists() {
                return Ok(root_path);
            }
            let status = cipd::download(url.clone(), &aemu_zip).await?;
            if status == StatusCode::OK {
                cipd::extract_zip(&aemu_zip, &root_path)?;
                Ok(root_path)
            } else {
                Err(format_err!(
                    "Cannot download file from cipd path {}. Got status code {}",
                    url,
                    status.as_str(),
                ))
            }
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
    /// Initialize fuchsia image and package files for in-tree usage.
    ///
    /// Requires the environment variable FUCHSIA_BUILD_DIR to be specified.
    pub fn from_tree_env() -> Result<ImageFiles> {
        let fuchsia_build_dir = read_env_path("FUCHSIA_BUILD_DIR")?;
        Ok(ImageFiles {
            amber_files: fuchsia_build_dir.join("amber-files"),
            build_args: fuchsia_build_dir.join("args.gn"),
            fvm: fuchsia_build_dir.join(read_env_path("IMAGE_FVM_RAW")?),
            kernel: fuchsia_build_dir.join(read_env_path("IMAGE_QEMU_KERNEL_RAW")?),
            zbi: fuchsia_build_dir.join(read_env_path("IMAGE_ZIRCONA_ZBI")?),
        })
    }

    /// Initialize fuchsia image and package files for GN SDK usage.
    ///
    /// First check the existence of environment variable IMAGE_DIR, if not specified
    /// make a best effort guess in other known paths by calling get_sdk_data_dir().
    ///
    /// If --sdk_version is specified, fuchsia image will be downloaded from GCS.
    pub fn from_sdk_env() -> Result<ImageFiles> {
        let fuchsia_build_dir = match read_env_path("IMAGE_DIR") {
            Ok(dir) => dir,
            _ => get_sdk_data_dir()?,
        };
        Ok(ImageFiles {
            amber_files: fuchsia_build_dir.join("amber-files"),
            build_args: fuchsia_build_dir.join("buildargs.gn"),
            fvm: fuchsia_build_dir.join("storage-full.blk"),
            kernel: fuchsia_build_dir.join("femu-kernel.kernel"),
            zbi: fuchsia_build_dir.join("zircon-a.zbi"),
        })
    }

    pub fn stage_files(&mut self, dir: &PathBuf) -> Result<()> {
        let vdl_kernel_dest = dir.join("femu_kernel");
        let vdl_kernel_src = self.kernel.as_path();
        unix::fs::symlink(&vdl_kernel_src, &vdl_kernel_dest)?;
        self.kernel = vdl_kernel_dest.to_path_buf();

        let vdl_fvm_dest = dir.join("femu_fvm");
        let vdl_fvm_src = self.fvm.as_path();
        unix::fs::symlink(&vdl_fvm_src, &vdl_fvm_dest)?;
        self.fvm = vdl_fvm_dest.to_path_buf();

        let vdl_args_dest = dir.join("femu_buildargs");
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

    /// Initialize SSH key files for in-tree usage.
    ///
    /// Requires the environment variable FUCHSIA_DIR & FUCHSIA_BUILD_DIR to be specified.
    pub fn from_tree_env() -> Result<SSHKeys> {
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

    /// Initialize SSH key files for GN SDK usage.
    ///
    /// Requires SSH keys to have been generated and stored in $HOME/.ssh/...
    pub fn from_sdk_env() -> Result<SSHKeys> {
        let keys = SSHKeys {
            auth_key: home_dir().unwrap_or_default().join(".ssh/fuchsia_authorized_keys"),
            config: get_sdk_data_dir()?.join("sshconfig"),
            private_key: home_dir().unwrap_or_default().join(".ssh/fuchsia_ed25519"),
            public_key: home_dir().unwrap_or_default().join(".ssh/fuchsia_ed25519.pub"),
        };
        if !keys.config.exists() {
            let config_content = format!(
                "# Configure port 8022 for connecting to a device with the local address.
# This makes it possible to forward 8022 to a device connected remotely.
# The fuchsia private key is used for the identity.
Host 127.0.0.1
  Port 8022

Host ::1
  Port 8022

Host *
# Turn off refusing to connect to hosts whose key has changed
StrictHostKeyChecking no
CheckHostIP no

# Disable recording the known hosts
UserKnownHostsFile=/dev/null

# Do not forward auth agent connection to remote, no X11
ForwardAgent no
ForwardX11 no

# Connection timeout in seconds
ConnectTimeout=10

# Check for server alive in seconds, max count before disconnecting
ServerAliveInterval 1
ServerAliveCountMax 10

# Try to keep the master connection open to speed reconnecting.
ControlMaster auto
ControlPersist yes

# When expanded, the ControlPath below cannot have more than 90 characters
# (total of 108 minus 18 used by a random suffix added by ssh).
# '%C' expands to 40 chars and there are 9 fixed chars, so '~' can expand to
# up to 41 chars, which is a reasonable limit for a user's home in most
# situations. If '~' expands to more than 41 chars, the ssh connection
# will fail with an error like:
#     unix_listener: path \"...\" too long for Unix domain socket
# A possible solution is to use /tmp instead of ~, but it has
# its own security concerns.
ControlPath=~/.ssh/fx-%C

# Connect with user, use the identity specified.
User fuchsia
IdentitiesOnly yes
IdentityFile {}
GSSAPIDelegateCredentials no
",
                keys.private_key.display()
            );
            File::create(&keys.config)?.write_all(config_content.as_bytes())?;
        }
        Ok(keys)
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

        let vdl_ssh_config_dest = dir.join("ssh_config");
        let vdl_ssh_config_src = self.config.as_path();
        unix::fs::symlink(&vdl_ssh_config_src, &vdl_ssh_config_dest)?;
        self.config = vdl_ssh_config_dest.to_path_buf();

        Ok(())
    }
}

#[derive(Debug)]
pub struct VDLArgs {
    pub headless: bool,
    pub tuntap: bool,
    pub enable_grpcwebproxy: bool,
    pub enable_hidpi_scaling: bool,
    pub grpcwebproxy_port: String,
    pub upscript: String,
    pub packages_to_serve: String,
    pub image_size: String,
    pub device_proto: String,
    pub gpu: String,
    pub pointing_device: String,
    pub gcs_bucket: String,
    pub gcs_build_id: String,
    pub gcs_image_archive: String,
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
            enable_hidpi_scaling: cmd.hidpi_scaling,
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
            gcs_bucket: cmd.gcs_bucket.as_ref().unwrap_or(&String::from("fuchsia")).to_string(),
            gcs_build_id: cmd.sdk_version.as_ref().unwrap_or(&String::from("")).to_string(),
            gcs_image_archive: cmd
                .image_name
                .as_ref()
                .unwrap_or(&String::from("qemu-x64"))
                .to_string(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::read_dir;
    use std::io::Write;
    use tempfile::TempDir;

    #[test]
    fn test_convert_start_cmd_to_vdl() {
        let start_command = &StartCommand {
            headless: false,
            tuntap: true,
            hidpi_scaling: false,
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
            aemu_version: Some("git_revision:da1cc2ee512714a176f08b8b5fec035994ca305d".to_string()),
            gcs_bucket: None,
            grpcwebproxy_version: None,
            sdk_version: Some("0.20201130.3.1".to_string()),
            image_name: Some("qemu-x64".to_string()),
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

        let host_tools = HostTools::from_tree_env()?;
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

        let mut image_files = ImageFiles::from_tree_env()?;
        assert_eq!(image_files.zbi.to_str().unwrap(), "/build/out/zircona");
        assert_eq!(image_files.kernel.to_str().unwrap(), "/build/out/kernel");
        assert_eq!(image_files.fvm.to_str().unwrap(), "/build/out/fvm");
        assert_eq!(image_files.build_args.to_str().unwrap(), "/build/out/args.gn");
        assert_eq!(image_files.amber_files.to_str().unwrap(), "/build/out/amber-files");

        let tmp_dir = TempDir::new()?.into_path();
        image_files.stage_files(&tmp_dir)?;
        assert_eq!(image_files.kernel.to_str(), tmp_dir.join("femu_kernel").to_str());

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
        let mut ssh_files = SSHKeys::from_tree_env()?;
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

    #[test]
    fn test_sdk_data_dir() -> Result<()> {
        let tmp_dir = TempDir::new()?.into_path();
        env::set_var("FUCHSIA_SDK_DATA_DIR", tmp_dir.to_str().unwrap());
        let p = get_sdk_data_dir()?;
        assert_eq!(p.to_str(), tmp_dir.to_str());
        Ok(())
    }

    #[test]
    fn test_download_and_extract() -> Result<()> {
        let tmp_dir = TempDir::new()?.into_path();
        env::set_var("FEMU_DOWNLOAD_DIR", tmp_dir.to_str().unwrap());
        let host_tools = HostTools::from_sdk_env()?;
        // Pick the smallest package I can find.
        host_tools.download_and_extract("latest".to_string(), "ninja".to_string())?;
        let mut has_zip = false;
        let mut has_extract = false;
        for path in read_dir(&tmp_dir)? {
            let entry = path?;
            let p = entry.path();
            println!("Found path {}", p.display());
            if p.ends_with("ninja.zip") {
                has_zip = true;
            }
            if p.ends_with("ninja") {
                has_extract = true;
            }
        }
        assert!(has_zip);
        assert!(has_extract);
        Ok(())
    }
}
