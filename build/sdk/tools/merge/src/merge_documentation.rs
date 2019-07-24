// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use sdk_metadata::Documentation;

use crate::app::Result;
use crate::file_provider::FileProvider;
use crate::immutable::merge_immutable_element;
use crate::tarball::{InputTarball, OutputTarball, TarballContent};

impl FileProvider for Documentation {
    fn get_common_files(&self) -> Vec<String> {
        self.docs.clone()
    }
}

pub fn merge_documentation<F: TarballContent>(
    meta_path: &str, base: &impl InputTarball<F>, complement: &impl InputTarball<F>,
    output: &mut impl OutputTarball<F>,
) -> Result<()> {
    merge_immutable_element::<Documentation, _, _, _, _>(meta_path, base, complement, output)
}

#[cfg(test)]
mod tests {
    use super::*;

    test_merge_immutable_success! {
        name = test_merge,
        merge = merge_documentation,
        meta = "doc/foobar.meta.json",
        data = r#"
        {
            "name": "foobar",
            "type": "documentation",
            "docs": [
                "docs/foobar.md"
            ]
        }
        "#,
        files = [
            "docs/foobar.md"
        ],
    }

    test_merge_immutable_failure! {
        name = test_merge_failed,
        merge = merge_documentation,
        meta = "doc/foobar.meta.json",
        base_data = r#"
        {
            "name": "foobar",
            "type": "documentation",
            "docs": [
                "docs/foobar.md"
            ]
        }
        "#,
        base_files = [
            "docs/foobar.md",
        ],
        // This metadata has one extra document.
        complement_data = r#"
        {
            "name": "foobar",
            "type": "documentation",
            "docs": [
                "docs/foobar.md",
                "docs/foobar.too.md",
            ]
        }
        "#,
        complement_files = [
            "docs/foobar.md",
            "docs/foobar.too.md",
        ],
    }
}
