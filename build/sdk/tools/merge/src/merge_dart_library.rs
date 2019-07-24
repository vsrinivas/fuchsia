// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use sdk_metadata::DartLibrary;

use crate::app::Result;
use crate::file_provider::FileProvider;
use crate::immutable::merge_immutable_element;
use crate::tarball::{InputTarball, OutputTarball, TarballContent};

impl FileProvider for DartLibrary {
    fn get_common_files(&self) -> Vec<String> {
        self.sources.clone()
    }
}

pub fn merge_dart_library<F: TarballContent>(
    meta_path: &str, base: &impl InputTarball<F>, complement: &impl InputTarball<F>,
    output: &mut impl OutputTarball<F>,
) -> Result<()> {
    merge_immutable_element::<DartLibrary, _, _, _, _>(meta_path, base, complement, output)
}

#[cfg(test)]
mod tests {
    use super::*;

    test_merge_immutable_success! {
        name = test_merge,
        merge = merge_dart_library,
        meta = "dart/foobar/meta.json",
        data = r#"
        {
            "name": "foobar",
            "type": "dart_library",
            "root": "dart/foobar",
            "sources": [
                "dart/foobar/lib/one.dart",
                "dart/foobar/lib/two.dart"
            ],
            "deps": [],
            "fidl_deps": [],
            "third_party_deps": []
        }
        "#,
        files = [
            "dart/foobar/lib/one.dart",
            "dart/foobar/lib/two.dart",
        ],
    }

    test_merge_immutable_failure! {
        name = test_merge_failed,
        merge = merge_dart_library,
        meta = "dart/foobar/meta.json",
        base_data = r#"
        {
            "name": "foobar",
            "type": "dart_library",
            "root": "dart/foobar",
            "sources": [
                "dart/foobar/lib/one.dart",
                "dart/foobar/lib/two.dart"
            ],
            "deps": [],
            "fidl_deps": [],
            "third_party_deps": []
        }
        "#,
        base_files = [
            "dart/foobar/lib/one.dart",
            "dart/foobar/lib/two.dart",
        ],
        // This metadata contains an extra dependency.
        complement_data = r#"
        {
            "name": "foobar",
            "type": "dart_library",
            "root": "dart/foobar",
            "sources": [
                "dart/foobar/lib/one.dart",
                "dart/foobar/lib/two.dart"
            ],
            "deps": [
                "raboof"
            ],
            "fidl_deps": [],
            "third_party_deps": []
        }
        "#,
        complement_files = [
            "dart/foobar/lib/one.dart",
            "dart/foobar/lib/two.dart",
        ],
    }
}
