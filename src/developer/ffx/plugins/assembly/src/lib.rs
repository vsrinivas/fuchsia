// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context;
use errors::FfxError;
use {anyhow::Result, ffx_assembly_args::*, ffx_core::ffx_plugin};

mod base_package;
mod blob_json_generator;
mod blobfs;
mod extra_hash_descriptor;
mod fvm;
mod operations;
mod util;
mod zbi;
use assembly_components as _;

pub mod vbmeta;
pub mod vfs;

#[ffx_plugin("assembly_enabled")]
pub async fn assembly(cmd: AssemblyCommand) -> Result<()> {
    // Dispatch to the correct operation based on the command.
    // The context() is used to display which operation failed in the event of
    // an error.
    match cmd.op_class {
        OperationClass::CreateSystem(args) => {
            operations::create_system::create_system(args).context("Create System")
        }
        OperationClass::CreateUpdate(args) => {
            operations::create_update::create_update(args).context("Create Update Package")
        }
        OperationClass::CreateFlashManifest(args) => {
            operations::create_flash_manifest::create_flash_manifest(args)
                .context("Create Flash Manifest")
        }
        OperationClass::Product(args) => {
            operations::product::assemble(args).context("Product Assembly")
        }
        OperationClass::SizeCheck(args) => match args.op_class {
            SizeCheckOperationClass::Package(args) => {
                operations::size_check_package::verify_package_budgets(args)
                    .context("Package size checker")
            }
            SizeCheckOperationClass::Product(args) => {
                // verify_product_budgets() returns a boolean that indicates whether the budget was
                // exceeded or not. We don't intend to fail the build when budgets are exceeded so the
                // returned value is dropped.
                operations::size_check_product::verify_product_budgets(args)
                    .context("Product size checker")
                    .map(|_| ())
            }
        },
    }
    .map_err(flatten_error_sources)
}

/// Wrap the anyhow::Error in an FfxError that's into'd an anyhow::Error
/// again.  This removes the "BUG: An internal error occurred" line from the
/// output, creating a multiline display of the context() entries.
///
/// The multi-line format which enumerates each of the intervening Context
/// lines is used so that there is enough information in the error to
/// understand what failed and why, as errors such as "No such file or
/// directory" isn't terribly useful, in an operation that may need to open
/// 100s of files, which may have been listed inside a file, which is listed
/// in a file that's given on the command line.
///
/// The error string constructed here is:
///  ```
///  <First Context> Failed:
///      1.  <Next Context>
///      2.  <Next Context>
///      ...
///      N.  <root error>
///  ```
///
///
///
fn flatten_error_sources(e: anyhow::Error) -> anyhow::Error {
    FfxError::Error(
        anyhow::anyhow!(
            "{} Failed{}",
            e,
            e.chain()
                .skip(1)
                .enumerate()
                .map(|(i, e)| format!("\n  {: >3}.  {}", i + 1, e))
                .collect::<Vec<String>>()
                .concat()
        ),
        -1,
    )
    .into()
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::anyhow;

    fn make_flattened_error_display_string(e: anyhow::Error) -> String {
        format!("{}", flatten_error_sources(e))
    }

    #[test]
    fn test_error_source_flatten_no_context() {
        assert_eq!(
            "Some Operation Failed",
            make_flattened_error_display_string(anyhow!("Some Operation"))
        );
    }

    // The order of context's is "in-side-out", the root-most error is
    // created first, and then the context() is attached on all of the
    // returned values, so they are created in the opposite order that they
    // are displayed.

    #[test]
    fn test_error_source_flatten_one_context() {
        let expected = "Some Other Operation Failed\n    1.  some failure";
        let error = anyhow!("some failure");
        let error = error.context("Some Other Operation");
        assert_eq!(expected, make_flattened_error_display_string(error));
    }

    #[test]
    fn test_error_source_flatten_two_contexts() {
        let expected = "Some Operation Failed\n    1.  some context\n    2.  some failure";
        let error = anyhow!("some failure");
        let error = error.context("some context");
        let error = error.context("Some Operation");
        assert_eq!(expected, make_flattened_error_display_string(error));
    }

    #[test]
    fn test_error_source_flatten_three_contexts() {
        let expected = r#"Some Operation Failed
    1.  some context
    2.  more context
    3.  some failure"#;
        let error = anyhow!("some failure")
            .context("more context")
            .context("some context")
            .context("Some Operation");
        assert_eq!(expected, make_flattened_error_display_string(error));
    }
}
