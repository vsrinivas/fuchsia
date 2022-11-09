// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use assembly_tool::Tool;
use camino::Utf8PathBuf;

/// A builder that receives a sparse FVM, and prepares it for nand flashing.
///
/// ```
/// let builder = NandFvmBuilder {
///     output: Utf8PathBuf::from("path/to/output.blk"),
///     sparse_blob_fvm: Utf8PathBuf::from("path/to/fvm.blob.sparse.blk"),
///     max_disk_size: None,
///     compression: None,
///     page_size: 0,
///     oob_size: 0,
///     pages_per_block: 0,
///     block_count: 0,
/// };
/// builder.build();
/// ```

pub struct NandFvmBuilder {
    /// The fvm host tool.
    pub tool: Box<dyn Tool>,
    /// The path to write the FVM to.
    pub output: Utf8PathBuf,
    /// The path to the sparse, blob-only FVM on the host.
    pub sparse_blob_fvm: Utf8PathBuf,
    /// The maximum disk size for the sparse FVM.
    /// The build will fail if the sparse FVM is larger than this.
    pub max_disk_size: Option<u64>,
    /// The compression algorithm to use.
    pub compression: Option<String>,
    /// The nand page size.
    pub page_size: u64,
    /// The out of bound size.
    pub oob_size: u64,
    /// The pages per block.
    pub pages_per_block: u64,
    /// The number of blocks.
    pub block_count: u64,
}

impl NandFvmBuilder {
    /// Build the FVM.
    pub fn build(self) -> Result<()> {
        let args = self.build_args()?;
        self.tool.run(&args)
    }

    fn build_args(&self) -> Result<Vec<String>> {
        let mut args: Vec<String> = Vec::new();
        args.push(self.output.to_string());
        args.push("ftl-raw-nand".to_string());

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

        maybe_append_value(&mut args, "nand-page-size", Some(self.page_size));
        maybe_append_value(&mut args, "nand-oob-size", Some(self.oob_size));
        maybe_append_value(&mut args, "nand-pages-per-block", Some(self.pages_per_block));
        maybe_append_value(&mut args, "nand-block-count", Some(self.block_count));
        maybe_append_value(&mut args, "max-disk-size", self.max_disk_size);
        maybe_append_value(&mut args, "compress", self.compression.as_ref());

        // A quirk of the FVM tool means the sparse argument *must* go last.
        maybe_append_value(&mut args, "sparse", Some(self.sparse_blob_fvm.to_string()));

        Ok(args)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assembly_tool::testing::FakeToolProvider;
    use assembly_tool::ToolProvider;

    #[test]
    fn nand_args() {
        let tools = FakeToolProvider::default();
        let fvm_tool = tools.get_tool("fvm").unwrap();
        let builder = NandFvmBuilder {
            tool: fvm_tool,
            output: "mypath".into(),
            sparse_blob_fvm: "sparsepath".into(),
            max_disk_size: Some(500),
            compression: Some("supercompress".into()),
            page_size: 1,
            oob_size: 2,
            pages_per_block: 3,
            block_count: 4,
        };
        let args = builder.build_args().unwrap();
        assert_eq!(
            args,
            [
                "mypath",
                "ftl-raw-nand",
                "--nand-page-size",
                "1",
                "--nand-oob-size",
                "2",
                "--nand-pages-per-block",
                "3",
                "--nand-block-count",
                "4",
                "--max-disk-size",
                "500",
                "--compress",
                "supercompress",
                "--sparse",
                "sparsepath",
            ]
        );
    }
}
