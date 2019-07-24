// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use sdk_metadata::JsonObject;

use crate::app::{Error, Result};
use crate::file_provider::{merge_files, FileProvider};
use crate::tarball::{InputTarball, OutputTarball, TarballContent};

/// Merges an element that should not have any variation.
pub fn merge_immutable_element<M, F, B, C, O>(
    meta_path: &str, base: &B, complement: &C, output: &mut O,
) -> Result<()>
where
    M: FileProvider + Eq + JsonObject,
    F: TarballContent,
    B: InputTarball<F>,
    C: InputTarball<F>,
    O: OutputTarball<F>,
{
    let base_meta: M = base.get_metadata::<M>(meta_path)?;
    let complement_meta: M = complement.get_metadata::<M>(meta_path)?;
    merge_files(&base_meta, base, &complement_meta, complement, output)?;
    if base_meta != complement_meta {
        return Err(Error::MetaFilesDiffer)?;
    }
    output.write_json(meta_path, &base_meta)?;
    Ok(())
}

#[cfg(test)]
#[macro_use]
pub mod tests {
    use crate::testing::MockInputTarball;

    pub fn create_source_tarball(meta: &str, data: &str, files: &Vec<&str>) -> MockInputTarball {
        let result = MockInputTarball::new();
        result.add(meta, data);
        for file in files {
            let content = format!("I am {}", file);
            result.add(file, &content);
        }
        result
    }

    /// Generates a test verifying that an immutable element merges properly.
    macro_rules! test_merge_immutable_success {
        (
            name = $name:ident,
            merge = $merge:ident,
            meta = $meta:expr,
            data = $data:expr,
            files = [$( $file:expr ),* $(,)?],
        ) => {
            #[test]
            pub fn $name() {
                use crate::testing::MockOutputTarball;

                let meta = $meta;
                let data = $data;
                let files: Vec<&str> = vec![$($file),*];
                let base = $crate::immutable::tests::create_source_tarball(meta, &data, &files);
                let complement = base.clone();
                let mut output = MockOutputTarball::new();
                $merge(meta, &base, &complement, &mut output).expect("Merge should not fail!");
                output.assert_has_file(meta);
                for file in files {
                    output.assert_has_file(file);
                }
            }
        };
    }

    /// Generates a test verifying that different versions of an immutable element fail to merge.
    macro_rules! test_merge_immutable_failure {
        (
            name = $name:ident,
            merge = $merge:ident,
            meta = $meta:expr,
            base_data = $base_data:expr,
            base_files = [$( $base_file:expr ),* $(,)?],
            complement_data = $complement_data:expr,
            complement_files = [$( $complement_file:expr ),* $(,)?],
        ) => {
            #[test]
            pub fn $name() {
                use crate::testing::MockOutputTarball;

                let base_files: Vec<&str> = vec![$($base_file),*];
                let complement_files: Vec<&str> = vec![$($complement_file),*];
                let meta = $meta;
                let base = $crate::immutable::tests::create_source_tarball(meta, &$base_data, &base_files);
                let complement = $crate::immutable::tests::create_source_tarball(meta, &$complement_data, &complement_files);
                let mut output = MockOutputTarball::new();
                assert!(
                    $merge(meta, &base, &complement, &mut output).is_err(),
                    "Merge should have failed due to metadata mismatch"
                );
            }
        };
    }

    // And now, some actual tests of the merge_immutable_element method.

    use sdk_metadata::testing::TestObject;

    use crate::app::Result;
    use crate::file_provider::FileProvider;
    use crate::tarball::{InputTarball, OutputTarball};

    use super::*;

    impl FileProvider for TestObject {
        fn get_common_files(&self) -> Vec<String> {
            self.files.clone()
        }
    }

    fn merge_foobar(
        meta_path: &str, base: &impl InputTarball<String>, complement: &impl InputTarball<String>,
        output: &mut impl OutputTarball<String>,
    ) -> Result<()> {
        merge_immutable_element::<TestObject, _, _, _, _>(meta_path, base, complement, output)
    }

    test_merge_immutable_success! {
        name = test_merge,
        merge = merge_foobar,
        meta = "foobar.json",
        data = r#"
        {
            "name": "foobar",
            "files": [
                "a/file.ext"
            ]
        }
        "#,
        files = [
            "a/file.ext",
        ],
    }

    test_merge_immutable_failure! {
        name = test_merge_failed,
        merge = merge_foobar,
        meta = "foobar.json",
        base_data = r#"
        {
            "name": "foobar",
            "files": [
                "a/file.ext"
            ]
        }
        "#,
        base_files = [
            "a/file.ext",
        ],
        complement_data = r#"
        {
            "name": "foobar",
            "files": [
                "a/file.ext",
                "another/file.ext"
            ]
        }
        "#,
        complement_files = [
            "a/file.ext",
            "another/file.ext",
        ],
    }
}
