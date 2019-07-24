// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use sdk_metadata::FidlLibrary;

use crate::app::Result;
use crate::file_provider::FileProvider;
use crate::immutable::merge_immutable_element;
use crate::tarball::{InputTarball, OutputTarball, TarballContent};

impl FileProvider for FidlLibrary {
    fn get_common_files(&self) -> Vec<String> {
        self.sources.clone()
    }
}

pub fn merge_fidl_library<F: TarballContent>(
    meta_path: &str, base: &impl InputTarball<F>, complement: &impl InputTarball<F>,
    output: &mut impl OutputTarball<F>,
) -> Result<()> {
    merge_immutable_element::<FidlLibrary, _, _, _, _>(meta_path, base, complement, output)
}

#[cfg(test)]
mod tests {
    use super::*;

    test_merge_immutable_success! {
        name = test_merge,
        merge = merge_fidl_library,
        meta = "fidl/foo.bar/meta.json",
        data = r#"
        {
            "name": "foo.bar",
            "type": "fidl_library",
            "root": "fidl/foo.bar",
            "deps": [
                "rab.oof"
            ],
            "sources": [
                "fidl/foo.bar/one.fidl",
                "fidl/foo.bar/two.fidl"
            ]
        }
        "#,
        files = [
            "fidl/foo.bar/one.fidl",
            "fidl/foo.bar/two.fidl",
        ],
    }

    test_merge_immutable_failure! {
        name = test_merge_failed,
        merge = merge_fidl_library,
        meta = "fidl/foo.bar/meta.json",
        base_data = r#"
        {
            "name": "foo.bar",
            "type": "fidl_library",
            "root": "fidl/foo.bar",
            "deps": [
                "rab.oof"
            ],
            "sources": [
                "fidl/foo.bar/one.fidl",
                "fidl/foo.bar/two.fidl"
            ]
        }
        "#,
        base_files = [
            "fidl/foo.bar/one.fidl",
            "fidl/foo.bar/two.fidl",
        ],
        // This metadata specifies one extra dependency.
        complement_data = r#"
        {
            "name": "foo.bar",
            "type": "fidl_library",
            "root": "fidl/foo.bar",
            "deps": [
                "rab.oof",
                "hmm.not.really"
            ],
            "sources": [
                "fidl/foo.bar/one.fidl",
                "fidl/foo.bar/two.fidl"
            ]
        }
        "#,
        complement_files = [
            "fidl/foo.bar/one.fidl",
            "fidl/foo.bar/two.fidl",
        ],
    }
}
