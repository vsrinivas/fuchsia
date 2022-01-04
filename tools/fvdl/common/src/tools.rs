// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::cipd;
use crate::types::{
    get_fuchsia_sdk_dir, get_fuchsia_sdk_tools_dir, get_sdk_data_dir, read_env_path, FuchsiaPaths,
};
use {
    ansi_term::Colour::{Red, Yellow},
    anyhow::{anyhow, format_err, Result},
    errors::ffx_error,
    fuchsia_async::LocalExecutor,
    hyper::{StatusCode, Uri},
    serde::Deserialize,
    std::{
        env, fmt, fs,
        fs::{create_dir_all, read_to_string, remove_dir_all, remove_file},
        io::BufReader,
        path::PathBuf,
    },
    walkdir::WalkDir,
};

#[derive(Default, Deserialize)]
pub struct Tools(Vec<Tool>);

#[derive(Default, Deserialize)]
pub struct Tool {
    pub cpu: String,
    pub label: String,
    pub name: String,
    pub os: String,
    pub path: String,
}

impl Tools {
    pub fn from_build_dir(path: std::path::PathBuf) -> Result<Self> {
        let manifest_path = path.join("tool_paths.json");
        fs::File::open(manifest_path.clone())
            .map_err(|e| ffx_error!("Cannot open file {:?} \nerror: {:?}", manifest_path, e))
            .map(BufReader::new)
            .map(serde_json::from_reader)?
            .map_err(|e| anyhow!("json parsing errored {}", e))
    }

    #[cfg(test)]
    pub fn from_string(content: &str) -> Result<Self> {
        serde_json::from_str(content).map_err(|e| anyhow!("json parsing errored {}", e))
    }

    pub fn find_path(&self, name: &str) -> Result<String> {
        self.find_path_with_arch(
            name,
            match std::env::consts::ARCH {
                "x86_64" => "x64",
                "aarch64" => "arm64",
                _ => "unsupported",
            },
        )
    }

    pub fn find_path_with_arch(&self, name: &str, host_cpu: &str) -> Result<String> {
        self.0
            .iter()
            .find(|x| x.name == name && x.cpu == host_cpu)
            .map(|i| i.path.clone())
            .ok_or(anyhow!("cannot find matching tool for name {}, arch {}", name, host_cpu))
    }
}

#[derive(Clone)]
pub struct HostTools {
    pub aemu: PathBuf,
    pub far: Option<PathBuf>,
    pub ffx: Option<PathBuf>,
    pub fvm: Option<PathBuf>,
    pub grpcwebproxy: PathBuf,
    pub pm: Option<PathBuf>,
    pub vdl: PathBuf,
    pub zbi: PathBuf,
    pub is_sdk: bool,
}

impl fmt::Debug for HostTools {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "[fvdl] tool aemu {:?}", self.aemu)?;
        writeln!(f, "[fvdl] tool far {:?}", self.far)?;
        writeln!(f, "[fvdl] tool ffx {:?}", self.ffx)?;
        writeln!(f, "[fvdl] tool fvm {:?}", self.fvm)?;
        writeln!(f, "[fvdl] tool grpcwebproxy {:?}", self.grpcwebproxy)?;
        writeln!(f, "[fvdl] tool pm {:?}", self.pm)?;
        writeln!(f, "[fvdl] tool vdl {:?}", self.vdl)?;
        write!(f, "[fvdl] tool zbi {:?}", self.zbi)
    }
}

impl HostTools {
    /// Initialize host tools for in-tree usage via fx vdl.
    ///
    /// Environment variable HOST_OUT_DIR, PREBUILT_AEMU_DIR,
    /// REBUILT_GRPCWEBPROXY_DIR, and PREBUILT_VDL_DIR are optional.
    pub fn from_tree_env(f: &mut impl FuchsiaPaths) -> Result<Self> {
        Ok(Self {
            // prebuilt binaries that can be optionally fetched from cipd.
            aemu: match read_env_path("PREBUILT_AEMU_DIR") {
                Ok(val) => val.join("emulator"),
                _ => {
                    let fuchsia_root = f.find_fuchsia_root()?;
                    WalkDir::new(fuchsia_root.join("prebuilt/third_party/android/aemu/release"))
                        .into_iter()
                        .filter_map(|e| e.ok())
                        .find(|e| e.file_name() == "emulator")
                        .ok_or(anyhow!(
                            "Cannot find emulator executable from {:?}",
                            fuchsia_root
                                .join("prebuilt/third_party/android/aemu/release")
                                .display()
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
                        .find(|e| e.file_name() == "grpcwebproxy" && e.file_type().is_file())
                        .ok_or(anyhow!(
                            "Cannot find grpcwebproxy executable from {:?}",
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
            far: match read_env_path("HOST_OUT_DIR") {
                Ok(val) => Some(val.join("far")),
                _ => f.get_tool_path("far").ok(),
            },
            ffx: match read_env_path("HOST_OUT_DIR") {
                Ok(val) => Some(val.join("ffx")),
                _ => f.get_tool_path("ffx").ok(),
            },
            fvm: match read_env_path("HOST_OUT_DIR") {
                Ok(val) => Some(val.join("fvm")),
                _ => f.get_tool_path("fvm").ok(),
            },
            pm: match read_env_path("HOST_OUT_DIR") {
                Ok(val) => Some(val.join("pm")),
                _ => f.get_tool_path("pm").ok(),
            },
            zbi: match read_env_path("HOST_OUT_DIR") {
                Ok(val) => val.join("zbi"),
                _ => f.get_tool_path("zbi")?,
            },
            is_sdk: false,
        })
    }

    /// Initialize host tools for GN SDK usage.
    ///
    /// First check the existence of environment variable TOOL_DIR, if not specified
    /// look for host tools in the program's containing directory.
    pub fn from_sdk_env() -> Result<Self> {
        let sdk_tool_dir = match read_env_path("TOOL_DIR") {
            Ok(dir) => dir,
            _ => get_fuchsia_sdk_tools_dir()?,
        };

        Ok(Self {
            // prebuilt binaries that can be optionally fetched from cipd.
            aemu: PathBuf::new(),
            grpcwebproxy: PathBuf::new(),
            vdl: PathBuf::new(),
            // in-tree tools that are packaged with GN SDK.
            far: Some(sdk_tool_dir.join("far")),
            ffx: Some(sdk_tool_dir.join("ffx")),
            fvm: Some(sdk_tool_dir.join("fvm")),
            pm: Some(sdk_tool_dir.join("pm")),
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
    /// * `cipd_pkg` - this is appended to cipd url https://chrome-infra-packages.appspot.com/dl/fuchsia/third_party/.
    pub fn download_and_extract(&self, label: String, cipd_pkg: String) -> Result<PathBuf> {
        let mut executor = LocalExecutor::new().unwrap();
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
                //
                // TODO(yuanzhi) When using fuchsia/third_party/android/aemu/release/...
                // we no longer add "integration" ref to any cipd instances. Keeping the "integration" check
                // for now for any backward compatibility we may have to support.
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

#[cfg(test)]
mod test {
    use super::*;
    use crate::types::InTreePaths;
    use crate::types::MockFuchsiaPaths;
    use serial_test::serial;
    use std::fs::{read_dir, File};
    use std::io::Write;
    use tempfile::Builder;

    const TOOLS_JSON: &str = r#"[
        {
          "cpu": "x64",
          "label": "//:tool_paths.llvm-tools(//build/toolchain/fuchsia:x64)",
          "name": "clang-doc",
          "os": "linux",
          "path": "../../prebuilt/third_party/clang/linux-x64/bin/clang-doc"
        },
        {
          "cpu": "x64",
          "label": "//src/developer/ffx:ffx_bin(//build/toolchain:host_x64)",
          "name": "ffx",
          "os": "linux",
          "path": "host_x64/ffx"
        },
        {
          "cpu": "x64",
          "label": "//zircon/tools/zbi:zbi(//build/toolchain:host_x64)",
          "name": "zbi",
          "os": "linux",
          "path": "host_x64/zbi"
        },
        {
          "cpu": "arm64",
          "label": "//src/storage/bin/fvm:fvm(//build/toolchain:host_arm64)",
          "name": "fvm",
          "os": "linux",
          "path": "host_arm64/fvm"
        },
        {
          "cpu": "x64",
          "label": "//src/storage/bin/fvm:fvm(//build/toolchain:host_x64)",
          "name": "fvm",
          "os": "linux",
          "path": "host_x64/fvm"
        },
        {
          "cpu": "x64",
          "label": "//:tool_paths.llvm-tools(//build/toolchain/fuchsia:x64)",
          "name": "clang-tidy",
          "os": "linux",
          "path": "../../prebuilt/third_party/clang/linux-x64/bin/clang-tidy"
        },
        {
          "cpu": "x64",
          "label": "//:tool_paths.llvm-tools(//build/toolchain/fuchsia:x64)",
          "name": "clangd",
          "os": "linux",
          "path": "../../prebuilt/third_party/clang/linux-x64/bin/clangd"
        }
      ]"#;

    #[test]
    fn test_tools_parse() -> Result<()> {
        let tools = Tools::from_string(TOOLS_JSON)?;
        assert_eq!(tools.find_path_with_arch("ffx", "x64")?, "host_x64/ffx");
        assert_eq!(tools.find_path_with_arch("zbi", "x64")?, "host_x64/zbi");
        assert_eq!(tools.find_path_with_arch("fvm", "x64")?, "host_x64/fvm");
        Ok(())
    }

    #[test]
    #[serial]
    fn test_host_tools() -> Result<()> {
        env::set_var("HOST_OUT_DIR", "/host/out");
        env::set_var("PREBUILT_AEMU_DIR", "/host/out/aemu");
        env::set_var("PREBUILT_VDL_DIR", "/host/out/vdl");
        env::set_var("PREBUILT_GRPCWEBPROXY_DIR", "/host/out/grpcwebproxy");

        let host_tools =
            HostTools::from_tree_env(&mut InTreePaths { root_dir: None, build_dir: None })?;
        assert_eq!(host_tools.aemu.to_str().unwrap(), "/host/out/aemu/emulator");
        assert_eq!(host_tools.vdl.to_str().unwrap(), "/host/out/vdl/device_launcher");
        assert_eq!(host_tools.far.as_ref().unwrap().to_str().unwrap(), "/host/out/far");
        assert_eq!(host_tools.ffx.as_ref().unwrap().to_str().unwrap(), "/host/out/ffx");
        assert_eq!(host_tools.fvm.as_ref().unwrap().to_str().unwrap(), "/host/out/fvm");
        assert_eq!(host_tools.pm.as_ref().unwrap().to_str().unwrap(), "/host/out/pm");
        assert_eq!(host_tools.zbi.to_str().unwrap(), "/host/out/zbi");
        Ok(())
    }

    #[test]
    #[serial]
    fn test_host_tools_no_env_var() -> Result<()> {
        env::remove_var("HOST_OUT_DIR");
        env::remove_var("PREBUILT_AEMU_DIR");
        env::remove_var("PREBUILT_VDL_DIR");
        env::remove_var("PREBUILT_GRPCWEBPROXY_DIR");

        let mut mock = MockFuchsiaPaths::new();
        let tmp_dir = Builder::new().tempdir()?;
        let a = tmp_dir.into_path();

        create_dir_all(a.join("prebuilt/third_party/android/aemu/release"))?;
        File::create(a.join("prebuilt/third_party/android/aemu/release/emulator"))?
            .write_all("foo bar".as_bytes())?;

        create_dir_all(a.join("prebuilt/third_party/grpcwebproxy"))?;
        File::create(a.join("prebuilt/third_party/grpcwebproxy/grpcwebproxy"))?
            .write_all("apple banana".as_bytes())?;

        create_dir_all(a.join("prebuilt/vdl"))?;
        File::create(a.join("prebuilt/vdl/device_launcher"))?.write_all("deadbeef".as_bytes())?;

        mock.expect_find_fuchsia_root().returning(move || Ok(a.clone()));
        mock.expect_get_tool_path().returning(|x: &str| {
            let mut p = PathBuf::from("/host/out");
            p.push(x);
            Ok(p)
        });

        let host_tools = HostTools::from_tree_env(&mut mock)?;
        assert!(!host_tools.aemu.as_os_str().is_empty());
        assert!(!host_tools.vdl.as_os_str().is_empty());
        assert!(!host_tools.grpcwebproxy.as_os_str().is_empty());
        assert_eq!(host_tools.far.as_ref().unwrap().to_str().unwrap(), "/host/out/far");
        assert_eq!(host_tools.ffx.as_ref().unwrap().to_str().unwrap(), "/host/out/ffx");
        assert_eq!(host_tools.fvm.as_ref().unwrap().to_str().unwrap(), "/host/out/fvm");
        assert_eq!(host_tools.pm.as_ref().unwrap().to_str().unwrap(), "/host/out/pm");
        assert_eq!(host_tools.zbi.to_str().unwrap(), "/host/out/zbi");
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
