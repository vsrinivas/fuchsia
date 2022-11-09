// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A builder for the FVM that accepts a few attributes and optionally a list of filesystems.
///
/// ```
/// let slice_size: u64 = 1;
/// let compress = true;
/// let builder = FvmBuilder::new(
///     fvm_tool,
///     "path/to/output.blk",
///     slice_size,
///     compress,
///     Fvm::Standard {
///         resize_image_file_to_fit: false,
///         truncate_to_length: false,
///     },
/// );
/// builder.filesystem(Filesystem::BlobFS {
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
use assembly_tool::Tool;
use camino::{Utf8Path, Utf8PathBuf};
use serde::{Deserialize, Serialize};

/// The FVM builder.
pub struct FvmBuilder {
    /// The fvm host tool.
    tool: Box<dyn Tool>,
    /// The path to write the FVM to.
    output: Utf8PathBuf,
    /// The size of a slice for the FVM.
    slice_size: u64,
    /// Whether to compress the FVM.
    compress: bool,
    /// The type of FVM to generate.
    fvm_type: FvmType,
    /// A list of filesystems to add to the FVM.
    filesystems: Vec<Filesystem>,
}

/// The type of the FVM to generate and all information required to build that type.
pub enum FvmType {
    /// A plain, non-sparse FVM.
    Standard {
        /// Shrink the FVM to fit exactly the contents.
        resize_image_file_to_fit: bool,
        /// After the optional resize, truncate the file to this length.
        truncate_to_length: Option<u64>,
    },
    /// A sparse FVM that is typically used for paving.
    Sparse {
        /// The maximum disk size for the sparse FVM.
        /// The build will fail if the sparse FVM is larger than this.
        max_disk_size: Option<u64>,
    },
}

/// A filesystem to add to the FVM.
#[derive(Clone)]
pub enum Filesystem {
    /// A blobfs filesystem.
    BlobFS {
        /// The path to the filesystem block file on the host.
        path: Utf8PathBuf,
        /// The attributes of the filesystem to create.
        attributes: FilesystemAttributes,
    },
    /// A minfs filesystem.
    MinFS {
        /// The path to the filesystem block file on the host.
        path: Utf8PathBuf,
        /// The attributes of the filesystem to create.
        attributes: FilesystemAttributes,
    },
    /// An empty minfs filesystem that will be formatted when Fuchsia boots.
    EmptyMinFS,
    /// An empty minfs filesystem reserved for account data.
    EmptyAccount,
    /// Reserved slices for future use.
    Reserved {
        /// The number of slices to reserve.
        slices: u64,
    },
}

/// Attributes common to all filesystems.
#[derive(Clone, Debug, PartialEq, Deserialize, Serialize)]
pub struct FilesystemAttributes {
    /// The name of the partition. Typically "blob" or "data".
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
        tool: Box<dyn Tool>,
        output: impl AsRef<Utf8Path>,
        slice_size: u64,
        compress: bool,
        fvm_type: FvmType,
    ) -> Self {
        Self {
            tool,
            output: output.as_ref().to_path_buf(),
            slice_size,
            compress,
            fvm_type,
            filesystems: Vec::<Filesystem>::new(),
        }
    }

    /// Set the output path.
    pub fn output(&mut self, output: impl AsRef<Utf8Path>) {
        self.output = output.as_ref().to_path_buf();
    }

    /// Add a `filesystem` to the FVM.
    pub fn filesystem(&mut self, filesystem: Filesystem) {
        self.filesystems.push(filesystem);
    }

    /// Build the FVM.
    pub fn build(&self) -> Result<()> {
        let args = self.build_args().context("building fvm arguments")?;
        self.tool.run(&args)
    }

    /// Build the arguments to pass to the fvm tool.
    fn build_args(&self) -> Result<Vec<String>> {
        let mut args: Vec<String> = Vec::new();
        args.push(self.output.to_string());

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

        // Append the arguments that specify a filesystem to add to the FVM.
        fn append_filesystem(
            args: &mut Vec<String>,
            path: impl AsRef<str>,
            attributes: &FilesystemAttributes,
        ) {
            maybe_append_value(args, &attributes.name, Some(path.as_ref()));
            maybe_append_value(args, "minimum-inodes", attributes.minimum_inodes);
            maybe_append_value(args, "minimum-data-bytes", attributes.minimum_data_bytes);
            maybe_append_value(args, "maximum-bytes", attributes.maximum_bytes);
        }

        // Append the type-specific args.
        match &self.fvm_type {
            FvmType::Standard { resize_image_file_to_fit, truncate_to_length } => {
                args.push("create".to_string());
                if *resize_image_file_to_fit {
                    args.push("--resize-image-file-to-fit".to_string());
                }
                maybe_append_value(&mut args, "length", truncate_to_length.as_ref());
            }
            FvmType::Sparse { max_disk_size } => {
                args.push("sparse".to_string());
                maybe_append_value(&mut args, "max-disk-size", max_disk_size.as_ref());
            }
        }

        // Append the common args.
        maybe_append_value(&mut args, "slice", Some(self.slice_size.to_string()));
        if self.compress {
            maybe_append_value(&mut args, "compress", Some("lz4"));
        }

        // Append the filesystem args.
        for fs in &self.filesystems {
            match fs {
                Filesystem::BlobFS { path, attributes } => {
                    append_filesystem(&mut args, path.to_string(), attributes);
                }
                Filesystem::MinFS { path, attributes } => {
                    append_filesystem(&mut args, path.to_string(), attributes);
                }
                Filesystem::EmptyMinFS => {
                    args.push("--with-empty-minfs".to_string());
                }
                Filesystem::EmptyAccount => {
                    args.push("--with-empty-account-partition".to_string());
                }
                Filesystem::Reserved { slices } => {
                    args.push("--reserve-slices".to_string());
                    args.push(slices.to_string());
                }
            }
        }

        Ok(args)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assembly_tool::testing::FakeToolProvider;
    use assembly_tool::ToolProvider;

    fn default_standard() -> FvmType {
        FvmType::Standard { resize_image_file_to_fit: false, truncate_to_length: None }
    }

    #[test]
    fn standard_args_no_filesystem() {
        let tools = FakeToolProvider::default();
        let fvm_tool = tools.get_tool("fvm").unwrap();
        let builder = FvmBuilder::new(fvm_tool, "mypath", 1, false, default_standard());
        let args = builder.build_args().unwrap();
        assert_eq!(vec!["mypath", "create", "--slice", "1"], args);
    }

    #[test]
    fn standard_args_with_filesystem() {
        let tools = FakeToolProvider::default();
        let fvm_tool = tools.get_tool("fvm").unwrap();
        let mut builder = FvmBuilder::new(fvm_tool, "mypath", 1, false, default_standard());
        builder.filesystem(Filesystem::BlobFS {
            path: Utf8PathBuf::from("path/to/blob.blk"),
            attributes: FilesystemAttributes {
                name: "blob".to_string(),
                minimum_inodes: Some(100),
                minimum_data_bytes: Some(200),
                maximum_bytes: Some(300),
            },
        });
        let args = builder.build_args().unwrap();

        assert_eq!(
            vec![
                "mypath",
                "create",
                "--slice",
                "1",
                "--blob",
                "path/to/blob.blk",
                "--minimum-inodes",
                "100",
                "--minimum-data-bytes",
                "200",
                "--maximum-bytes",
                "300",
            ],
            args
        );
    }

    #[test]
    fn standard_args_compressed() {
        let tools = FakeToolProvider::default();
        let fvm_tool = tools.get_tool("fvm").unwrap();
        let builder = FvmBuilder::new(fvm_tool, "mypath", 1, true, default_standard());
        let args = builder.build_args().unwrap();

        assert_eq!(vec!["mypath", "create", "--slice", "1", "--compress", "lz4",], args);
    }

    #[test]
    fn standard_args_with_empty_account() {
        let tools = FakeToolProvider::default();
        let fvm_tool = tools.get_tool("fvm").unwrap();
        let mut builder = FvmBuilder::new(fvm_tool, "mypath", 1, false, default_standard());
        builder.filesystem(Filesystem::EmptyAccount);
        let args = builder.build_args().unwrap();

        assert_eq!(
            vec!["mypath", "create", "--slice", "1", "--with-empty-account-partition",],
            args
        );
    }

    #[test]
    fn standard_args_with_reserved() {
        let tools = FakeToolProvider::default();
        let fvm_tool = tools.get_tool("fvm").unwrap();
        let mut builder = FvmBuilder::new(fvm_tool, "mypath", 1, false, default_standard());
        builder.filesystem(Filesystem::Reserved { slices: 500 });
        let args = builder.build_args().unwrap();

        assert_eq!(vec!["mypath", "create", "--slice", "1", "--reserve-slices", "500",], args);
    }

    #[test]
    fn standard_args_resize_and_truncate() {
        let tools = FakeToolProvider::default();
        let fvm_tool = tools.get_tool("fvm").unwrap();
        let builder = FvmBuilder::new(
            fvm_tool,
            "mypath",
            1,
            false,
            FvmType::Standard { resize_image_file_to_fit: true, truncate_to_length: Some(500) },
        );
        let args = builder.build_args().unwrap();

        assert_eq!(
            vec![
                "mypath",
                "create",
                "--resize-image-file-to-fit",
                "--length",
                "500",
                "--slice",
                "1",
            ],
            args
        );
    }

    #[test]
    fn sparse_args_no_max_size() {
        let tools = FakeToolProvider::default();
        let fvm_tool = tools.get_tool("fvm").unwrap();
        let builder =
            FvmBuilder::new(fvm_tool, "mypath", 1, false, FvmType::Sparse { max_disk_size: None });
        let args = builder.build_args().unwrap();
        assert_eq!(vec!["mypath", "sparse", "--slice", "1"], args);
    }

    #[test]
    fn sparse_args_max_size() {
        let tools = FakeToolProvider::default();
        let fvm_tool = tools.get_tool("fvm").unwrap();
        let builder = FvmBuilder::new(
            fvm_tool,
            "mypath",
            1,
            false,
            FvmType::Sparse { max_disk_size: Some(500) },
        );
        let args = builder.build_args().unwrap();
        assert_eq!(vec!["mypath", "sparse", "--max-disk-size", "500", "--slice", "1",], args);
    }

    #[test]
    fn sparse_blob_args() {
        let tools = FakeToolProvider::default();
        let fvm_tool = tools.get_tool("fvm").unwrap();
        let mut builder =
            FvmBuilder::new(fvm_tool, "mypath", 1, false, FvmType::Sparse { max_disk_size: None });
        builder.filesystem(Filesystem::EmptyMinFS);
        let args = builder.build_args().unwrap();
        assert_eq!(vec!["mypath", "sparse", "--slice", "1", "--with-empty-minfs",], args);
    }
}
