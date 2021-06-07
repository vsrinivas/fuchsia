// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{anyhow, Result},
    errors::ffx_error,
    serde::Deserialize,
    std::{fs, io::BufReader},
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

#[cfg(test)]
mod test {
    use super::*;

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
          "label": "//tools/net/device-finder:device-finder(//build/toolchain:host_x64)",
          "name": "device-finder",
          "os": "linux",
          "path": "host_x64/exe.unstripped/device-finder"
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
        assert_eq!(
            tools.find_path_with_arch("device-finder", "x64")?,
            "host_x64/exe.unstripped/device-finder"
        );
        assert_eq!(tools.find_path_with_arch("zbi", "x64")?, "host_x64/zbi");
        assert_eq!(tools.find_path_with_arch("fvm", "x64")?, "host_x64/fvm");
        Ok(())
    }
}
