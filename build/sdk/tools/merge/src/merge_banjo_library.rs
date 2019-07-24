// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use sdk_metadata::BanjoLibrary;

use crate::app::Result;
use crate::file_provider::FileProvider;
use crate::immutable::merge_immutable_element;
use crate::tarball::{InputTarball, OutputTarball, TarballContent};

impl FileProvider for BanjoLibrary {
    fn get_common_files(&self) -> Vec<String> {
        self.sources.clone()
    }
}

pub fn merge_banjo_library<F: TarballContent>(
    meta_path: &str, base: &impl InputTarball<F>, complement: &impl InputTarball<F>,
    output: &mut impl OutputTarball<F>,
) -> Result<()> {
    merge_immutable_element::<BanjoLibrary, _, _, _, _>(meta_path, base, complement, output)
}

#[cfg(test)]
mod tests {
    use super::*;

    test_merge_immutable_success! {
        name = test_merge,
        merge = merge_banjo_library,
        meta = "banjo/foo.bar/meta.json",
        data = r#"
        {
            "name": "foobar",
            "type": "banjo_library",
            "root": "banjo/foo.bar",
            "deps": [
                "rab.oof"
            ],
            "sources": [
                "banjo/foo.bar/one.banjo",
                "banjo/foo.bar/two.banjo"
            ]
        }
        "#,
        files = [
            "banjo/foo.bar/one.banjo",
            "banjo/foo.bar/two.banjo",
        ],
    }

    test_merge_immutable_failure! {
        name = test_merge_failed,
        merge = merge_banjo_library,
        meta = "banjo/foo.bar/meta.json",
        base_data = r#"
        {
            "name": "foobar",
            "type": "banjo_library",
            "root": "banjo/foo.bar",
            "deps": [
                "rab.oof"
            ],
            "sources": [
                "banjo/foo.bar/one.banjo",
                "banjo/foo.bar/two.banjo"
            ]
        }
        "#,
        base_files = [
            "banjo/foo.bar/one.banjo",
            "banjo/foo.bar/two.banjo",
        ],
        complement_data = r#"
        {
            "name": "foobar",
            "type": "banjo_library",
            "root": "banjo/foo.bar",
            "deps": [
                "rab.oof"
            ],
            "sources": [
                "banjo/foo.bar/one.banjo"
            ]
        }
        "#,
        complement_files = [
            "banjo/foo.bar/one.banjo",
        ],
    }
}
