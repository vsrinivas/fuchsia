// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A builder for the FVM that accepts a few attributes and optionally a list of filesystems.
///
/// ```
/// let slice_size: u64 = 1;
/// let reserved_slices: u64 = 2;
/// let builder = FvmBuilder::new(
///     "path/to/tool/fvm",
///     "path/to/output.blk",
///     slice_size,
///     reserved_slices,
/// );
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
    /// Path to the fvm host tool.
    tool: PathBuf,
    /// The path to write the FVM to.
    output: PathBuf,
    /// The size of a slice for the FVM.
    slice_size: u64,
    /// The number of slices to reserve in the FVM.
    reserved_slices: u64,
    /// The maximum disk size for the sparse FVM.
    /// The build will fail if the sparse FVM is larger than this.
    max_disk_size: Option<u64>,
    /// The type of FVM to generate.
    fvm_type: FvmType,
    /// A list of filesystems to add to the FVM.
    filesystems: Vec<Filesystem>,
}

/// The type of the FVM to generate and all information required to build that type.
pub enum FvmType {
    /// A plain, non-sparse FVM.
    Default,
    /// A sparse FVM that is typically used for paving.
    Sparse {
        /// Whether to insert an empty minfs that will be formatted on boot.
        empty_minfs: bool,
    },
    /// A sparse FVM that is formatted for flashing an EMMC.
    Emmc {
        /// The compression algorithm to use.
        compression: String,
        /// The length of the FVM to generate.
        length: u64,
    },
}

/// A filesystem to add to the FVM.
#[derive(Clone)]
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

impl FvmBuilder {
    /// Construct a new FvmBuilder.
    pub fn new(
        tool: impl AsRef<Path>,
        output: impl AsRef<Path>,
        slice_size: u64,
        reserved_slices: u64,
        max_disk_size: Option<u64>,
        fvm_type: FvmType,
    ) -> Self {
        Self {
            tool: tool.as_ref().to_path_buf(),
            output: output.as_ref().to_path_buf(),
            slice_size,
            reserved_slices,
            max_disk_size,
            fvm_type,
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
        let output = std::process::Command::new(&self.tool).args(&args).output();
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
        let mut args: Vec<String> = Vec::new();
        args.push(self.output.path_to_string()?);

        // Append key and value to the `args` if the value is present.
        fn maybe_append_value(
            args: &mut Vec<String>,
            key: impl AsRef<str>,
            value: Option<impl std::string::ToString>,
        ) {
            if let Some(value) = value {
                args.push(format!("--{}", key.as_ref()));
                args.push(value.to_string());
            }
        }

        // Append the type-specific args.
        match &self.fvm_type {
            FvmType::Default => {
                args.push("create".to_string());
            }
            FvmType::Sparse { empty_minfs } => {
                args.push("sparse".to_string());
                maybe_append_value(&mut args, "compress", Some("lz4"));
                maybe_append_value(&mut args, "max-disk-size", self.max_disk_size);

                if *empty_minfs {
                    args.push("--with-empty-minfs".to_string());
                }
            }
            FvmType::Emmc { compression, length } => {
                args.push("create".to_string());
                args.push("--resize-image-file-to-fit".to_string());
                maybe_append_value(&mut args, "length", Some(length));
                let compression = match compression.as_str() {
                    "none" => None,
                    c => Some(c),
                };
                maybe_append_value(&mut args, "compress", compression);
            }
        }

        // Append the common args.
        maybe_append_value(&mut args, "slice", Some(self.slice_size.to_string()));
        maybe_append_value(&mut args, "reserve-slices", Some(self.reserved_slices.to_string()));

        // Append the filesystem args.
        for fs in &self.filesystems {
            maybe_append_value(&mut args, &fs.attributes.name, Some(&fs.path.path_to_string()?));
            maybe_append_value(&mut args, "minimum-inodes", fs.attributes.minimum_inodes);
            maybe_append_value(&mut args, "minimum-data-bytes", fs.attributes.minimum_data_bytes);
            maybe_append_value(&mut args, "maximum-bytes", fs.attributes.maximum_bytes);
        }

        Ok(args)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_args_no_filesystem() {
        let builder = FvmBuilder::new("fvm", "mypath", 1, 2, None, FvmType::Default);
        let args = builder.build_args().unwrap();
        assert_eq!(args, ["mypath", "create", "--slice", "1", "--reserve-slices", "2"]);
    }

    #[test]
    fn default_args_with_filesystem() {
        let mut builder = FvmBuilder::new("fvm", "mypath", 1, 2, None, FvmType::Default);
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

    #[test]
    fn sparse_args_no_max_size() {
        let builder =
            FvmBuilder::new("fvm", "mypath", 1, 2, None, FvmType::Sparse { empty_minfs: false });
        let args = builder.build_args().unwrap();
        assert_eq!(
            args,
            ["mypath", "sparse", "--compress", "lz4", "--slice", "1", "--reserve-slices", "2"]
        );
    }

    #[test]
    fn sparse_args_max_size() {
        let builder = FvmBuilder::new(
            "fvm",
            "mypath",
            1,
            2,
            Some(500),
            FvmType::Sparse { empty_minfs: false },
        );
        let args = builder.build_args().unwrap();
        assert_eq!(
            args,
            [
                "mypath",
                "sparse",
                "--compress",
                "lz4",
                "--max-disk-size",
                "500",
                "--slice",
                "1",
                "--reserve-slices",
                "2"
            ]
        );
    }

    #[test]
    fn sparse_blob_args() {
        let builder =
            FvmBuilder::new("fvm", "mypath", 1, 2, None, FvmType::Sparse { empty_minfs: true });
        let args = builder.build_args().unwrap();
        assert_eq!(
            args,
            [
                "mypath",
                "sparse",
                "--compress",
                "lz4",
                "--with-empty-minfs",
                "--slice",
                "1",
                "--reserve-slices",
                "2"
            ]
        );
    }

    #[test]
    fn emmc_args() {
        let builder = FvmBuilder::new(
            "fvm",
            "mypath",
            1,
            2,
            None,
            FvmType::Emmc { length: 500, compression: "supercompress".to_string() },
        );
        let args = builder.build_args().unwrap();
        assert_eq!(
            args,
            [
                "mypath",
                "create",
                "--resize-image-file-to-fit",
                "--length",
                "500",
                "--compress",
                "supercompress",
                "--slice",
                "1",
                "--reserve-slices",
                "2"
            ]
        );
    }
}
