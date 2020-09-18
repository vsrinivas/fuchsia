// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::target::GnTarget,
    anyhow::{anyhow, Error},
    std::ffi::OsString,
    std::process::Command,
    std::{fs::File, io::Read, path::PathBuf},
    tempfile::{tempdir, TempDir},
};

pub struct BuildScriptOutput {
    pub cfgs: Vec<String>,
    pub rerun: Vec<PathBuf>,
}

impl BuildScriptOutput {
    #[allow(unused)]
    pub fn parse_from_file(file: PathBuf) -> Result<Self, Error> {
        let mut file = File::open(file)?;
        let mut contents = String::new();
        file.read_to_string(&mut contents)?;
        let configs: Vec<&str> = contents.split("\n").collect();
        Self::parse(configs)
    }

    pub fn parse(lines: Vec<&str>) -> Result<Self, Error> {
        let mut bs = BuildScriptOutput { cfgs: vec![], rerun: vec![] };

        let rustc_cfg = "cargo:rustc-cfg=";
        let rustc_rerun = "cargo:rerun-if-changed=";

        for line in lines {
            if line == "" {
                continue;
            } else if line.starts_with(rustc_cfg) {
                bs.cfgs.push(format!("\"--cfg={}\"", line.split_at(rustc_cfg.len()).1.to_string()));
            } else if line.starts_with(rustc_rerun) {
                // Ignored because these are always vendored
            } else {
                return Err(anyhow!("Don't know how to parse: {}", line));
            }
        }
        Ok(bs)
    }
}

pub struct BuildScript<'a> {
    path: PathBuf,
    output_dir: TempDir,
    target: &'a GnTarget<'a>,
}

fn get_rustc() -> OsString {
    std::env::var_os("RUSTC").unwrap_or(std::ffi::OsString::from("rustc"))
}

impl<'a> BuildScript<'a> {
    pub fn compile(target: &'a GnTarget<'_>) -> Result<BuildScript<'a>, Error> {
        let build_script = target.build_script.as_ref().unwrap();
        let rustc = get_rustc();
        // compile the build script
        let crate_name = format!("{}_build_script", target.name().replace("-", "_"));
        let mut out_file = std::env::temp_dir();
        out_file.push(crate_name.clone());

        let mut features = vec![];
        for feature in target.features {
            features.push(format!("--cfg=feature=\"{}\"", feature))
        }

        let output = Command::new(rustc)
            .arg(format!("--edition={}", target.edition))
            .arg(format!("--crate-name={}", crate_name))
            .args(features)
            .arg("--crate-type=bin")
            .arg("-o")
            .arg(out_file.clone())
            .arg(build_script.path.clone())
            .output()
            .expect("failed to execute process");
        if !output.status.success() {
            return Err(anyhow!(
                "Failed to compile {}:\n{}",
                target.gn_target_name(),
                String::from_utf8_lossy(&output.stderr)
            ));
        }

        let out_dir = tempdir()?;
        Ok(BuildScript { path: out_file, output_dir: out_dir, target: &target })
    }

    pub fn execute(self) -> Result<BuildScriptOutput, Error> {
        let mut features = vec![];
        for feature in self.target.features {
            features.push((format!("CARGO_FEATURE_{}", feature.to_uppercase()), ""))
        }

        let output = Command::new(self.path.clone())
            .env("RUSTC", get_rustc())
            .env("OUT_DIR", self.output_dir.path())
            // TODO more environment variables might be needed for Cargo
            // https://doc.rust-lang.org/cargo/reference/environment-variables.html
            .env("CARGO_CFG_TARGET_FEATURE", "")
            .envs(features)
            .env("TARGET", "x86_64-unknown-linux-gnu")
            .output()
            .expect("failed to execute process");
        if !output.status.success() {
            return Err(anyhow!("Failed to run {:?}", self.path));
        }

        let stdout = String::from_utf8(output.stdout)?;
        let configs: Vec<&str> = stdout.split("\n").collect();

        BuildScriptOutput::parse(configs)
    }
}
