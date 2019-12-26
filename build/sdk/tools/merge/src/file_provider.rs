// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::{HashMap, HashSet};

use thiserror::Error;

use sdk_metadata::TargetArchitecture;

use crate::app::Result;
use crate::tarball::{InputTarball, OutputTarball, TarballContent};

pub type CommonFiles = Vec<String>;
pub type ArchFiles = HashMap<TargetArchitecture, Vec<String>>;

/// A trait to extract files references from SDK elements.
pub trait FileProvider {
    /// Returns the list of architecture-independent files for this element.
    fn get_common_files(&self) -> CommonFiles {
        Vec::new()
    }

    /// Returns the list of architecture-dependent files for this element.
    fn get_arch_files(&self) -> ArchFiles {
        HashMap::new()
    }

    /// Returns all the files associated with this element.
    fn get_all_files(&self) -> Vec<String> {
        let mut result = self.get_common_files();
        for (_, files) in self.get_arch_files() {
            result.extend(files);
        }
        result
    }
}

/// Returns true if the given lists contain the same elements independently from any order.
fn are_lists_equal(one: &[String], two: &[String]) -> bool {
    let set_one: HashSet<_> = one.iter().collect();
    let set_two: HashSet<_> = two.iter().collect();
    set_one == set_two
}

/// Copies the given list of files from a tarball to another.
fn copy_file_list<F: TarballContent>(
    paths: &[String],
    input: &impl InputTarball<F>,
    output: &mut impl OutputTarball<F>,
) -> Result<()> {
    for path in paths {
        input.get_file(path, |file| output.write_file(path, file))?;
    }
    Ok(())
}

/// Errors thrown by `merge_files`.
#[derive(Debug, Error)]
enum Error {
    #[error("common files are different")]
    CommonFilesDiffer,
    #[error("contents of {} are different", path)]
    CommonFilesContentDiffer { path: String },
    #[error("arch files are different for {:?}", arch)]
    ArchFilesDiffer { arch: TargetArchitecture },
}

/// Verifies that the files in two input tarballs are consistent and copies them over to an output
/// tarball.
pub fn merge_files<F: TarballContent>(
    base: &impl FileProvider,
    base_tarball: &impl InputTarball<F>,
    complement: &impl FileProvider,
    complement_tarball: &impl InputTarball<F>,
    output: &mut impl OutputTarball<F>,
) -> Result<()> {
    let base_common = base.get_common_files();
    let complement_common = complement.get_common_files();
    if !are_lists_equal(&base_common, &complement_common) {
        return Err(Error::CommonFilesDiffer)?;
    }
    for path in &base_common {
        base_tarball.get_file(path, |base_file| {
            complement_tarball.get_file(path, |complement_file| {
                if !base_file.is_identical(complement_file)? {
                    return Err(Error::CommonFilesContentDiffer { path: path.clone() })?;
                }
                output.write_file(path, base_file)
            })
        })?;
    }

    let base_arch = base.get_arch_files();
    let complement_arch = complement.get_arch_files();
    let base_arches: HashSet<_> = base_arch.keys().collect();
    let complement_arches: HashSet<_> = complement_arch.keys().collect();
    for arch in base_arches.union(&complement_arches) {
        // Note: file contents are not being checked because build outputs are not stable.
        if base_arch.contains_key(arch) {
            if complement_arch.contains_key(arch) {
                if !are_lists_equal(
                    &base_arch.get(arch).unwrap(),
                    &complement_arch.get(arch).unwrap(),
                ) {
                    return Err(Error::ArchFilesDiffer { arch: arch.clone().to_owned() })?;
                }
            }
            copy_file_list(&base_arch.get(arch).unwrap(), base_tarball, output)?;
        } else {
            copy_file_list(&complement_arch.get(arch).unwrap(), complement_tarball, output)?;
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use sdk_metadata::TargetArchitecture;

    use crate::testing::{MockInputTarball, MockOutputTarball};

    use super::*;

    macro_rules! list {
        ( $( $element:expr ),* ) => {{
            let mut result = Vec::new();
            $(result.push($element.to_string());)*
            result
        }}
    }

    macro_rules! map {
        ( $( $arch:ident => [$( $element:expr ),*] ),* ) => {{
            let mut result = HashMap::new();
            $(
                result.insert(TargetArchitecture::$arch, list![$($element),*]);
            )*
            result
        }}
    }

    #[test]
    fn test_are_list_equals_yes() {
        let one = list!["1", "2", "3"];
        let two = list!["1", "2", "3"];
        assert!(are_lists_equal(&one, &two));
    }

    #[test]
    fn test_are_list_equals_no() {
        let one = list!["1", "2", "3"];
        let two = list!["1", "4"];
        assert!(!are_lists_equal(&one, &two));
    }

    #[test]
    fn test_are_list_equals_different_order() {
        let one = list!["1", "2", "3"];
        let two = list!["3", "1", "2"];
        assert!(are_lists_equal(&one, &two));
    }

    #[derive(Clone)]
    struct TestProvider {
        common: CommonFiles,
        arch: ArchFiles,
    }

    impl TestProvider {
        fn new(common: CommonFiles, arch: ArchFiles) -> Self {
            TestProvider { common: common, arch: arch }
        }

        fn common(common: CommonFiles) -> Self {
            Self::new(common, HashMap::new())
        }

        fn arch(arch: ArchFiles) -> Self {
            Self::new(Vec::new(), arch)
        }
    }

    impl FileProvider for TestProvider {
        fn get_common_files(&self) -> CommonFiles {
            self.common.clone()
        }

        fn get_arch_files(&self) -> ArchFiles {
            self.arch.clone()
        }
    }

    #[test]
    fn test_merge_files_common() {
        let base_files = TestProvider::common(list!["file_one", "file_two"]);
        let complement_files = base_files.clone();
        let base = MockInputTarball::new();
        base.add("file_one", "one");
        base.add("file_two", "two");
        let complement = MockInputTarball::new();
        complement.add("file_one", "one");
        complement.add("file_two", "two");
        let mut output = MockOutputTarball::new();
        merge_files(&base_files, &base, &complement_files, &complement, &mut output).unwrap();
        output.assert_has_file("file_one");
        output.assert_has_file("file_two");
    }

    #[test]
    fn test_merge_files_arch() {
        let base_files =
            TestProvider::arch(map![X64 => ["x64/target_file_one", "x64/target_file_two"]]);
        let complement_files =
            TestProvider::arch(map![Arm64 => ["arm64/target_file_one", "arm64/target_file_two"]]);
        let base = MockInputTarball::new();
        base.add("x64/target_file_one", "one");
        base.add("x64/target_file_two", "two");
        let complement = MockInputTarball::new();
        complement.add("arm64/target_file_one", "one");
        complement.add("arm64/target_file_two", "two");
        let mut output = MockOutputTarball::new();
        merge_files(&base_files, &base, &complement_files, &complement, &mut output).unwrap();
        output.assert_has_file("x64/target_file_one");
        output.assert_has_file("x64/target_file_two");
        output.assert_has_file("arm64/target_file_one");
        output.assert_has_file("arm64/target_file_two");
    }

    #[test]
    fn test_merge_files_both() {
        let base_files = TestProvider::new(
            list!["file_one", "file_two"],
            map![X64 => ["x64/target_file_one", "x64/target_file_two"]],
        );
        let complement_files = TestProvider::common(list!["file_one", "file_two"]);
        let base = MockInputTarball::new();
        base.add("file_one", "one");
        base.add("file_two", "two");
        base.add("x64/target_file_one", "and_one");
        base.add("x64/target_file_two", "and_two");
        let complement = MockInputTarball::new();
        complement.add("file_one", "one");
        complement.add("file_two", "two");
        let mut output = MockOutputTarball::new();
        merge_files(&base_files, &base, &complement_files, &complement, &mut output).unwrap();
        output.assert_has_file("file_one");
        output.assert_has_file("file_two");
        output.assert_has_file("x64/target_file_one");
        output.assert_has_file("x64/target_file_two");
    }

    #[test]
    fn test_merge_files_different_common_list() {
        let base_files = TestProvider::common(list!["file_one", "file_two"]);
        let complement_files = TestProvider::common(list!["file_one", "file_three"]);
        let base = MockInputTarball::new();
        base.add("file_one", "one");
        base.add("file_two", "two");
        let complement = MockInputTarball::new();
        complement.add("file_one", "one");
        complement.add("file_three", "three"); // Different file.
        let mut output = MockOutputTarball::new();
        assert!(
            merge_files(&base_files, &base, &complement_files, &complement, &mut output).is_err()
        );
    }

    #[test]
    fn test_merge_files_different_common_content() {
        let base_files = TestProvider::common(list!["file_one", "file_two"]);
        let complement_files = base_files.clone();
        let base = MockInputTarball::new();
        base.add("file_one", "one");
        base.add("file_two", "two");
        let complement = MockInputTarball::new();
        complement.add("file_one", "one");
        complement.add("file_two", "not really two!!"); // Different content.
        let mut output = MockOutputTarball::new();
        assert!(
            merge_files(&base_files, &base, &complement_files, &complement, &mut output).is_err()
        );
    }

    #[test]
    fn test_merge_files_different_arch_list() {
        let base_files =
            TestProvider::arch(map![X64 => ["x64/target_file_one", "x64/target_file_two"]]);
        let complement_files =
            TestProvider::arch(map![X64 => ["x64/target_file_one", "x64/target_file_three"]]);
        let base = MockInputTarball::new();
        base.add("x64/target_file_one", "one");
        base.add("x64/target_file_two", "two");
        let complement = MockInputTarball::new();
        complement.add("x64/target_file_one", "one");
        complement.add("x64/target_file_three", "three"); // Different file.
        let mut output = MockOutputTarball::new();
        assert!(
            merge_files(&base_files, &base, &complement_files, &complement, &mut output).is_err()
        );
    }
}
