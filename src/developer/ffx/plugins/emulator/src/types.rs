// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::cipd;
use crate::graphic_utils::get_default_graphics;
use ansi_term::Colour::*;
use anyhow::{anyhow, format_err, Result};
use ffx_config::sdk::{Sdk, SdkVersion};
use ffx_core::ffx_bail;
use ffx_emulator_args::StartCommand;
use fuchsia_async::Executor;
use home::home_dir;
use hyper::{StatusCode, Uri};
use mockall::automock;
use std::convert::From;
use std::env;
use std::fs::{
    create_dir, create_dir_all, read_dir, read_to_string, remove_dir_all, remove_file, File,
};
use std::io::{BufRead, BufReader};
use std::os::unix;
use std::path::PathBuf;
use walkdir::WalkDir;

pub fn read_env_path(var: &str) -> Result<PathBuf> {
    env::var_os(var)
        .map(PathBuf::from)
        .ok_or(anyhow!("{} is not a valid environment variable", var))
}

#[automock]
pub trait FuchsiaPaths {
    fn find_fuchsia_root(&self) -> Result<PathBuf>;
}
pub struct InTreePaths {}

impl FuchsiaPaths for InTreePaths {
    /// Walks the current execution path and its parent directories to find the path
    /// that contains .jiri_manifest directory.
    fn find_fuchsia_root(&self) -> Result<PathBuf> {
        for ancester in std::env::current_exe()?.ancestors() {
            if let Ok(entries) = read_dir(ancester) {
                for entry in entries {
                    if let Ok(entry) = entry {
                        if entry.path().ends_with(".jiri_manifest") {
                            return Ok(ancester.to_path_buf());
                        }
                    }
                }
            }
        }
        ffx_bail!(
            "Cannot find fuchsia repo root from current execution location and its parent directories"
        );
    }
}

/// Returns GN SDK tools directory. This assumes that fvdl is located in
/// <sdk_root>/tools/[x64|arm64]/fvdl, and will return path to <sdk_root>/tools/[x64|arm64]
pub fn get_fuchsia_sdk_tools_dir() -> Result<PathBuf> {
    Ok(std::env::current_exe()?
        .parent()
        .ok_or(anyhow!("Cannot get parent path to 'fvdl'."))?
        .to_path_buf())
}

/// Returns GN SDK tools directory. This assumes that fvdl is located in
/// <sdk_root>/tools/[x64|arm64]/fvdl, and will return path to <sdk_root>
pub fn get_fuchsia_sdk_dir() -> Result<PathBuf> {
    Ok(get_fuchsia_sdk_tools_dir()? // ex: <sdk_root>/tools/x64/
        .parent() // ex: <sdk_root>/tools/
        .ok_or(anyhow!("Cannot get parent path to 'tools' directory."))?
        .parent() // ex: <sdk_root>
        .ok_or(anyhow!("Cannot get path to sdk root."))?
        .to_path_buf())
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

/// Reads sdk version from manifest.json.
/// This method assumes that user is invoking the binary from GN SDK, not in fuchsia repo.
/// TODO(fxb/69689) Use ffx::config to obtain host_tools location.
pub fn get_sdk_version_from_manifest() -> Result<String> {
    let sdk = Sdk::from_sdk_dir(get_fuchsia_sdk_dir()?)?;
    match sdk.get_version() {
        SdkVersion::Version(v) => Ok(v.to_string()),
        SdkVersion::InTree => ffx_bail!("This should only be used in SDK"),
        SdkVersion::Unknown => ffx_bail!("Cannot determine SDK version"),
    }
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
    pub is_sdk: bool,
}

impl HostTools {
    /// Initialize host tools for in-tree usage via fx vdl.
    ///
    /// Requires the environment variable HOST_OUT_DIR to be specified
    /// PREBUILT_AEMU_DIR, PREBUILT_GRPCWEBPROXY_DIR, PREBUILT_VDL_DIR are optional.
    /// See: //tools/devshell/vdl
    pub fn from_tree_env(f: &impl FuchsiaPaths) -> Result<HostTools> {
        let host_out_dir = read_env_path("HOST_OUT_DIR")?;
        Ok(HostTools {
            // prebuilt binaries that can be optionally fetched from cipd.
            aemu: match read_env_path("PREBUILT_AEMU_DIR") {
                Ok(val) => val.join("emulator"),
                _ => {
                    let fuchsia_root = f.find_fuchsia_root()?;
                    WalkDir::new(fuchsia_root.join("prebuilt/third_party/aemu"))
                        .into_iter()
                        .filter_map(|e| e.ok())
                        .find(|e| e.file_name() == "emulator")
                        .ok_or(anyhow!(
                            "Cannot find find emulator executable from {:?}",
                            fuchsia_root.join("prebuilt/third_party/aemu").display()
                        ))?
                        .path()
                        .to_path_buf()
                }
            },
            grpcwebproxy: match read_env_path("PREBUILT_GRPCWEBPROXY_DIR") {
                Ok(val) => val.join("grpcwebproxy"),
                _ => {
                    let fuchsia_root = f.find_fuchsia_root()?;
                    WalkDir::new(fuchsia_root.join("prebuilt/third_party/grpcwebproxy"))
                        .into_iter()
                        .filter_map(|e| e.ok())
                        .find(|e| e.file_name() == "grpcwebproxy")
                        .ok_or(anyhow!(
                            "Cannot find find grpcwebproxy executable from {:?}",
                            fuchsia_root.join("prebuilt/third_party/grpcwebproxy").display()
                        ))?
                        .path()
                        .to_path_buf()
                }
            },
            vdl: match read_env_path("PREBUILT_VDL_DIR") {
                Ok(val) => val.join("device_launcher"),
                _ => f.find_fuchsia_root()?.join("prebuilt/vdl/device_launcher"),
            },
            device_finder: host_out_dir.join("device-finder"),
            far: host_out_dir.join("far"),
            fvm: host_out_dir.join("fvm"),
            pm: host_out_dir.join("pm"),
            zbi: host_out_dir.join("zbi"),
            is_sdk: false,
        })
    }

    /// Initialize host tools for GN SDK usage.
    ///
    /// First check the existence of environment variable TOOL_DIR, if not specified
    /// look for host tools in the program's containing directory.
    pub fn from_sdk_env() -> Result<HostTools> {
        let sdk_tool_dir = match read_env_path("TOOL_DIR") {
            Ok(dir) => dir,
            _ => get_fuchsia_sdk_tools_dir()?,
        };

        Ok(HostTools {
            // prebuilt binaries that can be optionally fetched from cipd.
            aemu: PathBuf::new(),
            grpcwebproxy: PathBuf::new(),
            vdl: PathBuf::new(),
            // in-tree tools that are packaged with GN SDK.
            device_finder: sdk_tool_dir.join("device-finder"),
            far: sdk_tool_dir.join("far"),
            fvm: sdk_tool_dir.join("fvm"),
            pm: sdk_tool_dir.join("pm"),
            zbi: sdk_tool_dir.join("zbi"),
            is_sdk: true,
        })
    }

    /// Reads the <prebuild>.version file stored in <sdk_root>/bin/<prebuild>.version
    ///
    /// # Arguments
    ///
    /// * `file_name` - <prebuild>.version file name.
    ///     ex: 'aemu.version', this file is expected to be found under <sdk_root>/bin
    pub fn read_prebuild_version(&self, file_name: &str) -> Result<String> {
        if self.is_sdk {
            let version_file = get_fuchsia_sdk_dir()?.join("bin").join(file_name);
            if version_file.exists() {
                println!(
                    "{}",
                    Yellow.paint(format!(
                        "[fvdl] reading prebuild version file from: {}",
                        version_file.display()
                    ))
                );
                return Ok(read_to_string(version_file)?);
            };
            println!(
                "{}",
                Red.paint(format!(
                    "[fvdl] prebuild version file: {} does not exist.",
                    version_file.display()
                ))
            );
            return Err(format_err!(
                "reading prebuilt version errored: file {:?} does not exist.",
                version_file
            ));
        }
        return Err(format_err!("reading prebuild version file is only support with --sdk flag."));
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
                "https://chrome-infra-packages.appspot.com/dl/fuchsia/{}/{}/+/{}",
                cipd_pkg, arch, label
            )
            .parse::<Uri>()?;
            let name = cipd_pkg
                .split('/')
                .last()
                .ok_or(anyhow!("Cannot identify filename from {}", cipd_pkg))?;
            let cipd_zip = root_path.join(format!("{}-{}.zip", name, label.replace(":", "-")));
            let unzipped_root = root_path.join(format!("{}-{}", name, label.replace(":", "-")));

            match label.as_str() {
                // "latest" and "integration" labels always point to the newest release.
                // We cannot assume that the binary is the same as last fetched. Therefore
                // we will always re-download and unzip when used.
                "latest" | "integration" => {
                    if cipd_zip.exists() {
                        remove_file(&cipd_zip)?;
                    }
                    if unzipped_root.exists() {
                        remove_dir_all(&unzipped_root)?;
                    }
                }
                _ => {
                    if unzipped_root.exists() {
                        return Ok(unzipped_root);
                    }
                }
            };
            let status = cipd::download(url.clone(), &cipd_zip).await?;
            if status == StatusCode::OK {
                cipd::extract_zip(&cipd_zip, &unzipped_root, false /* debug */)?;
                Ok(unzipped_root)
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
    /// First checks for environment variable FUCHSIA_BUILD_DIR. If not specify looks into
    /// <repo_root>/.fx-build-dir. For example ~/fuchsia/.fx-build-dir
    pub fn from_tree_env(f: &impl FuchsiaPaths) -> Result<ImageFiles> {
        let fuchsia_build_dir = match read_env_path("FUCHSIA_BUILD_DIR") {
            Ok(val) => val,
            _ => {
                let root = f.find_fuchsia_root()?;
                let build_dir = File::open(root.join(".fx-build-dir"))?;
                let build_dir_file = BufReader::new(build_dir);
                let dir = build_dir_file
                    .lines()
                    .nth(0)
                    .ok_or(anyhow!("cannot read file {:?}", root.join(".fx-build-dir")))?;
                root.join(dir?)
            }
        };
        println!("[fvdl] Using fuchsia build dir: {:?}", fuchsia_build_dir.display());

        Ok(ImageFiles {
            amber_files: fuchsia_build_dir.join("amber-files"),
            build_args: fuchsia_build_dir.join("args.gn"),
            fvm: fuchsia_build_dir.join(read_env_path("IMAGE_FVM_RAW")?),
            kernel: fuchsia_build_dir.join(read_env_path("IMAGE_QEMU_KERNEL_RAW")?),
            zbi: fuchsia_build_dir.join(read_env_path("IMAGE_ZIRCONA_ZBI")?),
        })
    }

    /// When running from SDK (running with --sdk), we will either fetch images from GCS or use cached-image files.
    /// If fetching from GCS, these image files will be ignored by device_launcher.
    /// If using cached images, call update_paths_from_cache() to populate the image file paths.
    pub fn from_sdk_env() -> Result<ImageFiles> {
        Ok(ImageFiles {
            amber_files: PathBuf::new(),
            build_args: PathBuf::new(),
            fvm: PathBuf::new(),
            kernel: PathBuf::new(),
            zbi: PathBuf::new(),
        })
    }

    pub fn check(&self) -> Result<()> {
        if !self.amber_files.exists() {
            ffx_bail!("amber-files at {:?} does not exist", self.amber_files);
        }
        if !self.build_args.exists() {
            ffx_bail!("build_args file at {:?} does not exist", self.build_args);
        }
        if !self.fvm.exists() {
            ffx_bail!("fvm file at {:?} does not exist", self.fvm);
        }
        if !self.kernel.exists() {
            ffx_bail!("kernel file at {:?} does not exist", self.kernel);
        }
        if !self.zbi.exists() {
            ffx_bail!("zbi file at {:?} does not exist", self.zbi);
        }
        Ok(())
    }

    pub fn images_exist(&self) -> bool {
        return self.amber_files.exists()
            && self.build_args.exists()
            && self.fvm.exists()
            && self.kernel.exists()
            && self.zbi.exists();
    }

    #[allow(dead_code)]
    pub fn print(&self) {
        println!("package {:?}", self.amber_files);
        println!("build_args {:?}", self.build_args);
        println!("fvm {:?}", self.fvm);
        println!("kernel {:?}", self.kernel);
        println!("zbi {:?}", self.zbi);
    }

    pub fn update_paths_from_cache(&mut self, cache_root: &PathBuf) {
        self.amber_files = cache_root.join("package_archive");
        self.build_args = cache_root.join("images").join("buildargs");
        self.fvm = cache_root.join("images").join("femu-fvm");
        self.kernel = cache_root.join("images").join("femu-kernel");
        self.zbi = cache_root.join("images").join("zircon-a.zbi");
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
    pub private_key: PathBuf,
}

impl SSHKeys {
    #[allow(dead_code)]
    pub fn print(&self) {
        println!("private_key {:?}", self.private_key);
        println!("auth_key {:?}", self.auth_key);
    }

    /// Initialize SSH key files for in-tree usage.
    ///
    /// Requires the environment variable FUCHSIA_BUILD_DIR to be specified.
    pub fn from_tree_env(f: &impl FuchsiaPaths) -> Result<SSHKeys> {
        let ssh_file = File::open(f.find_fuchsia_root()?.join(".fx-ssh-path"))?;
        let ssh_file = BufReader::new(ssh_file);
        let mut lines = ssh_file.lines();

        let private_key = PathBuf::from(lines.next().unwrap()?);
        let auth_key = PathBuf::from(lines.next().unwrap()?);
        Ok(SSHKeys { auth_key: auth_key, private_key: private_key })
    }

    /// Initialize SSH key files for GN SDK usage.
    ///
    /// Requires SSH keys to have been generated and stored in $HOME/.ssh/...
    pub fn from_sdk_env() -> Result<SSHKeys> {
        let keys = SSHKeys {
            auth_key: home_dir().unwrap_or_default().join(".ssh/fuchsia_authorized_keys"),
            private_key: home_dir().unwrap_or_default().join(".ssh/fuchsia_ed25519"),
        };
        Ok(keys)
    }

    pub fn check(&self) -> Result<()> {
        if !self.private_key.exists() {
            ffx_bail!("private_key file at {:?} does not exist", self.private_key);
        }
        if !self.auth_key.exists() {
            ffx_bail!("public_key file at {:?} does not exist", self.auth_key);
        }
        Ok(())
    }

    pub fn stage_files(&mut self, dir: &PathBuf) -> Result<()> {
        let vdl_priv_key_dest = dir.join("id_ed25519");
        let vdl_priv_key_src = self.private_key.as_path();
        unix::fs::symlink(&vdl_priv_key_src, &vdl_priv_key_dest)?;
        self.private_key = vdl_priv_key_dest.to_path_buf();

        let vdl_auth_key_dest = dir.join("id_ed25519.pub");
        let vdl_auth_key_src = self.auth_key.as_path();
        unix::fs::symlink(&vdl_auth_key_src, &vdl_auth_key_dest)?;
        self.auth_key = vdl_auth_key_dest.to_path_buf();

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
    pub start_package_server: bool,
    pub packages_to_serve: String,
    pub image_size: String,
    pub device_proto: String,
    pub gpu: String,
    pub pointing_device: String,
    pub gcs_bucket: String,
    pub gcs_image_archive: String,
    pub sdk_version: String,
    pub cache_root: PathBuf,
    pub extra_kerel_args: String,
}

impl From<&StartCommand> for VDLArgs {
    fn from(cmd: &StartCommand) -> Self {
        let mut gpu = get_default_graphics();
        if cmd.host_gpu {
            gpu = "host".to_string();
        } else if cmd.software_gpu {
            gpu = "swiftshader_indirect".to_string();
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
        let gcs_image = cmd.image_name.as_ref().unwrap_or(&String::from("qemu-x64")).to_string();
        let sdk_version = match &cmd.sdk_version {
            Some(version) => version.to_string(),
            None => match get_sdk_version_from_manifest() {
                Ok(version) => version,
                Err(_) => String::from(""),
            },
        };
        let mut cache_path = PathBuf::new();
        if cmd.cache_image {
            cache_path = get_sdk_data_dir().unwrap_or_default().join(&gcs_image).join(&sdk_version);
        }
        VDLArgs {
            headless: cmd.headless,
            tuntap: cmd.tuntap,
            enable_hidpi_scaling: cmd.hidpi_scaling,
            upscript: cmd.upscript.as_ref().unwrap_or(&String::from("")).to_string(),
            start_package_server: !cmd.nopackageserver,
            packages_to_serve: cmd
                .packages_to_serve
                .as_ref()
                .unwrap_or(&String::from(""))
                .to_string(),
            image_size: cmd.image_size.as_ref().unwrap_or(&String::from("2G")).to_string(),
            device_proto: cmd.device_proto.as_ref().unwrap_or(&String::from("")).to_string(),
            gpu: gpu,
            pointing_device: cmd
                .pointing_device
                .as_ref()
                .unwrap_or(&String::from("touch"))
                .to_string(),
            enable_grpcwebproxy: enable_grpcwebproxy,
            grpcwebproxy_port: grpcwebproxy_port,
            gcs_bucket: cmd.gcs_bucket.as_ref().unwrap_or(&String::from("fuchsia")).to_string(),
            gcs_image_archive: gcs_image,
            sdk_version: sdk_version,
            cache_root: cache_path,
            extra_kerel_args: cmd.kernel_args.as_ref().unwrap_or(&String::from("")).to_string(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serial_test::serial;
    use std::fs::read_dir;
    use std::io::Write;
    use tempfile::Builder;

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
            vdl_version: None,
            emulator_log: None,
            package_server_log: Some("/a/b/c/server.log".to_string()),
            amber_unpack_root: None,
            envs: Vec::new(),
            port_map: None,
            vdl_output: None,
            nointeractive: false,
            cache_image: true,
            kernel_args: None,
            debugger: false,
            nopackageserver: false,
        };
        let vdl_args: VDLArgs = start_command.into();
        assert_eq!(vdl_args.headless, false);
        assert_eq!(vdl_args.tuntap, true);
        assert_eq!(vdl_args.upscript, "/path/to/upscript");
        assert_eq!(vdl_args.packages_to_serve, "pkg1.far,pkg2.far");
        assert_eq!(vdl_args.image_size, "2G");
        assert_eq!(vdl_args.device_proto, "");
        assert_eq!(vdl_args.gpu, "host");
        assert_eq!(vdl_args.start_package_server, true);
        assert!(vdl_args.cache_root.as_path().ends_with("qemu-x64/0.20201130.3.1"));
    }

    #[test]
    #[serial]
    fn test_host_tools() -> Result<()> {
        env::set_var("HOST_OUT_DIR", "/host/out");
        env::set_var("PREBUILT_AEMU_DIR", "/host/out/aemu");
        env::set_var("PREBUILT_VDL_DIR", "/host/out/vdl");
        env::set_var("PREBUILT_GRPCWEBPROXY_DIR", "/host/out/grpcwebproxy");

        let host_tools = HostTools::from_tree_env(&InTreePaths {})?;
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
    #[serial]
    fn test_host_tools_no_env_var() -> Result<()> {
        env::set_var("HOST_OUT_DIR", "/host/out");
        env::remove_var("PREBUILT_AEMU_DIR");
        env::remove_var("PREBUILT_VDL_DIR");
        env::remove_var("PREBUILT_GRPCWEBPROXY_DIR");

        let mut mock = MockFuchsiaPaths::new();
        let tmp_dir = Builder::new().tempdir()?;
        let a = tmp_dir.into_path();

        create_dir_all(a.join("prebuilt/third_party/aemu"))?;
        File::create(a.join("prebuilt/third_party/aemu/emulator"))?
            .write_all("foo bar".as_bytes())?;

        create_dir_all(a.join("prebuilt/third_party/grpcwebproxy"))?;
        File::create(a.join("prebuilt/third_party/grpcwebproxy/grpcwebproxy"))?
            .write_all("apple banana".as_bytes())?;

        create_dir_all(a.join("prebuilt/vdl"))?;
        File::create(a.join("prebuilt/vdl/device_launcher"))?.write_all("deadbeef".as_bytes())?;

        mock.expect_find_fuchsia_root().returning(move || Ok(a.clone()));

        let host_tools = HostTools::from_tree_env(&mock)?;
        assert!(!host_tools.aemu.as_os_str().is_empty());
        assert!(!host_tools.vdl.as_os_str().is_empty());
        assert!(!host_tools.grpcwebproxy.as_os_str().is_empty());
        assert_eq!(host_tools.far.to_str().unwrap(), "/host/out/far");
        assert_eq!(host_tools.fvm.to_str().unwrap(), "/host/out/fvm");
        assert_eq!(host_tools.pm.to_str().unwrap(), "/host/out/pm");
        assert_eq!(host_tools.device_finder.to_str().unwrap(), "/host/out/device-finder");
        assert_eq!(host_tools.zbi.to_str().unwrap(), "/host/out/zbi");
        Ok(())
    }

    #[test]
    #[serial]
    fn test_image_files() -> Result<()> {
        env::remove_var("FUCHSIA_BUILD_DIR");
        env::set_var("IMAGE_ZIRCONA_ZBI", "zircona");
        env::set_var("IMAGE_QEMU_KERNEL_RAW", "kernel");
        env::set_var("IMAGE_FVM_RAW", "fvm");
        let mut mock = MockFuchsiaPaths::new();
        let data = format!(
            "/build/out
",
        );
        let tmp_dir = Builder::new().prefix("fvdl_tests_").tempdir()?;
        let a = tmp_dir.into_path();
        File::create(a.join(".fx-build-dir"))?.write_all(data.as_bytes())?;
        mock.expect_find_fuchsia_root().returning(move || Ok(a.clone()));
        let mut image_files = ImageFiles::from_tree_env(&mock)?;
        assert_eq!(image_files.zbi.to_str().unwrap(), "/build/out/zircona");
        assert_eq!(image_files.kernel.to_str().unwrap(), "/build/out/kernel");
        assert_eq!(image_files.fvm.to_str().unwrap(), "/build/out/fvm");
        assert_eq!(image_files.build_args.to_str().unwrap(), "/build/out/args.gn");
        assert_eq!(image_files.amber_files.to_str().unwrap(), "/build/out/amber-files");

        let tmp_dir = Builder::new().prefix("fvdl_tests_").tempdir()?;

        image_files.stage_files(&tmp_dir.path().to_owned())?;
        assert_eq!(image_files.kernel.to_str(), tmp_dir.path().join("femu_kernel").to_str());
        Ok(())
    }

    #[test]
    #[serial]
    fn test_ssh_files() -> Result<()> {
        let mut mock = MockFuchsiaPaths::new();
        let data = format!(
            "/usr/local/home/foo/.ssh/fuchsia_ed25519
/usr/local/home/foo/.ssh/fuchsia_authorized_keys
",
        );
        let tmp_dir = Builder::new().prefix("fvdl_tests_").tempdir()?;
        let a = tmp_dir.into_path();
        File::create(a.join(".fx-ssh-path"))?.write_all(data.as_bytes())?;
        mock.expect_find_fuchsia_root().returning(move || Ok(a.clone()));
        let mut ssh_files = SSHKeys::from_tree_env(&mock)?;
        assert_eq!(
            ssh_files.private_key.to_str().unwrap(),
            "/usr/local/home/foo/.ssh/fuchsia_ed25519"
        );
        assert_eq!(
            ssh_files.auth_key.to_str().unwrap(),
            "/usr/local/home/foo/.ssh/fuchsia_authorized_keys"
        );
        let tmp_dir = Builder::new().prefix("fvdl_test_ssh_").tempdir()?;
        ssh_files.stage_files(&tmp_dir.path().to_owned())?;
        assert!(ssh_files.private_key.ends_with("id_ed25519"));
        Ok(())
    }

    #[test]
    #[serial]
    fn test_sdk_data_dir() -> Result<()> {
        let tmp_dir = Builder::new().prefix("fvdl_test_sdk_data_dir_").tempdir()?;
        env::set_var("FUCHSIA_SDK_DATA_DIR", tmp_dir.path());
        let p = get_sdk_data_dir()?;
        assert_eq!(p.to_str(), tmp_dir.path().to_str());
        Ok(())
    }

    #[test]
    #[serial]
    fn test_download_and_extract() -> Result<()> {
        let tmp_dir = Builder::new().prefix("fvdl_test_download_").tempdir()?;
        env::set_var("FEMU_DOWNLOAD_DIR", tmp_dir.path());
        let host_tools = HostTools::from_sdk_env()?;
        let mut unzipped_root =
            host_tools.download_and_extract("latest".to_string(), "vdl".to_string())?;

        let mut has_extract = false;
        for path in read_dir(&unzipped_root)? {
            let entry = path?;
            let p = entry.path();
            println!("Found path {}", p.display());
            if p.ends_with("device_launcher") {
                has_extract = true;
            }
        }
        assert!(has_extract);

        // Download "latest" again should trigger a cleanup and re-download
        unzipped_root = host_tools.download_and_extract("latest".to_string(), "vdl".to_string())?;
        has_extract = false;
        for path in read_dir(&unzipped_root)? {
            let entry = path?;
            let p = entry.path();
            println!("Found path {}", p.display());
            if p.ends_with("device_launcher") {
                has_extract = true;
            }
        }
        assert!(has_extract);
        Ok(())
    }
}
