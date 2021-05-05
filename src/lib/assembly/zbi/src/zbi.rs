// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Error, Result};
use std::collections::BTreeMap;
use std::fs::File;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::Command;

/// Builder for the Zircon Boot Image (ZBI), which takes in a kernel, BootFS, boot args, and kernel
/// command line.
#[derive(Default)]
pub struct ZbiBuilder {
    kernel: Option<String>,
    bootfs_files: BTreeMap<String, String>,
    bootargs: Vec<String>,
    cmdline: Vec<String>,
}

impl ZbiBuilder {
    /// Set the kernel to be used.
    pub fn set_kernel(&mut self, kernel: &str) {
        self.kernel = Some(kernel.to_string());
    }

    /// Add a BootFS file to the ZBI.
    pub fn add_bootfs_file(&mut self, source: &str, destination: &str) {
        if self.bootfs_files.contains_key(&destination.to_string()) {
            println!("Found duplicate bootfs destination: {}", destination);
            return;
        }
        self.bootfs_files.insert(destination.to_string(), source.to_string());
    }

    /// Add a boot argument to the ZBI.
    pub fn add_boot_arg(&mut self, arg: &str) {
        self.bootargs.push(arg.to_string());
    }

    /// Add a kernel command line argument.
    pub fn add_cmdline_arg(&mut self, arg: &str) {
        self.cmdline.push(arg.to_string());
    }

    /// Build the ZBI.
    pub fn build(&self, gendir: &PathBuf, output: &Path) -> Result<()> {
        // Create the BootFS manifest file that lists all the files to insert
        // into BootFS.
        let mut bootfs_manifest_path = gendir.clone();
        bootfs_manifest_path.push("bootfs_files.list");
        let mut bootfs_manifest = File::create(&bootfs_manifest_path)
            .map_err(|e| Error::new(e).context("failed to create the bootfs manifest"))?;
        self.write_bootfs_manifest(&mut bootfs_manifest)?;

        // Create the boot args file.
        let mut boot_args_path = gendir.clone();
        boot_args_path.push("boot_args.txt");
        let mut boot_args = File::create(&boot_args_path)
            .map_err(|e| Error::new(e).context("failed to create the boot args"))?;
        self.write_boot_args(&mut boot_args)?;

        // Run the zbi tool to construct the ZBI.
        let zbi_args = self.build_zbi_args(&bootfs_manifest_path, &boot_args_path, output)?;
        let status = Command::new("host_x64/zbi")
            .args(&zbi_args)
            .status()
            .expect("Failed to run the zbi tool");
        if !status.success() {
            anyhow::bail!("zbi exited with status: {}", status);
        }

        Ok(())
    }

    fn write_bootfs_manifest(&self, out: &mut impl Write) -> Result<()> {
        for (destination, source) in &self.bootfs_files {
            write!(out, "{}={}\n", destination, source)?;
        }
        Ok(())
    }

    fn write_boot_args(&self, out: &mut impl Write) -> Result<()> {
        for arg in &self.bootargs {
            write!(out, "{}\n", arg)?;
        }
        Ok(())
    }

    fn build_zbi_args(
        &self,
        bootfs_manifest_path: &Path,
        boot_args_path: &Path,
        output_path: &Path,
    ) -> Result<Vec<String>> {
        // Ensure the supplied kernel is a valid file.
        let kernel_path;
        if let Some(kernel) = &self.kernel {
            kernel_path = Path::new(kernel);
        } else {
            anyhow::bail!("No kernel image supplied");
        }

        let mut args: Vec<String> = Vec::new();
        args.push("--type=container".to_string());
        args.push(kernel_path.to_string_lossy().into_owned());
        args.push("--files".to_string());
        args.push(bootfs_manifest_path.to_string_lossy().into_owned());
        args.push("--type=image_args".to_string());
        args.push(format!("--entry={}", boot_args_path.to_string_lossy()));
        args.push("--type=cmdline".to_string());
        for cmd in &self.cmdline {
            args.push(format!("--entry={}", cmd));
        }
        args.push(format!("--output={}", output_path.to_string_lossy()));
        Ok(args)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bootfs_manifest() {
        let mut builder = ZbiBuilder::default();
        let mut output: Vec<u8> = Vec::new();

        builder.write_bootfs_manifest(&mut output).unwrap();
        assert_eq!(output, b"");

        output.clear();
        builder.add_bootfs_file("path/to/file2", "bin/file2");
        builder.add_bootfs_file("path/to/file1", "lib/file1");
        builder.write_bootfs_manifest(&mut output).unwrap();
        assert_eq!(output, b"bin/file2=path/to/file2\nlib/file1=path/to/file1\n");
    }

    #[test]
    fn boot_args() {
        let mut builder = ZbiBuilder::default();
        let mut output: Vec<u8> = Vec::new();

        builder.write_boot_args(&mut output).unwrap();
        assert_eq!(output, b"");

        output.clear();
        builder.add_boot_arg("boot-arg1");
        builder.add_boot_arg("boot-arg2");
        builder.write_boot_args(&mut output).unwrap();
        assert_eq!(output, b"boot-arg1\nboot-arg2\n");
    }

    #[test]
    fn zbi_args_missing_kernel() {
        let bootfs_manifest = Path::new("bootfs");
        let boot_args = Path::new("bootargs");
        let output = Path::new("output");
        let builder = ZbiBuilder::default();

        // We should fail without a kernel.
        assert!(builder.build_zbi_args(bootfs_manifest, boot_args, output).is_err());
    }

    #[test]
    fn zbi_args_with_kernel() {
        let bootfs_manifest = Path::new("bootfs");
        let boot_args = Path::new("bootargs");
        let output = Path::new("output");
        let mut builder = ZbiBuilder::default();

        builder.set_kernel("path/to/kernel");
        let args = builder.build_zbi_args(bootfs_manifest, boot_args, output).unwrap();
        assert_eq!(
            args,
            [
                "--type=container",
                "path/to/kernel",
                "--files",
                "bootfs",
                "--type=image_args",
                "--entry=bootargs",
                "--type=cmdline",
                "--output=output",
            ]
        );
    }

    #[test]
    fn zbi_args_with_cmdline() {
        let bootfs_manifest = Path::new("bootfs");
        let boot_args = Path::new("bootargs");
        let output = Path::new("output");
        let mut builder = ZbiBuilder::default();

        builder.set_kernel("path/to/kernel");
        builder.add_cmdline_arg("cmd-arg1");
        builder.add_cmdline_arg("cmd-arg2");
        let args = builder.build_zbi_args(bootfs_manifest, boot_args, output).unwrap();
        assert_eq!(
            args,
            [
                "--type=container",
                "path/to/kernel",
                "--files",
                "bootfs",
                "--type=image_args",
                "--entry=bootargs",
                "--type=cmdline",
                "--entry=cmd-arg1",
                "--entry=cmd-arg2",
                "--output=output",
            ]
        );
    }
}
