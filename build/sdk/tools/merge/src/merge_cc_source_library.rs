// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use sdk_metadata::CcSourceLibrary;

use crate::app::Result;
use crate::file_provider::FileProvider;
use crate::immutable::merge_immutable_element;
use crate::tarball::{InputTarball, OutputTarball, TarballContent};

impl FileProvider for CcSourceLibrary {
    fn get_common_files(&self) -> Vec<String> {
        let mut result = self.sources.clone();
        result.extend(self.headers.clone());
        result
    }
}

pub fn merge_cc_source_library<F: TarballContent>(
    meta_path: &str, base: &impl InputTarball<F>, complement: &impl InputTarball<F>,
    output: &mut impl OutputTarball<F>,
) -> Result<()> {
    merge_immutable_element::<CcSourceLibrary, _, _, _, _>(meta_path, base, complement, output)
}

#[cfg(test)]
mod tests {
    use super::*;

    test_merge_immutable_success! {
        name = test_merge,
        merge = merge_cc_source_library,
        meta = "pkg/foobar/meta.json",
        data = r#"
        {
            "name": "foobar",
            "type": "cc_source_library",
            "root": "pkg/foobar",
            "deps": [
                "raboof"
            ],
            "sources": [
                "pkg/foobar/one.cc",
                "pkg/foobar/two.cc"
            ],
            "headers": [
                "pkg/foobar/include/foobar.h"
            ],
            "include_dir": "pkg/foobar/include",
            "banjo_deps": [],
            "fidl_deps": [
                "foo.bar"
            ]
        }
        "#,
        files = [
            "pkg/foobar/one.cc",
            "pkg/foobar/two.cc",
            "pkg/foobar/include/foobar.h",
        ],
    }

    test_merge_immutable_failure! {
        name = test_merge_failed,
        merge = merge_cc_source_library,
        meta = "pkg/foobar/meta.json",
        base_data = r#"
        {
            "name": "foobar",
            "type": "cc_source_library",
            "root": "pkg/foobar",
            "deps": [
                "raboof"
            ],
            "sources": [
                "pkg/foobar/one.cc",
                "pkg/foobar/two.cc"
            ],
            "headers": [
                "pkg/foobar/include/foobar.h"
            ],
            "include_dir": "pkg/foobar/include",
            "banjo_deps": [],
            "fidl_deps": [
                "foo.bar"
            ]
        }
        "#,
        base_files = [
            "pkg/foobar/one.cc",
            "pkg/foobar/two.cc",
            "pkg/foobar/include/foobar.h",
        ],
        // This data has one more dependency.
        complement_data = r#"
        {
            "name": "foobar",
            "type": "cc_source_library",
            "root": "pkg/foobar",
            "deps": [
                "raboof",
                "whoops"
            ],
            "sources": [
                "pkg/foobar/one.cc",
                "pkg/foobar/two.cc"
            ],
            "headers": [
                "pkg/foobar/include/foobar.h"
            ],
            "include_dir": "pkg/foobar/include",
            "banjo_deps": [],
            "fidl_deps": [
                "foo.bar"
            ]
        }
        "#,
        complement_files = [
            "pkg/foobar/one.cc",
            "pkg/foobar/two.cc",
            "pkg/foobar/include/foobar.h",
        ],
    }
}
