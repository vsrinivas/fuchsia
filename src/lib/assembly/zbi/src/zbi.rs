// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Error, Result};
use log::debug;
use std::collections::BTreeMap;
use std::fs::File;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::Command;

/// Builder for the Zircon Boot Image (ZBI), which takes in a kernel, BootFS, boot args, and kernel
/// command line.
#[derive(Default)]
pub struct ZbiBuilder {
    kernel: Option<PathBuf>,
    // Map from file destination in the ZBI to the path of the source file on the host.
    bootfs_files: BTreeMap<String, PathBuf>,
    bootargs: Vec<String>,
    cmdline: Vec<String>,

    /// optional compression to use.
    compression: Option<String>,

    /// optional output manifest file
    output_manifest: Option<PathBuf>,
}

impl ZbiBuilder {
    /// Set the kernel to be used.
    pub fn set_kernel(&mut self, kernel: impl AsRef<Path>) {
        self.kernel = Some(kernel.as_ref().to_path_buf());
    }

    /// Add a BootFS file to the ZBI.
    pub fn add_bootfs_file(&mut self, source: impl AsRef<Path>, destination: impl AsRef<str>) {
        if self.bootfs_files.contains_key(destination.as_ref()) {
            println!("Found duplicate bootfs destination: {}", destination.as_ref());
            return;
        }
        self.bootfs_files.insert(destination.as_ref().to_string(), source.as_ref().to_path_buf());
    }

    /// Add a boot argument to the ZBI.
    pub fn add_boot_arg(&mut self, arg: &str) {
        self.bootargs.push(arg.to_string());
    }

    /// Add a kernel command line argument.
    pub fn add_cmdline_arg(&mut self, arg: &str) {
        self.cmdline.push(arg.to_string());
    }

    /// Set the compression to use with the ZBI.
    pub fn set_compression(&mut self, compress: impl ToString) {
        self.compression = Some(compress.to_string());
    }

    /// Set the path to an optional JSON output manifest to produce.
    pub fn set_output_manifest(&mut self, manifest: impl AsRef<Path>) {
        self.output_manifest = Some(manifest.as_ref().to_path_buf());
    }

    /// Build the ZBI.
    pub fn build(&self, gendir: impl AsRef<Path>, output: impl AsRef<Path>) -> Result<()> {
        // Create the BootFS manifest file that lists all the files to insert
        // into BootFS.
        let bootfs_manifest_path = gendir.as_ref().join("bootfs_files.list");
        let mut bootfs_manifest = File::create(&bootfs_manifest_path)
            .map_err(|e| Error::new(e).context("failed to create the bootfs manifest"))?;
        self.write_bootfs_manifest(&mut bootfs_manifest)?;

        // Currently disabled to match the existing //build/images zbi creation.
        //  TODO(fxbug.dev/77387) - Re-enable bootarg generation
        //
        // // Create the boot args file.
        // let boot_args_path = gendir.as_ref().join("boot_args.txt");
        // let mut boot_args = File::create(&boot_args_path)
        //     .map_err(|e| Error::new(e).context("failed to create the boot args"))?;
        // self.write_boot_args(&mut boot_args)?;

        // Run the zbi tool to construct the ZBI.
        let zbi_args = self.build_zbi_args(&bootfs_manifest_path, None::<PathBuf>, output)?;
        debug!("ZBI command args: {:?}", zbi_args);

        let output = Command::new("host_x64/zbi")
            .args(&zbi_args)
            .output()
            .context("Failed to run the zbi tool")?;
        if !output.status.success() {
            anyhow::bail!(
                "zbi exited with status: {}\n{}",
                output.status,
                String::from_utf8_lossy(output.stderr.as_slice())
            );
        }

        Ok(())
    }

    fn write_bootfs_manifest(&self, out: &mut impl Write) -> Result<()> {
        for (destination, source) in &self.bootfs_files {
            write!(out, "{}", destination)?;
            write!(out, "=")?;
            // TODO(fxbug.dev76135): Use the zbi tool's set of valid inputs instead of constraining
            // to valid UTF-8.
            let source = source.to_str().ok_or(anyhow!(format!(
                "source path {} is not valid UTF-8",
                source.to_string_lossy().to_string()
            )))?;
            writeln!(out, "{}", source)?;
        }
        Ok(())
    }

    //  TODO(fxbug.dev/77387) - Re-enable bootarg generation
    #[allow(dead_code)]
    fn write_boot_args(&self, out: &mut impl Write) -> Result<()> {
        for arg in &self.bootargs {
            writeln!(out, "{}", arg)?;
        }
        Ok(())
    }

    fn build_zbi_args(
        &self,
        bootfs_manifest_path: impl AsRef<Path>,
        boot_args_path: Option<impl AsRef<Path>>,
        output_path: impl AsRef<Path>,
    ) -> Result<Vec<String>> {
        // Ensure a kernel is supplied.
        let kernel = &self.kernel.as_ref().ok_or(anyhow!("No kernel image supplied"))?;

        // Ensure the files are valid UTF-8.
        let kernel = kernel.to_str().ok_or(anyhow!("Kernel path is not valid UTF-8"))?;
        let bootfs_manifest_path = bootfs_manifest_path
            .as_ref()
            .to_str()
            .ok_or(anyhow!("BootFS manifest path is not valid UTF-8"))?;
        let output_path =
            output_path.as_ref().to_str().ok_or(anyhow!("Output path is not valid UTF-8"))?;

        let mut args: Vec<String> = Vec::new();

        // Add the kernel itself, first, to make a bootable ZBI.
        args.push("--type=container".to_string());
        args.push(kernel.to_string());

        // Then, add the kernel cmdline args.
        args.push("--type=cmdline".to_string());
        for cmd in &self.cmdline {
            args.push(format!("--entry={}", cmd));
        }

        // Then, add the bootfs files.
        args.push("--files".to_string());
        args.push(bootfs_manifest_path.to_string());

        // Instead of supplying the devmgr_config.txt file, we could use boot args. This is disabled
        // by default, in order to allow for binary diffing the ZBI to the in-tree built ZBI.
        if let Some(boot_args_path) = boot_args_path {
            let boot_args_path = boot_args_path
                .as_ref()
                .to_str()
                .ok_or(anyhow!("Boot args path is not valid UTF-8"))?;
            args.push("--type=image_args".to_string());
            args.push(format!("--entry={}", boot_args_path));
        }

        // Set the compression level for bootfs files.
        if let Some(compression) = &self.compression {
            args.push(format!("--compressed={}", compression));
        }

        // Set the output file to write.
        args.push(format!("--output={}", output_path));

        // Create an output manifest that describes the contents of the built ZBI.
        if let Some(output_manifest) = &self.output_manifest {
            args.push(format!(
                "--json-output={}",
                output_manifest
                    .to_str()
                    .ok_or(anyhow!("Output manifest path is not valid UTF-8"))?
            ));
        }
        Ok(args)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::OsString;
    use std::os::unix::ffi::OsStringExt;

    #[test]
    fn bootfs_manifest_empty() {
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
    fn bootfs_manifest_invalid_utf8_source() {
        let mut builder = ZbiBuilder::default();
        let mut output: Vec<u8> = Vec::new();

        let invalid_source = OsString::from_vec(b"invalid\xe7".to_vec());
        builder.add_bootfs_file(invalid_source, "lib/file1");
        assert!(builder.write_bootfs_manifest(&mut output).is_err());
    }

    #[test]
    fn bootfs_manifest() {
        let mut builder = ZbiBuilder::default();
        let mut output: Vec<u8> = Vec::new();

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
        let builder = ZbiBuilder::default();
        assert!(builder.build_zbi_args("bootfs", Some("bootargs"), "output").is_err());
    }

    #[test]
    fn zbi_args_with_kernel() {
        let mut builder = ZbiBuilder::default();
        builder.set_kernel("path/to/kernel");
        let args = builder.build_zbi_args("bootfs", Some("bootargs"), "output").unwrap();
        assert_eq!(
            args,
            [
                "--type=container",
                "path/to/kernel",
                "--type=cmdline",
                "--files",
                "bootfs",
                "--type=image_args",
                "--entry=bootargs",
                "--output=output",
            ]
        );
    }

    #[test]
    fn zbi_args_with_cmdline() {
        let mut builder = ZbiBuilder::default();
        builder.set_kernel("path/to/kernel");
        builder.add_cmdline_arg("cmd-arg1");
        builder.add_cmdline_arg("cmd-arg2");
        let args = builder.build_zbi_args("bootfs", Some("bootargs"), "output").unwrap();
        assert_eq!(
            args,
            [
                "--type=container",
                "path/to/kernel",
                "--type=cmdline",
                "--entry=cmd-arg1",
                "--entry=cmd-arg2",
                "--files",
                "bootfs",
                "--type=image_args",
                "--entry=bootargs",
                "--output=output",
            ]
        );
    }

    #[test]
    fn zbi_args_without_boot_args() {
        let mut builder = ZbiBuilder::default();
        builder.set_kernel("path/to/kernel");
        builder.add_cmdline_arg("cmd-arg1");
        builder.add_cmdline_arg("cmd-arg2");
        let args = builder.build_zbi_args("bootfs", None::<String>, "output").unwrap();
        assert_eq!(
            args,
            [
                "--type=container",
                "path/to/kernel",
                "--type=cmdline",
                "--entry=cmd-arg1",
                "--entry=cmd-arg2",
                "--files",
                "bootfs",
                "--output=output",
            ]
        );
    }

    #[test]
    fn zbi_args_with_compression() {
        let mut builder = ZbiBuilder::default();
        builder.set_kernel("path/to/kernel");
        builder.add_cmdline_arg("cmd-arg1");
        builder.add_cmdline_arg("cmd-arg2");
        builder.set_compression("zstd.max");
        let args = builder.build_zbi_args("bootfs", None::<String>, "output").unwrap();
        assert_eq!(
            args,
            [
                "--type=container",
                "path/to/kernel",
                "--type=cmdline",
                "--entry=cmd-arg1",
                "--entry=cmd-arg2",
                "--files",
                "bootfs",
                "--compressed=zstd.max",
                "--output=output",
            ]
        );
    }

    #[test]
    fn zbi_args_with_manifest() {
        let mut builder = ZbiBuilder::default();
        builder.set_kernel("path/to/kernel");
        builder.add_cmdline_arg("cmd-arg1");
        builder.add_cmdline_arg("cmd-arg2");
        builder.set_output_manifest("path/to/manifest");
        let args = builder.build_zbi_args("bootfs", None::<String>, "output").unwrap();
        assert_eq!(
            args,
            [
                "--type=container",
                "path/to/kernel",
                "--type=cmdline",
                "--entry=cmd-arg1",
                "--entry=cmd-arg2",
                "--files",
                "bootfs",
                "--output=output",
                "--json-output=path/to/manifest",
            ]
        );
    }
}
