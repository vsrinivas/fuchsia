// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Error, Result},
    ffx_core::ffx_plugin,
    ffx_scrutiny_kernel_cmdline_args::ScrutinyKernelCmdlineCommand,
    scrutiny_config::{Config, LoggingConfig, PluginConfig, RuntimeConfig},
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    scrutiny_utils::golden::{CompareResult, GoldenFile},
    serde_json,
    std::fs,
};

const SOFT_TRANSITION_MSG : &str = "
If you are making a change in fuchsia.git that causes this, you need to perform a soft transition:
1: Instead of adding lines as written above, add each line prefixed with a question mark to mark it as transitional.
2: Instead of removing lines as written above, prefix the line with a question mark to mark it as transitional.
3: Check in your fuchsia.git change.
4: For each new line you added in 1, remove the question mark.
5: For each existing line you modified in 2, remove the line.
";

#[allow(dead_code)]
struct VerifyKernelCmdline {
    golden_path: String,
    zbi_path: String,
}

impl VerifyKernelCmdline {
    pub fn new(golden_path: String, zbi_path: impl Into<String>) -> Self {
        Self { golden_path, zbi_path: zbi_path.into() }
    }

    /// Extracts the kernel commandline from the provided zbi_path and compares
    /// it against the golden file.
    pub fn verify(&self) -> Result<()> {
        let config = Config::run_command_with_runtime(
            CommandBuilder::new("tool.zbi.extract.cmdline")
                .param("input", self.zbi_path.clone())
                .build(),
            RuntimeConfig {
                logging: LoggingConfig { silent_mode: true, ..LoggingConfig::minimal() },
                plugin: PluginConfig { plugins: vec!["ToolkitPlugin".to_string()] },
                ..RuntimeConfig::minimal()
            },
        );
        let scrutiny_output =
            launcher::launch_from_config(config).context("Failed to launch scrutiny")?;
        let kernel_cmdline: String = serde_json::from_str(&scrutiny_output)
            .context(format!("Failed to deserialize scrutiny output: {}", scrutiny_output))?;
        let mut sorted_cmdline =
            kernel_cmdline.split(' ').map(ToString::to_string).collect::<Vec<String>>();
        sorted_cmdline.sort();
        let golden_file =
            GoldenFile::open(self.golden_path.clone()).context("Failed to open golden file")?;
        match golden_file.compare(sorted_cmdline) {
            CompareResult::Matches => Ok(()),
            CompareResult::Mismatch { errors } => {
                println!("Kernel cmdline mismatch");
                println!("");
                for error in errors.iter() {
                    println!("{}", error);
                }
                println!("");
                println!("If you intended to change the kernel command line, please acknowledge it by updating {} with the added or removed lines.", self.golden_path);
                println!("{}", SOFT_TRANSITION_MSG);
                Err(anyhow!("kernel cmdline mismatch"))
            }
        }
    }
}

#[ffx_plugin()]
pub async fn scrutiny_kernel_cmdline(cmd: ScrutinyKernelCmdlineCommand) -> Result<(), Error> {
    if cmd.golden.len() == 0 {
        bail!("Must specify at least one --golden");
    }

    for golden_file in cmd.golden.into_iter() {
        let verifier = VerifyKernelCmdline::new(golden_file, cmd.zbi.clone());
        verifier.verify()?;
        if let Some(stamp) = cmd.stamp.as_ref() {
            fs::write(stamp, "Verified").context("Failed to write stamp file")?;
        }
    }

    Ok(())
}
