// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A builder for the FVM that accepts a few attributes and optionally a list of filesystems.
///
/// ```
/// let slice_size: u64 = 1;
/// let reserved_slices: u64 = 2;
/// let builder = FvmBuilder::new("path/to/output.blk", slice_size, reserved_slices);
/// builder.filesystem(Filesystem {
///     path: "path/to/blob.blk",
///     attributes: FilesystemAttributes {
///         name: "blob",
///         minimum_inodes: 100,
///         minimum_data_bytes: 200,
///         maximum_bytes: 200
///     },
/// });
/// builder.build();
/// ```
///
use anyhow::{Context, Result};
use assembly_util::PathToStringExt;
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

/// The FVM builder.
pub struct FvmBuilder {
    /// The path to write the FVM to.
    output: PathBuf,
    /// The size of a slice for the FVM.
    slice_size: u64,
    /// The number of slices to reserve in the FVM.
    reserved_slices: u64,
    /// A list of filesystems to add to the FVM.
    filesystems: Vec<Filesystem>,
}

impl FvmBuilder {
    /// Construct a new FvmBuilder.
    pub fn new(output: impl AsRef<Path>, slice_size: u64, reserved_slices: u64) -> Self {
        Self {
            output: output.as_ref().to_path_buf(),
            slice_size,
            reserved_slices,
            filesystems: Vec::<Filesystem>::new(),
        }
    }

    /// Add a `filesystem` to the FVM.
    pub fn filesystem(&mut self, filesystem: Filesystem) {
        self.filesystems.push(filesystem);
    }

    /// Build the FVM.
    pub fn build(self) -> Result<()> {
        let args = self.build_args()?;

        // TODO(fxbug.dev/76378): Take the tool location from a config.
        let output = std::process::Command::new("host_x64/fvm").args(&args).output();
        let output = output.context("Failed to run the fvm tool")?;
        if !output.status.success() {
            anyhow::bail!(format!(
                "Failed to generate fvm with status: {}\n{}",
                output.status,
                String::from_utf8_lossy(output.stdout.as_slice())
            ));
        }

        Ok(())
    }

    /// Build the arguments to pass to the fvm tool.
    fn build_args(&self) -> Result<Vec<String>> {
        // Construct the initial args.
        let mut args = vec![
            self.output.path_to_string()?,
            "create".to_string(),
            "--slice".to_string(),
            self.slice_size.to_string(),
            "--reserve-slices".to_string(),
            self.reserved_slices.to_string(),
        ];

        // Append key and value to the `args` if the value is present.
        fn append_arg(
            args: &mut Vec<String>,
            key: impl AsRef<str>,
            value: Option<impl std::string::ToString>,
        ) {
            if let Some(value) = value {
                args.push(format!("--{}", key.as_ref()));
                args.push(value.to_string());
            }
        }

        // Append all the filesystem args.
        for fs in &self.filesystems {
            append_arg(&mut args, &fs.attributes.name, Some(&fs.path.path_to_string()?));
            append_arg(&mut args, "minimum-inodes", fs.attributes.minimum_inodes);
            append_arg(&mut args, "minimum-data-bytes", fs.attributes.minimum_data_bytes);
            append_arg(&mut args, "maximum-bytes", fs.attributes.maximum_bytes);
        }

        Ok(args)
    }
}

/// A filesystem to add to the FVM.
pub struct Filesystem {
    /// The path to the filesystem block file on the host.
    pub path: PathBuf,
    /// The attributes of the filesystem to create.
    pub attributes: FilesystemAttributes,
}

/// Attributes common to all filesystems.
#[derive(Clone, Deserialize, Serialize)]
pub struct FilesystemAttributes {
    /// The name of the filesystem. Typically "blob" or "data".
    pub name: String,
    /// The minimum number of inodes to add to the filesystem.
    pub minimum_inodes: Option<u64>,
    /// The minimum number of data bytes to set for the filesystem.
    pub minimum_data_bytes: Option<u64>,
    /// The maximum number of bytes for the filesystem.
    pub maximum_bytes: Option<u64>,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn args_no_filesystem() {
        let builder = FvmBuilder::new("mypath", 1, 2);
        let args = builder.build_args().unwrap();
        assert_eq!(args, ["mypath", "create", "--slice", "1", "--reserve-slices", "2",]);
    }

    #[test]
    fn args_with_filesystem() {
        let mut builder = FvmBuilder::new("mypath", 1, 2);
        builder.filesystem(Filesystem {
            path: PathBuf::from("path/to/file.blk"),
            attributes: FilesystemAttributes {
                name: "name".to_string(),
                minimum_inodes: Some(100),
                minimum_data_bytes: Some(200),
                maximum_bytes: Some(300),
            },
        });
        let args = builder.build_args().unwrap();

        assert_eq!(
            args,
            [
                "mypath",
                "create",
                "--slice",
                "1",
                "--reserve-slices",
                "2",
                "--name",
                "path/to/file.blk",
                "--minimum-inodes",
                "100",
                "--minimum-data-bytes",
                "200",
                "--maximum-bytes",
                "300",
            ]
        );
    }
}
