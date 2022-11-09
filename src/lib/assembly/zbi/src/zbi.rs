// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Error, Result};
use assembly_tool::Tool;
use camino::{Utf8Path, Utf8PathBuf};
use std::collections::BTreeMap;
use std::fs::File;
use std::io::Write;
use tracing::debug;

/// Builder for the Zircon Boot Image (ZBI), which takes in a kernel, BootFS, boot args, and kernel
/// command line.
pub struct ZbiBuilder {
    /// The zbi host tool.
    tool: Box<dyn Tool>,

    kernel: Option<Utf8PathBuf>,
    // Map from file destination in the ZBI to the path of the source file on the host.
    bootfs_files: BTreeMap<String, Utf8PathBuf>,
    bootargs: Vec<String>,
    cmdline: Vec<String>,

    // A ramdisk to add to the ZBI.
    ramdisk: Option<Utf8PathBuf>,

    /// optional compression to use.
    compression: Option<String>,

    /// optional output manifest file
    output_manifest: Option<Utf8PathBuf>,
}

impl ZbiBuilder {
    /// Construct a new ZbiBuilder that uses the zbi |tool|.
    pub fn new(tool: Box<dyn Tool>) -> Self {
        Self {
            tool,
            kernel: None,
            bootfs_files: BTreeMap::default(),
            bootargs: Vec::default(),
            cmdline: Vec::default(),
            ramdisk: None,
            compression: None,
            output_manifest: None,
        }
    }

    /// Set the kernel to be used.
    pub fn set_kernel(&mut self, kernel: impl Into<Utf8PathBuf>) {
        self.kernel = Some(kernel.into());
    }

    /// Add a BootFS file to the ZBI.
    pub fn add_bootfs_file(
        &mut self,
        source: impl Into<Utf8PathBuf>,
        destination: impl AsRef<str>,
    ) {
        if self.bootfs_files.contains_key(destination.as_ref()) {
            println!("Found duplicate bootfs destination: {}", destination.as_ref());
            return;
        }
        self.bootfs_files.insert(destination.as_ref().to_string(), source.into());
    }

    /// Add a boot argument to the ZBI.
    pub fn add_boot_arg(&mut self, arg: &str) {
        self.bootargs.push(arg.to_string());
    }

    /// Add a kernel command line argument.
    pub fn add_cmdline_arg(&mut self, arg: &str) {
        self.cmdline.push(arg.to_string());
    }

    /// Add a ramdisk to the ZBI.
    pub fn add_ramdisk(&mut self, source: impl Into<Utf8PathBuf>) {
        self.ramdisk = Some(source.into());
    }

    /// Set the compression to use with the ZBI.
    pub fn set_compression(&mut self, compress: impl ToString) {
        self.compression = Some(compress.to_string());
    }

    /// Set the path to an optional JSON output manifest to produce.
    pub fn set_output_manifest(&mut self, manifest: impl Into<Utf8PathBuf>) {
        self.output_manifest = Some(manifest.into());
    }

    /// Build the ZBI.
    pub fn build(&self, gendir: impl AsRef<Utf8Path>, output: impl AsRef<Utf8Path>) -> Result<()> {
        // Create the devmgr_config.
        // TODO(fxbug.dev/77387): Switch to the boot args file once we are no longer
        // comparing to the GN build.
        let devmgr_config_path = gendir.as_ref().join("devmgr_config.txt");
        let mut devmgr_config = File::create(&devmgr_config_path)
            .map_err(|e| Error::new(e).context("failed to create the devmgr config"))?;
        self.write_boot_args(&mut devmgr_config)?;

        // Create the BootFS manifest file that lists all the files to insert
        // into BootFS.
        let bootfs_manifest_path = gendir.as_ref().join("bootfs_files.list");
        let mut bootfs_manifest = File::create(&bootfs_manifest_path)
            .map_err(|e| Error::new(e).context("failed to create the bootfs manifest"))?;
        self.write_bootfs_manifest(devmgr_config_path, &mut bootfs_manifest)?;

        // Run the zbi tool to construct the ZBI.
        let zbi_args = self.build_zbi_args(&bootfs_manifest_path, None::<Utf8PathBuf>, output)?;
        debug!("ZBI command args: {:?}", zbi_args);

        self.tool.run(&zbi_args)
    }

    fn write_bootfs_manifest(
        &self,
        devmgr_config_path: impl Into<Utf8PathBuf>,
        out: &mut impl Write,
    ) -> Result<()> {
        let mut bootfs_files = self.bootfs_files.clone();
        bootfs_files.insert("config/devmgr".to_string(), devmgr_config_path.into());
        for (destination, source) in bootfs_files {
            write!(out, "{}", destination)?;
            write!(out, "=")?;
            // TODO(fxbug.dev76135): Use the zbi tool's set of valid inputs instead of constraining
            // to valid UTF-8.
            writeln!(out, "{}", source)?;
        }
        Ok(())
    }

    fn write_boot_args(&self, out: &mut impl Write) -> Result<()> {
        for arg in &self.bootargs {
            writeln!(out, "{}", arg)?;
        }
        Ok(())
    }

    fn build_zbi_args(
        &self,
        bootfs_manifest_path: impl AsRef<Utf8Path>,
        boot_args_path: Option<impl AsRef<Utf8Path>>,
        output_path: impl AsRef<Utf8Path>,
    ) -> Result<Vec<String>> {
        // Ensure a kernel is supplied.
        let kernel = &self.kernel.as_ref().ok_or(anyhow!("No kernel image supplied"))?;

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
        args.push(bootfs_manifest_path.as_ref().to_string());

        // Instead of supplying the devmgr_config.txt file, we could use boot args. This is disabled
        // by default, in order to allow for binary diffing the ZBI to the in-tree built ZBI.
        if let Some(boot_args_path) = boot_args_path {
            let boot_args_path = boot_args_path.as_ref().to_string();
            args.push("--type=image_args".to_string());
            args.push(format!("--entry={}", boot_args_path));
        }

        // Add the ramdisk if needed.
        if let Some(ramdisk) = &self.ramdisk {
            args.push("--type=ramdisk".to_string());
            if let Some(compression) = &self.compression {
                args.push(format!("--compress={}", compression));
            }
            args.push(ramdisk.to_string());
        }

        // Set the compression level for bootfs files.
        if let Some(compression) = &self.compression {
            args.push(format!("--compressed={}", compression));
        }

        // Set the output file to write.
        args.push("--output".into());
        args.push(output_path.as_ref().to_string());

        // Create an output manifest that describes the contents of the built ZBI.
        if let Some(output_manifest) = &self.output_manifest {
            args.push("--json-output".into());
            args.push(output_manifest.to_string());
        }
        Ok(args)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assembly_tool::testing::FakeToolProvider;
    use assembly_tool::ToolProvider;

    #[test]
    fn bootfs_manifest_devmgr_only() {
        let tools = FakeToolProvider::default();
        let zbi_tool = tools.get_tool("zbi").unwrap();
        let mut builder = ZbiBuilder::new(zbi_tool);
        let mut output: Vec<u8> = Vec::new();

        builder.write_bootfs_manifest("devmgr_config.txt", &mut output).unwrap();
        let output_str = String::from_utf8(output).unwrap();
        assert_eq!(output_str, "config/devmgr=devmgr_config.txt\n".to_string());

        let mut output: Vec<u8> = Vec::new();
        builder.add_bootfs_file("path/to/file2", "bin/file2");
        builder.add_bootfs_file("path/to/file1", "lib/file1");
        builder.write_bootfs_manifest("devmgr_config.txt", &mut output).unwrap();
        let output_str = String::from_utf8(output).unwrap();
        assert_eq!(
            output_str,
            "bin/file2=path/to/file2\nconfig/devmgr=devmgr_config.txt\nlib/file1=path/to/file1\n"
                .to_string()
        );
    }

    #[test]
    fn bootfs_manifest() {
        let tools = FakeToolProvider::default();
        let zbi_tool = tools.get_tool("zbi").unwrap();
        let mut builder = ZbiBuilder::new(zbi_tool);
        let mut output: Vec<u8> = Vec::new();

        builder.add_bootfs_file("path/to/file2", "bin/file2");
        builder.add_bootfs_file("path/to/file1", "lib/file1");
        builder.write_bootfs_manifest("devmgr_config.txt", &mut output).unwrap();
        assert_eq!(
            output,
            b"bin/file2=path/to/file2\nconfig/devmgr=devmgr_config.txt\nlib/file1=path/to/file1\n"
        );
    }

    #[test]
    fn boot_args() {
        let tools = FakeToolProvider::default();
        let zbi_tool = tools.get_tool("zbi").unwrap();
        let mut builder = ZbiBuilder::new(zbi_tool);
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
        let tools = FakeToolProvider::default();
        let zbi_tool = tools.get_tool("zbi").unwrap();
        let builder = ZbiBuilder::new(zbi_tool);
        assert!(builder.build_zbi_args("bootfs", Some("bootargs"), "output").is_err());
    }

    #[test]
    fn zbi_args_with_kernel() {
        let tools = FakeToolProvider::default();
        let zbi_tool = tools.get_tool("zbi").unwrap();
        let mut builder = ZbiBuilder::new(zbi_tool);
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
                "--output",
                "output",
            ]
        );
    }

    #[test]
    fn zbi_args_with_cmdline() {
        let tools = FakeToolProvider::default();
        let zbi_tool = tools.get_tool("zbi").unwrap();
        let mut builder = ZbiBuilder::new(zbi_tool);
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
                "--output",
                "output",
            ]
        );
    }

    #[test]
    fn zbi_args_without_boot_args() {
        let tools = FakeToolProvider::default();
        let zbi_tool = tools.get_tool("zbi").unwrap();
        let mut builder = ZbiBuilder::new(zbi_tool);
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
                "--output",
                "output",
            ]
        );
    }

    #[test]
    fn zbi_args_with_compression() {
        let tools = FakeToolProvider::default();
        let zbi_tool = tools.get_tool("zbi").unwrap();
        let mut builder = ZbiBuilder::new(zbi_tool);
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
                "--output",
                "output",
            ]
        );
    }

    #[test]
    fn zbi_args_with_manifest() {
        let tools = FakeToolProvider::default();
        let zbi_tool = tools.get_tool("zbi").unwrap();
        let mut builder = ZbiBuilder::new(zbi_tool);
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
                "--output",
                "output",
                "--json-output",
                "path/to/manifest",
            ]
        );
    }
}
